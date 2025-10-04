/**
 * @file http_server.c
 * @brief Implements an asynchronous HTTP server on top of a TCP server.
 *
 * This file contains the logic for managing HTTP connections, parsing incoming
 * data streams, handling request bodies, and dispatching requests to user-defined callbacks.
 */

#include "http_server.h"
#include "tcp_server.h"

#include <string.h>

#define HEADERS_BUFF_SIZE 8192
#define BODY_IN_FILE_THRESHOLD (1024 * 1024)

/**
 * @brief Enumeration of states for an HTTP connection.
 */
typedef enum {
    CONN_STATE_READING_HEADERS,
    CONN_STATE_READING_BODY
} http_conn_state_t;

/**
 * @brief Represents a single HTTP client connection.
 */
struct http_conn_t {
    tcp_conn_t* tcp_conn;
    http_server_t* http_server;
    http_conn_state_t state;

    char headers_buff[HEADERS_BUFF_SIZE];
    size_t headers_buff_len;
    size_t headers_len;

    char* body_buff;
    FILE* body_file;
    size_t body_expected;
    size_t body_received;

    http_request_t parsed_request;
};

/**
 * @brief Represents the HTTP server instance.
 */
struct http_server_t {
    tcp_server_t* tcp_server;
    http_server_callbacks_t callbacks;
};

static void* http_on_connect(tcp_conn_t* tcp_conn, void* context);
static void http_on_data(tcp_conn_t* tcp_conn, void* conn_data, void* context, const char* buffer, size_t n_read);
static void http_on_close(tcp_conn_t* tcp_conn, void* conn_data, void* context);
static bool try_parse_request(http_conn_t* http_conn);
static bool init_body_reading(http_conn_t* http_conn);
static bool check_if_body_received(http_conn_t* http_conn);
static bool handle_request(http_conn_t* http_conn);
static void reset_http_conn(http_conn_t* http_conn);
static void bad_request(http_conn_t* http_conn);
static void internal_server_error(http_conn_t* http_conn);

http_server_t* http_server_create(uint16_t port, http_server_callbacks_t callbacks, int conn_timeout) {
    http_server_t* http_server = malloc(sizeof(http_server_t));
    if (!http_server) {
        perror("[ERROR] malloc() http_server_t");
        return NULL;
    }

    http_server->callbacks = callbacks;

    tcp_server_callbacks_t tcp_callbacks = {
        .on_connect = http_on_connect,
        .on_data = http_on_data,
        .on_close = http_on_close
    };

    http_server->tcp_server = tcp_server_create(port, tcp_callbacks, conn_timeout, http_server);
    if (!http_server->tcp_server) {
        fprintf(stderr, "[ERROR] Failed to create tcp server\n");
        free(http_server);
        return NULL;
    }

    return http_server;
}

void http_server_run(http_server_t* http_server) {
    if (!http_server) return;
    tcp_server_run(http_server->tcp_server);
}

void http_server_stop(http_server_t* http_server) {
    if (!http_server) return;
    tcp_server_stop(http_server->tcp_server);
}

void http_server_destroy(http_server_t* http_server) {
    if (!http_server) return;
    tcp_server_destroy(http_server->tcp_server);
    free(http_server);
}

bool http_server_send_data(http_conn_t* http_conn, const char* response, size_t data_len) {
    if (!http_conn || !response) return false;
    return tcp_server_write(http_conn->tcp_conn, response, data_len);
}

void http_server_close_conn(http_conn_t* http_conn) {
    if (!http_conn) return;
    tcp_server_close_conn(http_conn->tcp_conn);
}

/**
 * @brief Callback for when a new TCP connection is established.
 * Allocates and initializes an http_conn_t structure.
 * @param tcp_conn The underlying TCP connection object.
 * @param context A pointer to the http_server_t instance.
 * @return A pointer to the newly created http_conn_t structure.
 */
static void* http_on_connect(tcp_conn_t* tcp_conn, void* context) {
    http_conn_t* http_conn = calloc(1, sizeof(http_conn_t));
    if (!http_conn) {
        perror("[ERROR] calloc() http_conn_t");
        return NULL;
    }
    http_conn->tcp_conn = tcp_conn;
    http_conn->http_server = (http_server_t*)context;
    http_conn->state = CONN_STATE_READING_HEADERS;
    return http_conn;
}

/**
 * @brief Callback for when data is received on a TCP connection.
 * Processes the incoming data stream, parsing headers and bodies.
 * @param tcp_conn The TCP connection object.
 * @param conn_data A pointer to the associated http_conn_t.
 * @param context A pointer to the http_server_t instance.
 * @param buffer The buffer containing the received data.
 * @param n_read The number of bytes received.
 */
