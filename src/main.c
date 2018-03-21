#include "mgos.h"
#include "mgos_mqtt.h"
#include <time.h>

#define MESSAGING_VERSION 1
#define FIRMWARE_VERSION 1
#define STR_BUF_LEN 256
#define STR_BUF2_LEN 512
#define SEC_TO_MS 1000
#define DEVICE_ID "001"
#define DEVICE_INFO_STATUS_PATH "001/status/device_info"
#define POWER_OUTPUT_STATUS_PATH "001/status/power_output"
#define BATTERY_STATE_STATUS_PATH "001/status/battery_state"
#define AC_MAINS_STATUS_PATH "001/status/ac_mains"

typedef struct outlet_state_t {
  char ac1;
  char ac2;
  char ac3;
  char ac4;
} outlet_state_t;

typedef enum status_type_t{
  all,
  battery,
  ac_mains,
  power_output,
  device_info
} status_type_t;

uint battery_status_interval_ms = 300 * SEC_TO_MS;
uint ac_mains_status_interval_ms = 300 * SEC_TO_MS;
uint power_output_status_interval_ms = 60 * SEC_TO_MS;

mgos_timer_id battery_status_timer;
mgos_timer_id ac_mains_status_timer;
mgos_timer_id power_output_status_timer;

void * vp = NULL;
static char str_buf[STR_BUF_LEN];
static char str_buf2[STR_BUF2_LEN];
static outlet_state_t outlet_state = {.ac1=1, .ac2=1, .ac3=1, .ac4=1};

/**
 * Call callback and update timer to new value.
 */
static void change_callback_timer(
    timer_callback cb, mgos_timer_id* timer_handle, uint* p_interval_ms,
    uint new_interval_sec) {
  *p_interval_ms = new_interval_sec * SEC_TO_MS;
  cb(vp);
  mgos_clear_timer(*timer_handle);
  *timer_handle = mgos_set_timer(*p_interval_ms, MGOS_TIMER_REPEAT, cb, NULL);
}


/**
 * Status Handler
 */
static void send_battery_status(void * arg) {
  // TODO: implement
  LOG(LL_INFO, ("Unimplemented: send_battery_status"));
}

/**
 * Status Handler
 */
static void send_ac_mains_status(void * arg) {
  static uint msg_id = 0; // Resets to 0 after every device reset.

  // Print json to message buffer
  char* message = str_buf2;
  memset(message, 0, STR_BUF2_LEN);
  struct json_out out = JSON_OUT_BUF(message, STR_BUF2_LEN);
  json_printf(&out, "{version: %u, id: %u, ac_mains_on: %B}",
              MESSAGING_VERSION, msg_id, true); // TODO: use real status value

  // Send message
  bool sent = mgos_mqtt_pub(AC_MAINS_STATUS_PATH, message, strlen(message),
                            1, false);
  msg_id++;
  LOG(LL_INFO, ("sending ac_mains status. sent: %d, message: %s", sent,
                message));
}

/**
 * Status Handler
 */
static void send_power_output_status(void * arg) {
  // TODO: implement
  LOG(LL_INFO, ("Unimplemented: send_power_output_status"));
}

/**
 * Device info handler
 */
static void send_device_info(void * arg) {
  static uint msg_id = 0; // Resets to 0 after every device reset.
  char topic[] = DEVICE_INFO_STATUS_PATH;

  // Print json to message buffer
  char* message = str_buf2;
  memset(message, 0, STR_BUF2_LEN);
  struct json_out out = JSON_OUT_BUF(message, STR_BUF2_LEN);
  json_printf(&out, "{version: %u, id: %u, firmware: %u}",
              MESSAGING_VERSION, msg_id, FIRMWARE_VERSION);

  // Send message
  bool sent = mgos_mqtt_pub(DEVICE_INFO_STATUS_PATH, message, strlen(message),
                            1, false);
  msg_id++;
  LOG(LL_INFO, ("sending device_info status. sent: %d, message: %s", sent,
                message));
}

