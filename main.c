#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>

#define PORT 8080
#define MAX_EVENTS 64 // Max events to handle in a single epoll_wait call
#define BUFFER_SIZE 1024 // Size of buffer for incoming client data

volatile sig_atomic_t running = 1;
void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

bool set_socket_nonblocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl() F_GETFL");
        return false;
    }
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl() F_SETFL");
        return false;
    }
    return true;
}

bool register_socket_with_epoll(int epoll_fd, int socket_fd) {
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET; // Watch for incoming data (EPOLLIN) in Edge-Triggered mode (EPOLLET)
    event.data.fd = socket_fd;
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

    if (!register_socket_with_epoll(epoll_fd, listen_socket_fd)) {
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

        if (!register_socket_with_epoll(epoll_fd, conn_socket_fd)) {
            fprintf(stderr, "[ERROR] Failed to register connection socket with epoll");
            close(conn_socket_fd);
            continue;
        }

        char ip_addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_addr_str, sizeof(ip_addr_str));
        printf("[INFO] Accepted connection from %s on fd %d\n", ip_addr_str, conn_socket_fd);
    }
}

void handle_data_read_event(int conn_socket_fd) {
    char buffer[BUFFER_SIZE];
    while (true) {
        ssize_t n_bytes = read(conn_socket_fd, buffer, sizeof(buffer));
        if (n_bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // All available data read
            perror("[ERROR] read()");
            close(conn_socket_fd);
            break;
        } else if (n_bytes == 0) {
            printf("[INFO] Client on fd %d disconnected.\n", conn_socket_fd);
            close(conn_socket_fd);
            break;
        } else {
            write(conn_socket_fd, buffer, n_bytes);
        }
    }
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
            if (events[i].data.fd == listen_socket_fd) {
                handle_new_conn_event(epoll_fd, listen_socket_fd);
            } else {
                handle_data_read_event(events[i].data.fd);
            }
        }
    }

    printf("\n[INFO] Shutdown signal received. Closing listening sockets.\n");
    close(listen_socket_fd);
    close(epoll_fd);

    return 0;
}