static void http_on_data(tcp_conn_t* tcp_conn, void* conn_data, void* context, const char* buffer, size_t n_read) {
    (void)tcp_conn;
    (void)context;

    http_conn_t* http_conn = (http_conn_t*)conn_data;
    const char* input_buff_pos = buffer;
    size_t data_left_to_read = n_read;

    while (data_left_to_read > 0) {
        switch (http_conn->state) {
            case CONN_STATE_READING_HEADERS: {
                size_t space_left = HEADERS_BUFF_SIZE - http_conn->headers_buff_len - 1;
                if (space_left == 0) {
                    const char* res = "HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\n\r\n";
                    if (http_server_send_data(http_conn, res, strlen(res))) {
                        http_server_close_conn(http_conn);
                    }
                    return;
                }

                size_t n_to_copy = data_left_to_read > space_left ? space_left : data_left_to_read;
                memcpy(http_conn->headers_buff + http_conn->headers_buff_len, input_buff_pos, n_to_copy);
                http_conn->headers_buff_len += n_to_copy;
                http_conn->headers_buff[http_conn->headers_buff_len] = '\0';

                input_buff_pos += n_to_copy;
                data_left_to_read -= n_to_copy;

                if (!try_parse_request(http_conn)) return;
                break;
            }
            case CONN_STATE_READING_BODY: {
                size_t body_remaining = http_conn->body_expected - http_conn->body_received;
                size_t to_write = data_left_to_read > body_remaining ? body_remaining : data_left_to_read;

                if (http_conn->body_file) {
                    size_t written = fwrite(input_buff_pos, 1, to_write, http_conn->body_file);
                    if (written != to_write) {
                        fprintf(stderr, "[ERROR] Failed to write body chunk to temp file.\n");
                        internal_server_error(conn_data);
                        return;
                    }
                } else {
                    memcpy(http_conn->body_buff + http_conn->body_received, input_buff_pos, to_write);
                }

                http_conn->body_received += to_write;
                input_buff_pos += to_write;
                data_left_to_read -= to_write;

                if (!check_if_body_received(conn_data)) return;
                break;
            }
        }
    }
}

/**
 * @brief Callback for when a TCP connection is closed.
 * Frees resources associated with the http_conn_t structure.
 * @param tcp_conn The TCP connection object.
 * @param conn_data A pointer to the associated http_conn_t.
 * @param context A pointer to the http_server_t instance.
 */
static void http_on_close(tcp_conn_t* tcp_conn, void* conn_data, void* context) {
    (void)tcp_conn;
    (void)context;

    http_conn_t* http_conn = (http_conn_t*)conn_data;
    if (!http_conn) return;

    if (http_conn->body_file) fclose(http_conn->body_file);
    if (http_conn->body_buff) free(http_conn->body_buff);

    http_free_request(&http_conn->parsed_request);
    free(http_conn);
}

/**
 * @brief Attempts to parse the headers buffer to see if a full request is present.
 * If a full request is found, it proceeds to handle it.
 * @param http_conn The connection object.
 * @return A boolean indicating if the connection should remain open.
 */
static bool try_parse_request(http_conn_t* http_conn) {
    char* headers_end = strstr(http_conn->headers_buff, "\r\n\r\n");
    if (!headers_end) return true;

    http_parse_err_t parse_err = http_parse_request(http_conn->headers_buff, &http_conn->parsed_request);
    if (parse_err != HTTP_PARSE_OK) {
        if (parse_err == HTTP_PARSE_ERR_BAD_REQUEST) bad_request(http_conn);
        else internal_server_error(http_conn);
        return false;
    }

    http_conn->headers_len = (headers_end + 4) - http_conn->headers_buff;

    const char* content_len_str = http_get_header_value(http_conn->parsed_request.headers, "Content-Length");
    if (content_len_str) {
        char* end;
        long content_len = strtol(content_len_str, &end, 10);
        if (*end != '\0' || content_len < 0) { bad_request(http_conn); return false; }
        http_conn->body_expected = (size_t)content_len;
    }

    if (http_conn->body_expected <= 0) {
        return handle_request(http_conn);
    }

    if (!init_body_reading(http_conn)) return false;
    return check_if_body_received(http_conn);
}

/**
 * @brief Initializes the necessary structures for reading the request body.
 * Decides whether to buffer the body in memory or in a temporary file.
 * @param http_conn The connection object.
 * @return A boolean indicating if initialization was successful.
 */
static bool init_body_reading(http_conn_t* http_conn) {
    http_conn->state = CONN_STATE_READING_BODY;

    if (http_conn->body_expected > BODY_IN_FILE_THRESHOLD) {
        http_conn->body_file = tmpfile();
        if (!http_conn->body_file) {
            internal_server_error(http_conn);
            return false;
        }
        http_conn->parsed_request.body_in_file = true;
        http_conn->parsed_request.body_file = http_conn->body_file;

    } else {
        http_conn->body_buff = malloc(sizeof(char) * http_conn->body_expected + 1);
        if (!http_conn->body_buff) {
            internal_server_error(http_conn);
            return false;
        }
        http_conn->parsed_request.body_in_file = false;
        http_conn->parsed_request.body = http_conn->body_buff;
    }

    size_t body_in_buffer = http_conn->headers_buff_len - http_conn->headers_len;
    if (body_in_buffer > 0) {
        size_t to_write = body_in_buffer > http_conn->body_expected ? http_conn->body_expected : body_in_buffer;
        char* body_start = http_conn->headers_buff + http_conn->headers_len;
        if (http_conn->body_file) {
            size_t written = fwrite(body_start, 1, to_write, http_conn->body_file);
            if (written != to_write) {
                internal_server_error(http_conn);
                return false;
            }
        } else {
            memcpy(http_conn->body_buff, body_start, to_write);
        }
        http_conn->body_received = to_write;
    }

    return true;
}

