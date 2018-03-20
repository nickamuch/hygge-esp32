/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#include "mgos_http_server.h"

#if defined(MGOS_HAVE_ATCA)
#include "mgos_atca.h"
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/cs_file.h"
#include "common/json_utils.h"
#include "common/str_util.h"
#include "mgos_config_util.h"
#include "mgos_debug.h"
#include "mgos_debug_hal.h"
#include "mgos_hal.h"
#include "mgos_init.h"
#include "mgos_mongoose.h"
#include "mgos_net.h"
#include "mgos_sys_config.h"
#include "mgos_updater_common.h"
#include "mgos_utils.h"

#define MGOS_F_RELOAD_CONFIG MG_F_USER_5
#define BODY_LEN 200

#if MG_ENABLE_FILESYSTEM
static struct mg_serve_http_opts s_http_server_opts;
#endif
static struct mg_connection *s_listen_conn;
static struct mg_connection *s_listen_conn_tun;

#if MGOS_ENABLE_WEB_CONFIG

#define JSON_HEADERS "Connection: close\r\nContent-Type: application/json"

static void send_cfg(const void *cfg, const struct mgos_conf_entry *schema,
                     struct http_message *hm, struct mg_connection *c) {
  mg_send_response_line(c, 200, JSON_HEADERS);
  mg_send(c, "\r\n", 2);
  bool pretty = (mg_vcmp(&hm->query_string, "pretty") == 0);
  mgos_conf_emit_cb(cfg, NULL, schema, pretty, &c->send_mbuf, NULL, NULL);
}

static void conf_handler(struct mg_connection *c, int ev, void *p,
                         void *user_data) {
  struct http_message *hm = (struct http_message *) p;
  if (ev != MG_EV_HTTP_REQUEST) return;
  LOG(LL_DEBUG, ("[%.*s] requested", (int) hm->uri.len, hm->uri.p));
  struct mbuf jsmb;
  struct json_out jsout = JSON_OUT_MBUF(&jsmb);
  mbuf_init(&jsmb, 0);
  char *msg = NULL;
  int status = -1;
  int rc = 200;
  if (mg_vcmp(&hm->uri, "/conf/defaults") == 0) {
    struct mgos_config cfg;
    if (load_config_defaults(&cfg)) {
      send_cfg(&cfg, mgos_config_schema(), hm, c);
      mgos_conf_free(mgos_config_schema(), &cfg);
      status = 0;
    }
  } else if (mg_vcmp(&hm->uri, "/conf/current") == 0) {
    send_cfg(&mgos_sys_config, mgos_config_schema(), hm, c);
    status = 0;
  } else if (mg_vcmp(&hm->uri, "/conf/save") == 0) {
    struct mgos_config tmp;
    memset(&tmp, 0, sizeof(tmp));
    if (load_config_defaults(&tmp)) {
      char *acl_copy = (tmp.conf_acl == NULL ? NULL : strdup(tmp.conf_acl));
      if (mgos_conf_parse(hm->body, acl_copy, mgos_config_schema(), &tmp)) {
        status = (save_cfg(&tmp, &msg) ? 0 : -10);
      } else {
        status = -11;
      }
      free(acl_copy);
    } else {
      status = -10;
    }
    mgos_conf_free(mgos_config_schema(), &tmp);
    if (status == 0) c->flags |= MGOS_F_RELOAD_CONFIG;
  } else if (mg_vcmp(&hm->uri, "/conf/reset") == 0) {
    struct stat st;
    if (stat(CONF_USER_FILE, &st) == 0) {
      status = remove(CONF_USER_FILE);
    } else {
      status = 0;
    }
    if (status == 0) c->flags |= MGOS_F_RELOAD_CONFIG;
  }

  if (status != 0) {
    json_printf(&jsout, "{status: %d", status);
    if (msg != NULL) {
      json_printf(&jsout, ", message: %Q}", msg);
    } else {
      json_printf(&jsout, "}");
    }
    LOG(LL_ERROR, ("Error: %.*s", (int) jsmb.len, jsmb.buf));
    rc = 500;
  }

  if (jsmb.len > 0) {
    mg_send_head(c, rc, jsmb.len, JSON_HEADERS);
    mg_send(c, jsmb.buf, jsmb.len);
  }
  c->flags |= MG_F_SEND_AND_CLOSE;
  mbuf_free(&jsmb);
  free(msg);
  (void) user_data;
}