/*
 * Handler for set outlet switches command
 */
static void handle_outlet_switch_states_cmd(
    struct mg_connection *nc, const char *topic, int topic_len,
    const char *msg, int msg_len, void *ud){
  LOG(LL_INFO, ("outlet_switch_states message: %.*s", msg_len, msg));
  uint req_id;
  bool version, ac1, ac2, ac3, ac4;
  json_scanf(
      msg, msg_len,
      "{version: %u, id: %u, ac1: %B, ac2: %B, ac3: %B, ac4: %B}",
      &version, &req_id, &ac1, &ac2, &ac3, &ac4);
  if (ac1 != outlet_state.ac1){
    outlet_state.ac1 = ac1;
    //set_outlet(1, ac1);
  }
  if (ac2 != outlet_state.ac2){
    outlet_state.ac2 = ac2;
    //set_outlet(2, ac2);
  }
  if (ac3 != outlet_state.ac3){
    outlet_state.ac3 = ac4;
    //set_outlet(3, ac3);
  }
  if (ac4 != outlet_state.ac4){
    outlet_state.ac4 = ac4;
    //set_outlet(4, ac4);
  }
}

/*
 * Handler for set status interval command
 */
static void handle_status_interval_cmd(
    struct mg_connection *nc, const char *topic, int topic_len,
    const char *msg, int msg_len, void *ud){
  LOG(LL_INFO, ("status_interval message: %.*s", msg_len, msg));
  char version;
  uint req_id, interval;
  status_type_t status_type;

  json_scanf(msg, msg_len,
             "{version: %u, id: %u, status_type: %u, interval: %u}",
             &version, &req_id, &status_type, &interval);

  switch(status_type) {
    case battery:
      change_callback_timer(send_battery_status, &battery_status_timer,
                            &battery_status_interval_ms, interval);
      break;
    case ac_mains:
      change_callback_timer(send_ac_mains_status, &ac_mains_status_timer,
                            &ac_mains_status_interval_ms, interval);
      break;
    case power_output:
      change_callback_timer(
        send_power_output_status, &power_output_status_timer,
        &power_output_status_interval_ms, interval);
      break;
    case device_info:
      break; // Does not currently have a status interval.
    case all:
      change_callback_timer(send_battery_status, &battery_status_timer,
                            &battery_status_interval_ms, interval);
      change_callback_timer(send_ac_mains_status, &ac_mains_status_timer,
                            &ac_mains_status_interval_ms, interval);
      change_callback_timer(
        send_power_output_status, &power_output_status_timer,
        &power_output_status_interval_ms, interval);
      break;
  }
}

/*
 * Handler for charge command
 */
static void handle_charge_cmd(
    struct mg_connection *nc, const char *topic, int topic_len, const char *msg,
    int msg_len, void *ud){
  bool charge;
  char version;
  uint req_id;
  LOG(LL_INFO, ("charge_cmd message: %.*s", msg_len, msg));
  json_scanf(msg, msg_len,
             "{version: %u, id: %u, charge: %B}",
             &version, &req_id, &charge);
  // TODO: charge if possible
}

/*
 * Handler for discharge command
 */
static void handle_discharge_cmd(
    struct mg_connection *nc, const char *topic, int topic_len, const char *msg,
    int msg_len, void *ud){
  bool discharge;
  char version;
  uint req_id;
  LOG(LL_INFO, ("discharge_cmd message: %.*s", msg_len, msg));
  json_scanf(msg, msg_len,
             "{version: %u, id: %u, discharge: %B}",
             &version, &req_id, &discharge);
  // TODO: discharge if possible
}

/*
 * Handler for set light state command
 */
