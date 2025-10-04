/**
 * @file http_server.h
 * @brief Defines the public interface for the HTTP server.
 *
 * This header provides the necessary structures and functions to create, run,
 * and manage a simple HTTP server.
 */

#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include "http_parser.h"

#include <stdint.h>

/**
 * @brief Opaque structure representing the HTTP server instance.
 */
typedef struct http_server_t http_server_t;

/**
 * @brief Opaque structure representing a single HTTP client connection.
 */
typedef struct http_conn_t http_conn_t;

/**
 * @brief Defines the callback functions for handling HTTP server events.
 */
typedef struct {
    /**
     * @brief Called when a request has been successfully parsed.
     * @param http_conn The connection object.
     * @param http_req The parsed HTTP request.
     */
    void (*on_request)(http_conn_t* http_conn, http_request_t* http_req);

    /**
     * @brief Called when the server receives a malformed request (optional).
     * If not provided, a default 400 Bad Request response is sent.
     * @param http_conn The connection object.
     */
    void (*on_bad_request)(http_conn_t* http_conn);

    /**
     * @brief Called on an internal server error (e.g., malloc failure).
     * If not provided, a default 500 Internal Server Error response is sent.
     * @param http_conn The connection object.
     */
    void (*on_server_error)(http_conn_t* http_conn);
} http_server_callbacks_t;


/**
 * @brief Creates and initializes a new HTTP server instance.
 * @param port The port number to listen on.
 * @param callbacks A structure containing callback functions for handling requests.
 * @param conn_timeout The timeout in seconds for inactive connections. 0 means no timeout.
 * @return A pointer to the new http_server_t instance, or NULL on failure.
 */
http_server_t* http_server_create(uint16_t port, http_server_callbacks_t callbacks, int conn_timeout);

/**
 * @brief Runs the main event loop for the HTTP server.
 * This function blocks until the server is stopped via http_server_stop().
 * @param http_server The server instance to run.
 */
void http_server_run(http_server_t* http_server);

/**
 * @brief Stops the HTTP server's event loop.
 * @param http_server The server instance to stop.
 */
void http_server_stop(http_server_t* http_server);

/**
 * @brief Destroys the HTTP server instance and frees all associated resources.
 * The server must be stopped before calling this function.
 * @param http_server The server instance to destroy.
 */
void http_server_destroy(http_server_t* http_server);

/**
 * @brief Sends data to a client over an HTTP connection.
 * @param http_conn The connection to send data to.
 * @param response A pointer to the data buffer.
 * @param data_len The length of the data to send.
 * @return A boolean indicating if the data was successfully queued for sending.
 */
bool http_server_send_data(http_conn_t* http_conn, const char* response, size_t data_len);

/**
 * @brief Closes a client connection.
 * @param http_conn The connection to close.
 */
void http_server_close_conn(http_conn_t* http_conn);

#endif // HTTP_SERVER_H_
