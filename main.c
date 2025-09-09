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
#define REQUEST_BUFF_SIZE 4096 // Size of buffer for incoming HTTP request

typedef struct conn_data_t {
    int socket_fd;
    char ip_addr[INET_ADDRSTRLEN];
    char request_buff[REQUEST_BUFF_SIZE];
    size_t request_buff_len;
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
        conn_data->socket_fd = conn_socket_fd;
        conn_data->request_buff_len = 0;
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


static void close_conn(conn_data_t* conn_data);

void handle_client_event(conn_data_t* conn_data) {
    while (true) {
        char* buff_addr = conn_data->request_buff + conn_data->request_buff_len;
        size_t n_bytes_to_read = REQUEST_BUFF_SIZE - conn_data->request_buff_len - 1;
        ssize_t n_bytes_read = read(conn_data->socket_fd, buff_addr, n_bytes_to_read);

        if (n_bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return; // All available data read
            perror("[ERROR] read()");
            close_conn(conn_data);
            return;
        }

        if (n_bytes_read == 0) {
            printf("[INFO] Client %s on fd %d disconnected\n", conn_data->ip_addr, conn_data->socket_fd);
            close_conn(conn_data);
            return;
        }

        conn_data->request_buff_len += n_bytes_read;
        if (conn_data->request_buff_len >= REQUEST_BUFF_SIZE - 1) {
            fprintf(stderr, "[ERROR] Request too large from fd %d\n", conn_data->socket_fd);
            close_conn(conn_data);
            return;
        }

        /* Check if we have received the end of the HTTP headers */
        conn_data->request_buff[conn_data->request_buff_len] = '\0';
        char* headers_end = strstr(conn_data->request_buff, "\r\n\r\n");
        if (!headers_end) continue;

        printf("[INFO] Request received from %s (fd %d):\n", conn_data->ip_addr, conn_data->socket_fd);
        printf("%s\n", conn_data->request_buff);
        http_request_t request = {0};
        http_parse_err_t parse_err = http_parse_request(conn_data->request_buff, &request);

        switch (parse_err) {
            case HTTP_PARSE_OK: {
                char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
                write(conn_data->socket_fd, response, strlen(response));

                const char* conn_header = http_get_header_value(request.headers, "Connection");
                bool keep_alive = !(conn_header && strcasecmp(conn_header, "close") == 0);
                if (!keep_alive) {
                    close_conn(conn_data);
                    http_free_request(&request);
                    return;
                }

                size_t data_left = conn_data->request_buff_len - (headers_end + 4 - conn_data->request_buff);
                if (data_left > 0) {
                    memmove(conn_data->request_buff, headers_end + 4, data_left);
                }
                conn_data->request_buff_len = data_left;
                break;
            }
            case HTTP_PARSE_ERR_BAD_REQUEST: {
                char* response = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                write(conn_data->socket_fd, response, strlen(response));
                close_conn(conn_data);
                http_free_request(&request);
                return;
            }
            case HTTP_PARSE_ERR_PARSER_ERR: {
                perror("[ERROR] Internal server error!");
                char* response = "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                write(conn_data->socket_fd, response, strlen(response));
                close_conn(conn_data);
                http_free_request(&request);
                return;
            }
        }
    }
}

static void close_conn(conn_data_t* conn_data) {
    printf("[INFO] Closing connection with %s (fd %d)\n", conn_data->ip_addr, conn_data->socket_fd);
    close(conn_data->socket_fd);
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