static void handle_light_state_cmd(
    struct mg_connection *nc, const char *topic, int topic_len, const char *msg,
    int msg_len, void *ud){
  char light_on;
  char version;
  uint req_id;
  LOG(LL_INFO, ("light state message: %.*s", msg_len, msg));
  json_scanf(msg, msg_len,
             "{version: %u, id: %u, light_on: %B}",
             &version, &req_id, &light_on);
  // TODO: Turn light on/off
}

/*
 * Handler for send status command
 */
static void handle_send_status_cmd(
    struct mg_connection *nc, const char *topic, int topic_len, const char *msg,
    int msg_len, void *ud){
  char status_type;
  char version;
  uint req_id;
  LOG(LL_INFO, ("send status cmd message: %.*s", msg_len, msg));

  json_scanf(msg, msg_len,
             "{version: %u, id: %u, status_type: %u}",
             &version, &req_id, &status_type);
  switch(status_type) {
    case battery:
      send_battery_status(vp);
      break;
    case ac_mains:
      send_ac_mains_status(vp);
      break;
    case power_output:
      send_power_output_status(vp);
      break;
    case device_info:
      send_device_info(vp);
      break;
    case all:
      send_power_output_status(vp);
      send_ac_mains_status(vp);
      send_battery_status(vp);
      send_device_info(vp);
      break;
  }
}

/**
 * Register an MQTT channel handler.  Prepend the device id to supplied channel
 */
void add_mqtt_channel_handler(char* path, sub_handler_t handler) {
  memset(str_buf, 0, STR_BUF_LEN);
  strcpy(str_buf, DEVICE_ID);
  strcpy(str_buf + strlen(str_buf), path);
  LOG(LL_INFO, ("subscribing to channel: %s", str_buf));
  mgos_mqtt_sub(str_buf, handler, NULL);
}

/**
 * Network event handler. Useful for debugging.
 */
static void net_cb(int ev, void *evd, void *arg) {
  switch (ev) {
    case MGOS_NET_EV_DISCONNECTED:
      LOG(LL_INFO, ("%s", "Net disconnected"));
      break;
    case MGOS_NET_EV_CONNECTING:
      LOG(LL_INFO, ("%s", "Net connecting..."));
      break;
    case MGOS_NET_EV_CONNECTED:
      LOG(LL_INFO, ("%s", "Net connected"));
      break;
    case MGOS_NET_EV_IP_ACQUIRED:
      LOG(LL_INFO, ("%s", "Net got IP address"));
      break;
  }
}

/**
 * Application entry point. Returns an enum specifying failure/success.
 */
enum mgos_app_init_result mgos_app_init(void) {
  // Define handlers for MQTT command channels
  add_mqtt_channel_handler("/cmd/outlet_switch_states",
                           handle_outlet_switch_states_cmd);
  add_mqtt_channel_handler("/cmd/status_interval", handle_status_interval_cmd);
  add_mqtt_channel_handler("/cmd/charge", handle_charge_cmd);
  add_mqtt_channel_handler("/cmd/discharge", handle_discharge_cmd);
  add_mqtt_channel_handler("/cmd/light_state", handle_light_state_cmd);
  add_mqtt_channel_handler("/cmd/send_status", handle_send_status_cmd);

  // Add a handler for network events. Currently just logs events
  mgos_event_add_group_handler(MGOS_EVENT_GRP_NET, net_cb, NULL);

  // Call our event checking function every 500 ms.
  battery_status_timer = mgos_set_timer(
    battery_status_interval_ms, MGOS_TIMER_REPEAT, send_battery_status, NULL);
  ac_mains_status_timer = mgos_set_timer(
    ac_mains_status_interval_ms, MGOS_TIMER_REPEAT, send_ac_mains_status,
    NULL);
  power_output_status_timer = mgos_set_timer(
    power_output_status_interval_ms, MGOS_TIMER_REPEAT,
    send_power_output_status, NULL);
  return MGOS_APP_INIT_SUCCESS;
}
