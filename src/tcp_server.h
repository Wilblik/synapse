#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* Represents the server instance */
typedef struct tcp_server_t tcp_server_t;

/* Represents a single client connection managed by the TCP server */
typedef struct tcp_conn_t tcp_conn_t;

typedef struct {
    /* Called when a new client connects. Should return a pointer to user defined connection data */
    void* (*on_connect)(tcp_conn_t* conn, void* context);
    /* Called when data is received from a client */
    void (*on_data)(tcp_conn_t* conn, void* conn_data, void* context, const char* buffer, size_t n_read);
    /* Called when a connection is closed. The user should free their connection data here */
    void (*on_close)(tcp_conn_t* conn, void* conn_data, void* context);
} tcp_server_callbacks_t;

/* Creates and initializes a new TCP server instance */
tcp_server_t* tcp_server_create(uint16_t port, tcp_server_callbacks_t callbacks, int conn_timeout, void* context);

/* Runs the main event loop for the server. This function blocks until the server is stopped */
void tcp_server_run(tcp_server_t* server);

/* Stops the server */
void tcp_server_stop(tcp_server_t* server);

/* Cleans up resources */
void tcp_server_destroy(tcp_server_t* server);

/* Sends data to a client */
bool tcp_server_write(tcp_conn_t* conn, const char* data, size_t len);

/* Closes a client connection */
void tcp_server_close_conn(tcp_conn_t* conn);

/* Returns the IP address string of the client */
const char* tcp_server_conn_ip(const tcp_conn_t* conn);

bool is_conn_closed(const tcp_conn_t* conn);

#endif // TCP_SERVER_H_