static void reboot_handler(struct mg_connection *c, int ev, void *p,
                           void *user_data) {
  (void) p;
  if (ev != MG_EV_HTTP_REQUEST) return;
  LOG(LL_DEBUG, ("Reboot requested"));
  mg_send_head(c, 200, 0, JSON_HEADERS);
  c->flags |= (MG_F_SEND_AND_CLOSE | MGOS_F_RELOAD_CONFIG);
  (void) user_data;
}

static void ro_vars_handler(struct mg_connection *c, int ev, void *p,
                            void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  LOG(LL_DEBUG, ("RO-vars requested"));
  struct http_message *hm = (struct http_message *) p;
  send_cfg(&mgos_sys_ro_vars, mgos_ro_vars_schema(), hm, c);
  c->flags |= MG_F_SEND_AND_CLOSE;
  (void) user_data;
}
#endif /* MGOS_ENABLE_WEB_CONFIG */

#if MGOS_ENABLE_FILE_UPLOAD
static struct mg_str upload_fname(struct mg_connection *nc,
                                  struct mg_str fname) {
  struct mg_str res = {NULL, 0};
  (void) nc;
  if (mgos_conf_check_access(fname, mgos_sys_config_get_http_upload_acl())) {
    res = fname;
  }
  return res;
}

static void upload_handler(struct mg_connection *c, int ev, void *p,
                           void *user_data) {
  mg_file_upload_handler(c, ev, p, upload_fname, user_data);
}
#endif

#if MGOS_ENABLE_TUNNEL
static void on_net_ready(int ev, void *evd, void *arg) {
  if (s_listen_conn_tun != NULL) {
    /* Depending on the WiFi status, allow or disallow tunnel reconnection */
    switch (ev) {
      case MGOS_NET_EV_DISCONNECTED:
        s_listen_conn_tun->flags |= MG_F_TUN_DO_NOT_RECONNECT;
        break;
      case MGOS_NET_EV_IP_ACQUIRED:
        s_listen_conn_tun->flags &= ~MG_F_TUN_DO_NOT_RECONNECT;
        break;
      default:
        break;
    }
  }

  (void) arg;
}
#endif /* MGOS_ENABLE_TUNNEL */

static void reboot_cb(void *arg) {
  mgos_system_restart();
}