/**
 * @brief Checks if the entire request body has been received.
 * If it has, it calls the request handler.
 * @param http_conn The connection object.
 * @return A boolean indicating if the connection should remain open.
 */
static bool check_if_body_received(http_conn_t* http_conn) {
    if (http_conn->body_received >= http_conn->body_expected) {
        if (http_conn->body_file) {
            rewind(http_conn->body_file);
        } else {
            http_conn->body_buff[http_conn->body_expected] = '\0';
        }
        return handle_request(http_conn);
    }
    return true;
}

/**
 * @brief Handles a fully parsed HTTP request by invoking the user-provided callback.
 * Also handles connection persistence (Keep-Alive) and pipelining.
 * @param http_conn The connection object.
 * @return A boolean indicating if the connection should remain open for subsequent requests.
 */
static bool handle_request(http_conn_t* http_conn) {
    printf("[INFO] Request received from %s\n", tcp_server_conn_ip(http_conn->tcp_conn));


    if (http_conn->http_server && http_conn->http_server->callbacks.on_request) {
        /* Save pointer to tcp_conn in case http_conn gets freed after on_request callback */
        tcp_conn_t* tcp_conn = http_conn->tcp_conn;
        http_conn->http_server->callbacks.on_request(http_conn, &http_conn->parsed_request);
        if (tcp_server_is_conn_closed(tcp_conn)) return false;
    } else {
        const char* res = "HTTP/1.1 501 Not Implemented\r\nContent-Length: 0\r\n\r\n";
        if (!http_server_send_data(http_conn, res, strlen(res))) {
            return false;
        }
    }

    const char* conn_header = http_get_header_value(http_conn->parsed_request.headers, "Connection");
    bool should_close_conn = (conn_header && strcasecmp(conn_header, "close") == 0);
    if (should_close_conn) {
        http_server_close_conn(http_conn);
        return false;
    }

    size_t total_request_size = http_conn->headers_len + http_conn->body_expected;
    size_t data_left_to_read = http_conn->headers_buff_len > total_request_size ? http_conn->headers_buff_len - total_request_size : 0;
    if (data_left_to_read > 0) {
        memmove(http_conn->headers_buff, http_conn->headers_buff + total_request_size, data_left_to_read);
    }

    reset_http_conn(http_conn);
    http_conn->headers_buff_len = data_left_to_read;

    if (data_left_to_read > 0) {
        return try_parse_request(http_conn);
    }

    return true;
}

/**
 * @brief Resets the state of an http_conn_t to prepare for the next request on the same connection.
 * @param http_conn The connection object to reset.
 */
static void reset_http_conn(http_conn_t* http_conn) {
    http_free_request(&http_conn->parsed_request);

    if (http_conn->body_file) {
        fclose(http_conn->body_file);
        http_conn->body_file = NULL;
    }

    if (http_conn->body_buff) {
        free(http_conn->body_buff);
        http_conn->body_buff = NULL;
    }

    memset(&http_conn->parsed_request, 0, sizeof(http_request_t));
    http_conn->headers_len = 0;
    http_conn->body_expected = 0;
    http_conn->body_received = 0;
    http_conn->state = CONN_STATE_READING_HEADERS;
}

/**
 * @brief Handles a bad request by invoking the user-provided callback or sending a 400 response.
 * @param http_conn The connection object.
 */
static void bad_request(http_conn_t* http_conn) {
    if (http_conn->http_server && http_conn->http_server->callbacks.on_bad_request) {
        http_conn->http_server->callbacks.on_bad_request(http_conn);
        http_server_close_conn(http_conn);
    } else {
        const char* res = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
        if (http_server_send_data(http_conn, res, strlen(res))) {
            http_server_close_conn(http_conn);
        }
    }
}

/**
 * @brief Handles an internal server error by invoking the callback or sending a 500 response.
 * @param http_conn The connection object.
 */
static void internal_server_error(http_conn_t* http_conn) {
    perror("[ERROR] Internal server error");
    if (http_conn->http_server && http_conn->http_server->callbacks.on_server_error) {
        http_conn->http_server->callbacks.on_server_error(http_conn);
        http_server_close_conn(http_conn);
    } else {
        const char* res = "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n";
        if (http_server_send_data(http_conn, res, strlen(res))) {
            http_server_close_conn(http_conn);
        }
    }
}
