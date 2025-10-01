#include "tcp_server.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#define MAX_EVENTS 64
#define EPOLL_CHECK_INTERVAL_MS 5000
#define READ_BUFFER_SIZE 8192
#define INITIAL_WRITE_BUFFER_CAPACITY 4096

struct tcp_conn_t {
    tcp_server_t* server;
    int socket_fd;
    char ip_addr[INET_ADDRSTRLEN];
    void* data; /* Pointer to user defined data (e.g. http_conn_data_t) */
    bool is_closed;

    time_t last_activity;
    struct tcp_conn_t* next;
    struct tcp_conn_t* prev;

    char* out_buff;
    size_t out_buff_len;
    size_t out_buff_sent;
    size_t out_buff_capacity;
};

struct tcp_server_t {
    int listen_socket_fd;
    int epoll_fd;
    uint16_t port;

    tcp_server_callbacks_t callbacks;
    volatile sig_atomic_t running;

    int conn_timeout;
    tcp_conn_t* conn_list_head;
    tcp_conn_t* conn_list_tail;

    void* context;
};

static int create_listening_socket(int epoll_fd, uint16_t port);
static bool set_socket_nonblocking(int socket_fd);
static bool register_socket_with_epoll(int epoll_fd, int socket_fd, void* data);
static void handle_new_conn_event(tcp_server_t* server);
static void handle_read_event(tcp_conn_t* conn);
static void move_conn_to_tail(tcp_conn_t* conn);
static void handle_write_event(tcp_conn_t* conn);
static bool mod_epoll_for_writing(tcp_conn_t* conn, bool enable_writing);
static void close_inactive_connections(tcp_server_t* server);

tcp_server_t* tcp_server_create(uint16_t port, tcp_server_callbacks_t callbacks, int conn_timeout, void* context) {
    tcp_server_t* server = malloc(sizeof(tcp_server_t));
    if (!server) {
        perror("[ERROR] Failed to allocate memory for tcp_server_t");
        return NULL;
    }

    server->port = port;
    server->callbacks = callbacks;
    server->running = 1;
    server->conn_timeout = conn_timeout;
    server->conn_list_head = NULL;
    server->conn_list_tail = NULL;
    server->context = context;

    server->epoll_fd = epoll_create1(0);
    if (server->epoll_fd < 0) {
        perror("[ERROR] epoll_create1()");
        free(server);
        return NULL;
    }

    server->listen_socket_fd = create_listening_socket(server->epoll_fd, port);
    if (server->listen_socket_fd < 0) {
        fprintf(stderr, "[ERROR] Failed to create listetning socket\n");
        close(server->epoll_fd);
        free(server);
        return NULL;
    }

    return server;
}

void tcp_server_run(tcp_server_t* server) {
    if (!server) return;
    printf("[INFO] Server is listening on port %d\n", server->port);

    struct epoll_event events[MAX_EVENTS];
    int epoll_timeout = (server->conn_timeout > 0) ? EPOLL_CHECK_INTERVAL_MS : -1;

    while (server->running) {
        int n_events = epoll_wait(server->epoll_fd, events, MAX_EVENTS, epoll_timeout);
        if (n_events < 0) {
            if (errno == EINTR) continue;
            perror("[ERROR] epoll_wait()");
            break;
        }
        for (int i = 0; i < n_events; i++) {
            if (events[i].data.fd == server->listen_socket_fd) {
                handle_new_conn_event(server);
                continue;
            }
            tcp_conn_t* conn = events[i].data.ptr;
            if (events[i].events & EPOLLIN) handle_read_event(conn);
            if (events[i].events & EPOLLOUT) handle_write_event(conn);
            if (conn->is_closed) free(conn);
        }

        if (server->conn_timeout > 0) {
           close_inactive_connections(server);
        }
    }
}

void tcp_server_stop(tcp_server_t* server) {
    if (server) {
        server->running = 0;
        printf("[INFO] Stopping server\n");
    }
}