static void mgos_http_ev(struct mg_connection *c, int ev, void *p,
                         void *user_data) {
  switch (ev) {
    case MG_EV_ACCEPT: {
      char addr[32];
      mg_sock_addr_to_str(&c->sa, addr, sizeof(addr),
                          MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      LOG(LL_INFO, ("%p HTTP connection from %s", c, addr));
      break;
    }
    case MG_EV_HTTP_REQUEST: {
#if MG_ENABLE_FILESYSTEM
      if (s_http_server_opts.document_root != NULL) {
        struct http_message *hm = (struct http_message *) p;

        LOG(LL_INFO, ("meth: %.*s", hm->method.len, hm->method.p));
        LOG(LL_INFO, ("uri: %.*s", hm->uri.len, hm->uri.p));

        // Handle POST to /configure_ap.html
        if (mg_vcmp(&(hm->method), "POST") == 0 &&
            mg_vcmp(&(hm->uri), "/configure_ap.html") == 0) {
          LOG(LL_DEBUG, ("responding to POST request"));
          LOG(LL_DEBUG, ("Body: %.*s", hm->body.len, hm->body.p));

          char *buf = calloc(1, BODY_LEN);
          char *name = calloc(1, BODY_LEN);
          char *pass = calloc(1, BODY_LEN);
          char input_valid = 1;
          if (hm->body.len < BODY_LEN) {
            memcpy(buf, hm->body.p, hm->body.len);
          } else {
            input_valid = 0;
          }

          // Extract access point name from body
          char *start_ptr = buf + strlen("ap_name=");
          char *end_ptr = strstr(buf, "&ap_pass=");
          int length = end_ptr - start_ptr;

          if (length < BODY_LEN && input_valid) {
            memcpy(name, start_ptr, length);
            //TODO need to handle escaped characters
          } else {
            input_valid = 0;
          }

          // Extract access point name from body
          start_ptr = end_ptr + strlen("&ap_name=");
          end_ptr = buf + strlen(buf);
          length = end_ptr - start_ptr;
          if (length < BODY_LEN && input_valid) {
            memcpy(pass, start_ptr, length);
            //TODO need to handle escaped characters
          } else {
            input_valid = 0;
          }

          LOG(LL_DEBUG, ("body len: %d, strlen %d", hm->body.len, strlen(buf)));
          LOG(LL_DEBUG, ("input_valid: %d, extracted: %s-- %s--",
                         input_valid, name, pass));

           if (input_valid) {
             // Save WiFi parameters in persistent storage and restart the device
             // to use these parameters.
             LOG(LL_INFO, ("Configuring WiFi"));
             mgos_sys_config_set_wifi_sta_pass(pass);
             mgos_sys_config_set_wifi_sta_ssid(name);
             mgos_sys_config_set_wifi_sta_enable(true);
             char *err = NULL;
             save_cfg(&mgos_sys_config, &err);
             printf("Saving configuration: %s\n", err ? err : "no error");
             free(err);

             // Allow server time to serve web page before reboot
             mgos_set_timer(3000, 0, reboot_cb, NULL);
             LOG(LL_INFO, ("Rebooting"));
           } else{
             LOG(LL_ERROR, ("Invalid access point info. Not configuring."));
           }

          free(buf);
          free(name);
          free(pass);
          mg_serve_http(c, p, s_http_server_opts);
        }

        // Serve GET requests. Invalid paths will get a 404 response
        else if(mg_vcmp(&(hm->method), "GET") == 0) {
          LOG(LL_INFO, ("serving GET request"));
          mg_serve_http(c, p, s_http_server_opts);
          (void) hm;
        }
      } else
#endif
      {
        mg_http_send_error(c, 404, "Not Found");
      }
      break;
    }
    case MG_EV_HTTP_MULTIPART_REQUEST: {
      mg_http_send_error(c, 404, "Not Found");
      break;
    }
    case MG_EV_CLOSE: {
      /* If we've sent the reply to the server, and should reboot, reboot */
      if (c->flags & MGOS_F_RELOAD_CONFIG) {
        c->flags &= ~MGOS_F_RELOAD_CONFIG;
        mgos_system_restart();
      }
      break;
    }
  }
  (void) user_data;
}

bool mgos_http_server_init(void) {
  if (!mgos_sys_config_get_http_enable()) {
    return true;
  }

  if (mgos_sys_config_get_http_listen_addr() == NULL) {
    LOG(LL_WARN, ("HTTP Server disabled, listening address is empty"));
    return true; /* At this moment it is just warning */
  }

#if MG_ENABLE_FILESYSTEM
  s_http_server_opts.document_root = mgos_sys_config_get_http_document_root();
  s_http_server_opts.hidden_file_pattern =
      mgos_sys_config_get_http_hidden_files();
  s_http_server_opts.auth_domain = mgos_sys_config_get_http_auth_domain();
  s_http_server_opts.global_auth_file = mgos_sys_config_get_http_auth_file();
#endif

  struct mg_bind_opts opts;
  memset(&opts, 0, sizeof(opts));
#if MG_ENABLE_SSL
  opts.ssl_cert = mgos_sys_config_get_http_ssl_cert();
  opts.ssl_key = mgos_sys_config_get_http_ssl_key();
  opts.ssl_ca_cert = mgos_sys_config_get_http_ssl_ca_cert();
#if CS_PLATFORM == CS_P_ESP8266
/*
 * ESP8266 cannot handle DH of any kind, unless there's hardware acceleration,
 * it's too slow.
 */
#if defined(MGOS_HAVE_ATCA)
  if (mbedtls_atca_is_available()) {
    opts.ssl_cipher_suites =
        "TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256:"
        "TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256:"
        "TLS-ECDH-ECDSA-WITH-AES-128-GCM-SHA256:"
        "TLS-ECDH-ECDSA-WITH-AES-128-CBC-SHA256:"
        "TLS-ECDH-ECDSA-WITH-AES-128-CBC-SHA:"
        "TLS-ECDH-RSA-WITH-AES-128-GCM-SHA256:"
        "TLS-ECDH-RSA-WITH-AES-128-CBC-SHA256:"
        "TLS-ECDH-RSA-WITH-AES-128-CBC-SHA:"
        "TLS-RSA-WITH-AES-128-GCM-SHA256:"
        "TLS-RSA-WITH-AES-128-CBC-SHA256:"
        "TLS-RSA-WITH-AES-128-CBC-SHA";
  } else
#endif /* defined(MGOS_HAVE_ATCA) */
    opts.ssl_cipher_suites =
        "TLS-RSA-WITH-AES-128-GCM-SHA256:"
        "TLS-RSA-WITH-AES-128-CBC-SHA256:"
        "TLS-RSA-WITH-AES-128-CBC-SHA";
#endif /* CS_PLATFORM == CS_P_ESP8266 */
#endif /* MG_ENABLE_SSL */
  s_listen_conn =
      mg_bind_opt(mgos_get_mgr(), mgos_sys_config_get_http_listen_addr(),
                  mgos_http_ev, NULL, opts);

  if (!s_listen_conn) {
    LOG(LL_ERROR,
        ("Error binding to [%s]", mgos_sys_config_get_http_listen_addr()));
    return false;
  }

  s_listen_conn->recv_mbuf_limit = MGOS_RECV_MBUF_LIMIT;

  mg_set_protocol_http_websocket(s_listen_conn);
  LOG(LL_INFO,
      ("HTTP server started on [%s]%s", mgos_sys_config_get_http_listen_addr(),
#if MG_ENABLE_SSL
       (opts.ssl_cert ? " (SSL)" : "")
#else
       ""
#endif
           ));

#if MGOS_ENABLE_TUNNEL
  if (mgos_sys_config_get_http_tunnel_enable() &&
      mgos_sys_config_get_device_id() != NULL &&
      mgos_sys_config_get_device_password() != NULL) {
    char *tun_addr = NULL;
    /*
     * NOTE: we won't free `tun_addr`, because when reconnect happens, this
     * address string will be accessed again.
     */
    if (mg_asprintf(&tun_addr, 0, "ws://%s:%s@%s.%s",
                    mgos_sys_config_get_device_id(),
                    mgos_sys_config_get_device_password(),
                    mgos_sys_config_get_device_id(),
                    mgos_sys_config_get_http_tunnel_addr()) < 0) {
      return false;
    }
    s_listen_conn_tun =
        mg_bind_opt(mgos_get_mgr(), tun_addr, mgos_http_ev, opts);

    if (s_listen_conn_tun == NULL) {
      LOG(LL_ERROR, ("Error binding to [%s]", tun_addr));
      return false;
    } else {
      /*
       * Network is not yet ready, so we need to set a flag which prevents the
       * tunnel from reconnecting. The flag will be cleared when wifi connection
       * is ready.
       */
      s_listen_conn_tun->flags |= MG_F_TUN_DO_NOT_RECONNECT;
      mgos_event_add_group_handler(MGOS_EVENT_GRP_NET, on_net_ready, NULL);
    }

    mg_set_protocol_http_websocket(s_listen_conn_tun);
    LOG(LL_INFO, ("Tunneled HTTP server started on [%s]%s", tun_addr,
#if MG_ENABLE_SSL
                  (opts.ssl_cert ? " (SSL)" : "")
#else
                  ""
#endif
                      ));
  }
#endif

#if MGOS_ENABLE_WEB_CONFIG
  mgos_register_http_endpoint("/conf/", conf_handler, NULL);
  mgos_register_http_endpoint("/reboot", reboot_handler, NULL);
  mgos_register_http_endpoint("/ro_vars", ro_vars_handler, NULL);
#endif
#if MGOS_ENABLE_FILE_UPLOAD
  mgos_register_http_endpoint("/upload", upload_handler, NULL);
#endif

  return true;
}

void mgos_register_http_endpoint_opt(const char *uri_path,
                                     mg_event_handler_t handler,
                                     struct mg_http_endpoint_opts opts) {
  if (s_listen_conn != NULL) {
    mg_register_http_endpoint_opt(s_listen_conn, uri_path, handler, opts);
  }
  if (s_listen_conn_tun != NULL) {
    mg_register_http_endpoint_opt(s_listen_conn_tun, uri_path, handler, opts);
  }
}

void mgos_register_http_endpoint(const char *uri_path,
                                 mg_event_handler_t handler, void *user_data) {
  struct mg_http_endpoint_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.user_data = user_data;
  opts.auth_domain = mgos_sys_config_get_http_auth_domain();
  opts.auth_file = mgos_sys_config_get_http_auth_file();
  mgos_register_http_endpoint_opt(uri_path, handler, opts);
}

struct mg_connection *mgos_get_sys_http_server(void) {
  return s_listen_conn;
}

void mgos_http_server_set_document_root(const char *document_root) {
  s_http_server_opts.document_root = document_root;
}
