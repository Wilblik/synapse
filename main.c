#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <asm-generic/socket.h>

#include "http_parser.h"

#define PORT 8080
#define MAX_EVENTS 64 // Max events to handle in a single epoll_wait call
#define HEADERS_BUFF_SIZE 8192 // Size of buffer for HTTP headers
#define BODY_IN_FILE_THRESHOLD (1024 * 1024) // Threshold above which body is saved into a file instead of memory
#define MAX_BODY_BUFF_SIZE (1024 * 1024) // Max size of buffer for reading body into a file when it is larger than BODY_IN_FILE_THRESHOLD

typedef enum {
    CONN_STATE_READING_HEADERS,
    CONN_STATE_READING_BODY,
    CONN_STATE_HANDLING_REQUEST
} conn_state_t;

typedef struct conn_data_t {
    conn_state_t state;
    int socket_fd;
    char ip_addr[INET_ADDRSTRLEN];

    char headers_buff[HEADERS_BUFF_SIZE];
    size_t curr_request_len;
    size_t headers_buff_len;

    char* body_buff;
    FILE* body_file;
    size_t body_expected;
    size_t body_received;

    http_request_t parsed_request;
} conn_data_t;

volatile sig_atomic_t running = 1;
void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

bool set_socket_nonblocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("[ERROR] fcntl() F_GETFL");
        return false;
    }
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("[ERROR] fcntl() F_SETFL");
        return false;
    }
    return true;
}

bool register_socket_with_epoll(int epoll_fd, int socket_fd, void* data) {
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET; // Watch for incoming data (EPOLLIN) in Edge-Triggered mode (EPOLLET)
    if (data) event.data.ptr = data;
    else event.data.fd = socket_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event) < 0) {
        perror("[ERROR] epoll_ctl() add listen_socket");
        return false;
    }
    return true;
}

int create_listening_socket(int epoll_fd) {
    int listen_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket_fd < 0) {
        perror("[ERROR] socket()");
        return -1;
    }

    int optval = 1;
    if (setsockopt(listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("[ERROR] setsockopt()");
        return -1;
    }

    if (!set_socket_nonblocking(listen_socket_fd)) {
        fprintf(stderr, "[ERROR] Failed to set listening socket as non blocking\n");
        close(listen_socket_fd);
        return -1;
    }

    /* Bind the socket to our IP and port */
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(listen_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERROR] bind()");
        return -1;
    }

    if (!register_socket_with_epoll(epoll_fd, listen_socket_fd, NULL)) {
        fprintf(stderr, "[ERROR] Failed to register listening socket with epoll");
        close(listen_socket_fd);
        return -1;
    }

    if (listen(listen_socket_fd, SOMAXCONN) < 0) {
        perror("[ERROR] listen()");
        close(listen_socket_fd);
        return -1;
    }

    return listen_socket_fd;
}

void handle_new_conn_event(int epoll_fd, int listen_socket_fd) {
    while (true) {
        struct sockaddr_in client_addr = {0};
        socklen_t client_len = sizeof(client_addr);

        /* Accept new connection */
        int conn_socket_fd = accept(listen_socket_fd, (struct sockaddr*)&client_addr, &client_len);

        if (conn_socket_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // All pending connections accepted
            perror("[ERROR] accept()");
            break;
        }

        if (!set_socket_nonblocking(conn_socket_fd)) {
            fprintf(stderr, "[ERROR] Failed to set connection socket as non blocking\n");
            close(conn_socket_fd);
            continue;
        }

        conn_data_t* conn_data = malloc(sizeof(conn_data_t));
        if (!conn_data) {
            fprintf(stderr, "[ERROR] Failed to allocate memory for new client connection data");
            close(conn_socket_fd);
            continue;
        }

        memset(conn_data, 0, sizeof(conn_data_t));
        conn_data->socket_fd = conn_socket_fd;
        conn_data->headers_buff_len = 0;
        inet_ntop(AF_INET, &client_addr.sin_addr, conn_data->ip_addr, sizeof(conn_data->ip_addr));

        if (!register_socket_with_epoll(epoll_fd, conn_socket_fd, conn_data)) {
            fprintf(stderr, "[ERROR] Failed to register connection socket with epoll");
            close(conn_socket_fd);
            free(conn_data);
            continue;
        }

        printf("[INFO] Accepted connection from %s (fd %d)\n", conn_data->ip_addr, conn_socket_fd);
    }
}

static ssize_t read_data_from_socket(conn_data_t* conn_data, void* buff_addr, size_t n_bytes_to_read);
static bool check_and_parse_headers(conn_data_t* conn_data);
static bool init_body_reading(conn_data_t* conn_data);
static void check_if_body_received(conn_data_t* conn_data);
static void bad_request(conn_data_t* conn_data);
static void internal_server_error(conn_data_t* conn_data);
static void send_and_close_conn(conn_data_t* conn_data, char* response);
static void close_conn(conn_data_t* conn_data);

