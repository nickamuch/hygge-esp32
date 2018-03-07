#include "mgos.h"
#include "mgos_mqtt.h"
#include <time.h>


typedef struct outlet_state_t {
  char ac1;
  char ac2;
  char ac3;
  char ac4;
} outlet_state_t;

static outlet_state_t outlet_state = {.ac1=1, .ac2=1, .ac3=1, .ac4=1};

static void send_msg_cb(void *arg) {
  char topic[100], message[200];
  struct json_out out = JSON_OUT_BUF(message, sizeof(message));
  snprintf(topic, sizeof(topic), "nickamuch");
  json_printf(&out, "{nickamuchtest: testing, total_ram: %lu, free_ram: %lu}",
              (unsigned long) mgos_get_heap_size(),
              (unsigned long) mgos_get_free_heap_size());
  LOG(LL_INFO, ("Nicka sending MQTT message: %s", message));
  bool res = mgos_mqtt_pub(topic, message, strlen(message), 1, false);
  LOG(LL_INFO, ("Nicka sent MQTT message, res: %d", res));
  (void) arg;
}


/*
 * Callback signature for `mgos_mqtt_sub()` below.
 */
static void nick_cmd_handler(struct mg_connection *nc, const char *topic,
                             int topic_len, const char *msg, int msg_len,
                              void *ud){
  LOG(LL_INFO, ("Got message from topic %s, msg is: %s", topic, msg));
}

/*
 * Handler for outlet command
 */
static void outlet_cmd(struct mg_connection *nc, const char *topic,
                             int topic_len, const char *msg, int msg_len,
                              void *ud){
  char ac1, ac2, ac3, ac4;
  json_scanf(msg, msg_len, "{ac1: %B, ac2: %B, ac3: %B, ac4: %B}", &ac1, &ac2,
             &ac3, &ac4);
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

  LOG(LL_INFO, ("Got message from topic %s, msg is: %s", topic, msg));
}


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

  (void) evd;
  (void) arg;
}

static void button_cb(int pin, void *arg) {
  char topic[100], message[100];
  struct json_out out = JSON_OUT_BUF(message, sizeof(message));
  snprintf(topic, sizeof(topic), "/devices/%s/events",
           mgos_sys_config_get_device_id());
  json_printf(&out, "{total_ram: %lu, free_ram: %lu}",
              (unsigned long) mgos_get_heap_size(),
              (unsigned long) mgos_get_free_heap_size());
  bool res = mgos_mqtt_pub(topic, message, strlen(message), 1, false);
  LOG(LL_INFO, ("Pin: %d, published: %s", pin, res ? "yes" : "no"));
  (void) arg;
}

enum mgos_app_init_result mgos_app_init(void) {

  //mgos_set_timer(6000, MGOS_TIMER_REPEAT, send_msg_cb, NULL);
  //mgos_mqtt_sub("nick/cmd", nick_cmd_handler, NULL);
  mgos_mqtt_sub("1/cmd/outlets", outlet_cmd, NULL);

  /* Publish to MQTT on button press */
  mgos_gpio_set_button_handler(mgos_sys_config_get_pins_button(),
                               MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_NEG, 200,
                               button_cb, NULL);

  /* Network connectivity events */
  mgos_event_add_group_handler(MGOS_EVENT_GRP_NET, net_cb, NULL);

  return MGOS_APP_INIT_SUCCESS;
}
