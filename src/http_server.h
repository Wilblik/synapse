#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include "http_parser.h"

#include <stdint.h>

typedef struct http_server_t http_server_t;
typedef struct http_conn_t http_conn_t;

typedef struct {
    /* Called on a successfully parsed request */
    void (*on_request)(http_conn_t* http_conn, http_request_t* http_req);

    /* Called when the server receives a malformed request (optional) */
    void (*on_bad_request)(http_conn_t* http_conn);

    /* Called on an internal server error (e.g. malloc failure) */
    void (*on_server_error)(http_conn_t* http_conn);
} http_server_callbacks_t;


http_server_t* http_server_create(uint16_t port, http_server_callbacks_t callbacks, int conn_timeout);
void http_server_run(http_server_t* http_server);
void http_server_stop(http_server_t* http_server);
void http_server_destroy(http_server_t* http_server);
bool http_server_send_data(http_conn_t* http_conn, const char* response, size_t data_len);
void http_server_close_conn(http_conn_t* http_conn);

#endif // HTTP_SERVER_H_
