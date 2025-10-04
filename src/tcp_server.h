/**
 * @file tcp_server.h
 * @brief Defines the public interface for a non-blocking TCP server.
 *
 * This header provides the structures and functions necessary to create and manage
 * an asynchronous TCP server using a callback-based approach.
 */

#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Opaque structure representing the server instance.
 */
typedef struct tcp_server_t tcp_server_t;

/**
 * @brief Opaque structure representing a single client connection.
 */
typedef struct tcp_conn_t tcp_conn_t;

/**
 * @brief Defines the callback functions for handling TCP server events.
 */
typedef struct {
    /**
     * @brief Called when a new client connects.
     * @param conn The new connection object.
     * @param context The user-defined context pointer from tcp_server_create.
     * @return A pointer to user-defined connection data to be associated with this connection.
     */
    void* (*on_connect)(tcp_conn_t* conn, void* context);
    /**
     * @brief Called when data is received from a client.
     * @param conn The connection object.
     * @param conn_data The user-defined data for this connection.
     * @param context The user-defined context pointer from tcp_server_create.
     * @param buffer The buffer containing the received data.
     * @param n_read The number of bytes in the buffer.
     */
    void (*on_data)(tcp_conn_t* conn, void* conn_data, void* context, const char* buffer, size_t n_read);
    /**
     * @brief Called when a connection is closed.
     * The user should free their connection data here.
     * @param conn The connection object.
     * @param conn_data The user-defined data for this connection.
     * @param context The user-defined context pointer from tcp_server_create.
     */
    void (*on_close)(tcp_conn_t* conn, void* conn_data, void* context);
} tcp_server_callbacks_t;

/**
 * @brief Creates and initializes a new TCP server instance.
 * @param port The port number to listen on.
 * @param callbacks A structure with user-defined callback functions.
 * @param conn_timeout Timeout in seconds for inactive connections. 0 for no timeout.
 * @param context A user-defined context pointer that will be passed to callback functions.
 * @return A pointer to the new tcp_server_t instance, or NULL on failure.
 */
tcp_server_t* tcp_server_create(uint16_t port, tcp_server_callbacks_t callbacks, int conn_timeout, void* context);

/**
 * @brief Runs the main event loop for the server.
 * This function blocks until the server is stopped.
 * @param server The server instance to run.
 */
void tcp_server_run(tcp_server_t* server);

/**
 * @brief Stops the server's event loop.
 * @param server The server instance to stop.
 */
void tcp_server_stop(tcp_server_t* server);

/**
 * @brief Cleans up resources used by the server.
 * The server must be stopped before calling this.
 * @param server The server instance to destroy.
 */
void tcp_server_destroy(tcp_server_t* server);

/**
 * @brief Sends data to a client.
 * @param conn The connection to send data to.
 * @param data Pointer to the data buffer.
 * @param len The length of the data to send.
 * @return True if the data was sent or buffered successfully, false on error.
 */
bool tcp_server_write(tcp_conn_t* conn, const char* data, size_t len);

/**
 * @brief Closes a client connection.
 * @param conn The connection to close.
 */
void tcp_server_close_conn(tcp_conn_t* conn);

/**
 * @brief Returns the IP address string of the client.
 * @param conn The connection object.
 * @return A const char pointer to the IP address string.
 */
const char* tcp_server_conn_ip(const tcp_conn_t* conn);

/**
 * @brief Checks if a connection is marked as closed.
 * @param conn The connection object.
 * @return True if the connection is closed, false otherwise.
 */
bool tcp_server_is_conn_closed(const tcp_conn_t* conn);

#endif // TCP_SERVER_H_
