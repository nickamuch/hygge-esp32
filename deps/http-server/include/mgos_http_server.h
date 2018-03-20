/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_MOS_LIBS_HTTP_SERVER_SRC_MGOS_HTTP_SERVER_H_
#define CS_MOS_LIBS_HTTP_SERVER_SRC_MGOS_HTTP_SERVER_H_

#include <stdbool.h>

#include "mgos_config.h"
#include "mgos_init.h"
#include "common/cs_dbg.h"

#if defined(__cplusplus)
extern "C" {  // Make sure we have C-declarations in C++ programs
#endif

/*
 * Return global listening connection
 */
struct mg_connection *mgos_get_sys_http_server(void);

/*
 * Register HTTP endpoint handler `handler` on URI `uri_path`
 *
 * Example:
 * ```c
 * static void foo_handler(struct mg_connection *c, int ev, void *p,
 *                         void *user_data) {
 *   (void) p;
 *   if (ev != MG_EV_HTTP_REQUEST) return;
 *   LOG(LL_INFO, ("Foo requested"));
 *   mg_send_response_line(c, 200,
 *                         "Content-Type: text/html\r\n");
 *   mg_printf(c, "%s\r\n", "Fooooo");
 *   c->flags |= (MG_F_SEND_AND_CLOSE | MGOS_F_RELOAD_CONFIG);
 *   (void) user_data;
 * }
 *
 * // Somewhere else:
 * mgos_register_http_endpoint("/foo/", foo_handler, NULL);
 * ```
 */
void mgos_register_http_endpoint(const char *uri_path,
                                 mg_event_handler_t handler, void *user_data);

/*
 * Like `mgos_register_http_endpoint()`, but additionally takes `struct
 * mg_http_endpoint_opts opts`
 */
void mgos_register_http_endpoint_opt(const char *uri_path,
                                     mg_event_handler_t handler,
                                     struct mg_http_endpoint_opts opts);

/*
 * Set document root to serve static content from. Setting it to NULL disables
 * static server (404 will be returned).
 */
void mgos_http_server_set_document_root(const char *document_root);

#if defined(__cplusplus)
}
#endif

#endif /* CS_MOS_LIBS_HTTP_SERVER_SRC_MGOS_HTTP_SERVER_H_ */