void handle_client_event(conn_data_t* conn_data) {
    while (true) {
        switch(conn_data->state) {
            case CONN_STATE_READING_HEADERS: {
                size_t n_bytes_to_read = HEADERS_BUFF_SIZE - conn_data->headers_buff_len - 1;
                if (n_bytes_to_read == 0) {
                    send_and_close_conn(conn_data, "HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\n\r\n");
                    return;
                }

                char* buff_addr = conn_data->headers_buff + conn_data->headers_buff_len;
                ssize_t n_bytes_read = read_data_from_socket(conn_data, buff_addr, n_bytes_to_read);
                if (n_bytes_read <= 0) return;

                conn_data->headers_buff_len += n_bytes_read;
                conn_data->headers_buff[conn_data->headers_buff_len] = '\0';

                if (!check_and_parse_headers(conn_data)) return;
                break;
            }
            case CONN_STATE_READING_BODY: {
                size_t data_left_to_receive = conn_data->body_expected - conn_data->body_received;
                size_t n_bytes_to_read = MAX_BODY_BUFF_SIZE > data_left_to_receive + 1 ? data_left_to_receive : MAX_BODY_BUFF_SIZE;

                if (conn_data->body_file) {
                    ssize_t n_bytes_read = read_data_from_socket(conn_data, conn_data->body_buff, n_bytes_to_read);
                    if (n_bytes_read <= 0) return;
                    conn_data->body_received += n_bytes_read;

                    size_t written = fwrite(conn_data->body_buff, 1, n_bytes_read, conn_data->body_file);
                    if (written != (size_t)n_bytes_read) {
                        fprintf(stderr, "[ERROR] Failed to write body chunk to temp file.\n");
                        internal_server_error(conn_data);
                        return;
                    }
                } else {
                    ssize_t n_bytes_read = read_data_from_socket(conn_data, conn_data->body_buff + conn_data->body_received, n_bytes_to_read);
                    if (n_bytes_read <= 0) return;
                    conn_data->body_received += n_bytes_read;
                    conn_data->body_buff[conn_data->body_received] = '\0';
                }

                check_if_body_received(conn_data);
                break;
            }
            case CONN_STATE_HANDLING_REQUEST: {
                printf("[INFO] Request received from %s (fd %d):\n", conn_data->ip_addr, conn_data->socket_fd);

                // TODO Handle incomplete write
                http_print_request(&conn_data->parsed_request);
                char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
                write(conn_data->socket_fd, response, strlen(response));

                const char* conn_header = http_get_header_value(conn_data->parsed_request.headers, "Connection");
                bool should_close_conn = conn_header && strcasecmp(conn_header, "close") == 0;
                if (should_close_conn) {
                    close_conn(conn_data);
                    return;
                }
                http_free_request(&conn_data->parsed_request);

                /* Handle HTTP pipelining in case there is leftover data in headers buff */
                /* This can happen if we read another request while reading data inside CONN_STATE_READING_HEADERS */
                size_t data_left = conn_data->headers_buff_len - conn_data->curr_request_len;
                if (data_left > 0) {
                    char *request_end = conn_data->headers_buff + conn_data->curr_request_len;
                    memmove(conn_data->headers_buff, request_end, data_left);
                }

                conn_data->headers_buff_len = data_left;
                conn_data->curr_request_len = 0;

                if (conn_data->body_buff) {
                    free(conn_data->body_buff);
                    conn_data->body_buff = NULL;
                }
                if (conn_data->body_file) {
                    fclose(conn_data->body_file);
                    conn_data->body_file = NULL;
                }

                conn_data->state = CONN_STATE_READING_HEADERS;
                if (!check_and_parse_headers(conn_data)) return;
                break;
            }
        }
    }
}

/* Returns true if handling was ok and server can proceed, false if not */
static bool check_and_parse_headers(conn_data_t* conn_data) {
    char* headers_end = strstr(conn_data->headers_buff, "\r\n\r\n");
    if (!headers_end) return true;

    http_parse_err_t parse_err = http_parse_request(conn_data->headers_buff, &conn_data->parsed_request);

    switch (parse_err) {
        case HTTP_PARSE_OK: {
            size_t headers_len = (headers_end + 4) - conn_data->headers_buff;
            conn_data->curr_request_len = headers_len;

            const char* content_len_str = http_get_header_value(conn_data->parsed_request.headers, "Content-Length");
            if (content_len_str) {
                char* end;
                long content_len = strtol(content_len_str, &end, 10);
                if (*end == '\0' && content_len >= 0) {
                    conn_data->body_expected = (size_t)content_len;
                } else {
                    bad_request(conn_data);
                    return false;
                }
            }

            if (conn_data->body_expected <= 0) {
                conn_data->state = CONN_STATE_HANDLING_REQUEST;
                return true;
            }

            if (!init_body_reading(conn_data)) return false;
            check_if_body_received(conn_data);
            return true;
        }
        case HTTP_PARSE_ERR_BAD_REQUEST: {
            bad_request(conn_data);
            return false;
        }
        case HTTP_PARSE_ERR_PARSER_ERR: {
            internal_server_error(conn_data);
            return false;
        }
        default:
            return false; // Unreachable
    }
}