void tcp_server_destroy(tcp_server_t* server) {
    if (!server || server->running) return;

    printf("[INFO] Destroying server...\n");

    tcp_conn_t* conn = server->conn_list_head;
    while (conn != NULL) {
        tcp_conn_t* next = conn->next;
        tcp_server_close_conn(conn);
        conn = next;
    }

    close(server->listen_socket_fd);
    close(server->epoll_fd);
    free(server);
}

bool tcp_server_write(tcp_conn_t* conn, const char* data, size_t len) {
    if (!conn || !data || len == 0) return false;
    if (conn->out_buff_len == 0) {
        ssize_t n_bytes_written = write(conn->socket_fd, data, len);
        if (n_bytes_written < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("[ERROR] write()");
                tcp_server_close_conn(conn);
                return false;
            }
            n_bytes_written = 0;
        }

        if ((size_t)n_bytes_written == len) {
            move_conn_to_tail(conn);
            return true;
        }

        data += n_bytes_written;
        len -= n_bytes_written;
    }

    size_t needed = conn->out_buff_len + len;
    if (needed > conn->out_buff_capacity) {
        size_t new_cap = conn->out_buff_capacity > 0
            ? conn->out_buff_capacity * 2
            : INITIAL_WRITE_BUFFER_CAPACITY;
        if (new_cap < needed) new_cap = needed;

        char* new_buff = realloc(conn->out_buff, new_cap);
        if (!new_buff) {
            perror("[ERROR] realloc()");
            tcp_server_close_conn(conn);
            return false;
        }

        conn->out_buff = new_buff;
        conn->out_buff_capacity = new_cap;
    }

    memcpy(conn->out_buff + conn->out_buff_len, data, len);
    conn->out_buff_len += len;
    return mod_epoll_for_writing(conn, true);
}

void tcp_server_close_conn(tcp_conn_t* conn) {
    if (!conn || conn->is_closed) return;

    printf("[INFO] Closing connection with %s (fd %d)\n", conn->ip_addr, conn->socket_fd);
    if (epoll_ctl(conn->server->epoll_fd, EPOLL_CTL_DEL, conn->socket_fd, NULL) < 0) {
        perror("[WARN] epoll_ctl(DEL)");
    }

    close(conn->socket_fd);
    conn->is_closed = true;

    if (conn->prev) conn->prev->next = conn->next;
    else conn->server->conn_list_head = conn->next;

    if (conn->next) conn->next->prev = conn->prev;
    else conn->server->conn_list_tail = conn->prev;

    if (conn->out_buff) free(conn->out_buff);

    if (conn->server->callbacks.on_close) {
        conn->server->callbacks.on_close(conn, conn->data, conn->server->context);
    }
}

const char* tcp_server_conn_ip(const tcp_conn_t* conn) {
    if (!conn) return NULL;
    return conn->ip_addr;
}

static int create_listening_socket(int epoll_fd, uint16_t port) {
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

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERROR] bind()");
        return -1;
    }

    if (!register_socket_with_epoll(epoll_fd, listen_socket_fd, NULL)) {
        fprintf(stderr, "[ERROR] Failed to register listening socket with epoll\n");
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

static bool set_socket_nonblocking(int socket_fd) {
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

static bool register_socket_with_epoll(int epoll_fd, int socket_fd, void* data) {
    struct epoll_event event = { .events = EPOLLIN | EPOLLET };
    if (data) event.data.ptr = data;
    else event.data.fd = socket_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event) < 0) {
        perror("[ERROR] epoll_ctl(ADD)");
        return false;
    }

    return true;
}