static bool init_body_reading(conn_data_t* conn_data) {
    conn_data->state = CONN_STATE_READING_BODY;

    char* body_start = conn_data->headers_buff + conn_data->curr_request_len;
    conn_data->body_received = conn_data->headers_buff_len - conn_data->curr_request_len;

    if (conn_data->body_received > conn_data->body_expected) {
        conn_data->curr_request_len += conn_data->body_expected;
        conn_data->body_received = conn_data->body_expected;
    } else {
        conn_data->curr_request_len += conn_data->body_received;
    }

    if (conn_data->body_expected > BODY_IN_FILE_THRESHOLD) {
        conn_data->body_buff = malloc(sizeof(char) * MAX_BODY_BUFF_SIZE);
        if (!conn_data->body_buff) {
            internal_server_error(conn_data);
            return false;
        }

        conn_data->body_file = tmpfile();
        if (!conn_data->body_file) {
            internal_server_error(conn_data);
            return false;
        }

        if (conn_data->body_received > 0) {
            size_t written = fwrite(body_start, 1, conn_data->body_received, conn_data->body_file);
            if (written != conn_data->body_received) {
                internal_server_error(conn_data);
                return false;
            }
        }
    } else {
        conn_data->body_buff = malloc(sizeof(char) * conn_data->body_expected + 1);
        if (!conn_data->body_buff) {
            internal_server_error(conn_data);
            return false;
        }

        if (conn_data->body_received > 0) {
            memmove(conn_data->body_buff, body_start, conn_data->body_received);
        }
    }

    return true;
}

static void check_if_body_received(conn_data_t* conn_data) {
    if (conn_data->body_received >= conn_data->body_expected) {
        conn_data->state = CONN_STATE_HANDLING_REQUEST;
        if (conn_data->body_file) {
            rewind(conn_data->body_file);
            conn_data->parsed_request.body_in_file = true;
            conn_data->parsed_request.body_file = conn_data->body_file;
        } else {
            conn_data->parsed_request.body = conn_data->body_buff;
        }
    }
}

static ssize_t read_data_from_socket(conn_data_t* conn_data, void* buff_addr, size_t n_bytes_to_read) {
    ssize_t n_bytes_read = read(conn_data->socket_fd, buff_addr, n_bytes_to_read);

    if (n_bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; // All available data read
        perror("[ERROR] read()");
        close_conn(conn_data);
        return -1;
    }

    if (n_bytes_read == 0) {
        printf("[INFO] Client %s on fd %d disconnected\n", conn_data->ip_addr, conn_data->socket_fd);
        close_conn(conn_data);
        return -1;
    }

    return n_bytes_read;
}

static void bad_request(conn_data_t* conn_data) {
    send_and_close_conn(conn_data, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
}

static void internal_server_error(conn_data_t* conn_data) {
    perror("[ERROR] Internal server error!");
    send_and_close_conn(conn_data, "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
}

// TODO Handle incomplete writes
static void send_and_close_conn(conn_data_t* conn_data, char* response) {
    write(conn_data->socket_fd, response, strlen(response));
    close_conn(conn_data);
}

static void close_conn(conn_data_t* conn_data) {
    printf("[INFO] Closing connection with %s (fd %d)\n", conn_data->ip_addr, conn_data->socket_fd);
    close(conn_data->socket_fd);
    if (conn_data->body_buff) {
        free(conn_data->body_buff);
        conn_data->body_buff = NULL;
    }
    if (conn_data->body_file) {
        fclose(conn_data->body_file);
        conn_data->body_file = NULL;
    }
    http_free_request(&conn_data->parsed_request);
    free(conn_data);
}

int main(void) {
    /* Register SIGINT handler */
    struct sigaction sa = {0};
    sa.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("[ERROR] sigcation()");
        return 1;
    }

    /* Create the epoll instance */
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("[ERROR] epoll_create1()");
        return 1;
    }

    int listen_socket_fd = create_listening_socket(epoll_fd);
    if (listen_socket_fd == -1) {
        fprintf(stderr, "[ERROR] Failed to create listetning socket\n");
        return 1;
    }

    printf("[INFO] Server is listening on port %d\n", PORT);

    /* Event loop */
    // TODO Close inactive connections!
    struct epoll_event events[MAX_EVENTS];
    while (running) {
        /* Get events from queue */
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n_events < 0) {
            if (errno == EINTR) continue;
            perror("[ERROR] epoll_wait()");
            break;
        }

        for (int i = 0; i < n_events; i++) {
            epoll_data_t event_data = events[i].data;
            if (event_data.fd == listen_socket_fd) {
                handle_new_conn_event(epoll_fd, listen_socket_fd);
            } else {
                handle_client_event(event_data.ptr);
            }
        }
    }

    printf("\n[INFO] Shutdown signal received. Closing listening sockets.\n");
    close(listen_socket_fd);
    close(epoll_fd);

    return 0;
}