static void handle_new_conn_event(tcp_server_t* server) {
    while (true) {
        struct sockaddr_in client_addr = {0};
        socklen_t client_len = sizeof(client_addr);

        int conn_socket_fd = accept(server->listen_socket_fd, (struct sockaddr*)&client_addr, &client_len);
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

        tcp_conn_t* conn = calloc(1, sizeof(tcp_conn_t));
        if (!conn) {
            fprintf(stderr, "[ERROR] Failed to allocate memory for new client connection data\n");
            close(conn_socket_fd);
            continue;
        }

        conn->server = server;
        conn->socket_fd = conn_socket_fd;
        conn->last_activity = time(NULL);
        inet_ntop(AF_INET, &client_addr.sin_addr, conn->ip_addr, sizeof(conn->ip_addr));

        if (server->conn_list_tail == NULL) {
            server->conn_list_head = conn;
            server->conn_list_tail = conn;
        } else {
            server->conn_list_tail->next = conn;
            conn->prev = server->conn_list_tail;
            server->conn_list_tail = conn;
        }

        if (server->callbacks.on_connect) {
            conn->data = server->callbacks.on_connect(conn, server->context);
        }

        if (!register_socket_with_epoll(server->epoll_fd, conn->socket_fd, conn)) {
            fprintf(stderr, "[ERROR] Failed to register connection socket with epoll\n");
            tcp_server_close_conn(conn);
            continue;
        }

        printf("[INFO] Accepted connection from %s (fd %d)\n", conn->ip_addr, conn->socket_fd);
    }
}

static void handle_read_event(tcp_conn_t* conn) {
    move_conn_to_tail(conn);

    char buff[READ_BUFFER_SIZE];
    while (!conn->is_closed) {
        ssize_t n_bytes_read = read(conn->socket_fd, buff, sizeof(buff));

        if (n_bytes_read < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("[ERROR] read()");
                tcp_server_close_conn(conn);
            }
            break;
        }

        if (n_bytes_read == 0) {
            printf("[INFO] Client %s on fd %d disconnected\n", conn->ip_addr, conn->socket_fd);
            tcp_server_close_conn(conn);
            break;
        }

        if (conn->server->callbacks.on_data) {
            conn->server->callbacks.on_data(conn, conn->data, conn->server->context, buff, n_bytes_read);
        }
    }
}

static void move_conn_to_tail(tcp_conn_t* conn) {
    conn->last_activity = time(NULL);
    if (conn->server->conn_list_tail == conn) return;
    if (conn->prev) conn->prev->next = conn->next;
    else conn->server->conn_list_head = conn->next;
    conn->next->prev = conn->prev;
    conn->prev = conn->server->conn_list_tail;
    conn->next = NULL;
    conn->server->conn_list_tail->next = conn;
    conn->server->conn_list_tail = conn;
}


static void handle_write_event(tcp_conn_t* conn) {
    assert(conn->out_buff_len != 0);
    assert(conn->out_buff_len > conn->out_buff_sent);

    if (conn->is_closed) return;

    size_t to_send = conn->out_buff_len - conn->out_buff_sent;
    ssize_t n_bytes_written = write(conn->socket_fd, conn->out_buff + conn->out_buff_sent, to_send);

    if (n_bytes_written > 0) {
        move_conn_to_tail(conn);
        conn->out_buff_sent += n_bytes_written;
    } else if (n_bytes_written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("[ERROR] write()");
        tcp_server_close_conn(conn);
        return;
    }

    if (conn->out_buff_len == conn->out_buff_sent) {
        conn->out_buff_len = 0;
        conn->out_buff_sent = 0;
        if (!mod_epoll_for_writing(conn, false)) {
            tcp_server_close_conn(conn);
        }
    }
}

static bool mod_epoll_for_writing(tcp_conn_t* conn, bool enable_writing) {
    struct epoll_event event = {
        .data.ptr = conn,
        .events = EPOLLIN | EPOLLET | (enable_writing ? EPOLLOUT : 0)
    };

    if (epoll_ctl(conn->server->epoll_fd, EPOLL_CTL_MOD, conn->socket_fd, &event) < 0) {
        perror("[ERROR] epoll_ctl(MOD)");
        tcp_server_close_conn(conn);
        return false;
    }

    return true;
}

static void close_inactive_connections(tcp_server_t* server) {
    time_t now = time(NULL);
    tcp_conn_t* conn = server->conn_list_head;
    while (conn != NULL) {
        if ((now - conn->last_activity) < server->conn_timeout) {
            break;
        }
        printf("[INFO] Closing inactive connection\n");
        tcp_conn_t* to_close = conn;
        conn = conn->next;
        tcp_server_close_conn(to_close);
    }
}
