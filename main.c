#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define QUEUE_SIZE 5 // Number of connections that can wait to be accepted
#define BUFFER_SIZE 1024

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

volatile sig_atomic_t running = 1;
void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

int main(void) {
    /* Register SIGINT handler */
    struct sigaction sa = {0};
    sa.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("[ERROR] sigcation()");
        return 1;
    }

    /* Create listening socket */
    int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0) {
        perror("[ERROR] socket()");
        return 1;
    }

    /* Bind the socket to our IP and port */
    sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERROR] bind()");
        return 1;
    }

    /* Listen for incoming connections */
    listen(listen_socket, QUEUE_SIZE);
    printf("[INFO] Server is listening on port %d\n", PORT);

    int conn_socket;
    sockaddr_in client_addr = {0};
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    while (running) {
        /* Accept incoming connection */
        conn_socket = accept(listen_socket, (sockaddr*)&client_addr, &client_len);
        if (conn_socket < 0) {
            perror("[ERROR] accept()");
            continue;
        }

        char* client_ip = inet_ntoa(client_addr.sin_addr);
        printf("[INFO] Accepted connection from: %s\n", client_ip);

        /* Read data from client and echo back */
        ssize_t num_bytes;
        while (true) {
            num_bytes = read(conn_socket, buffer, BUFFER_SIZE);
            if (num_bytes > 0) {
                printf("[INFO] Received %zd bytes from %s. Echoing back.\n", num_bytes, client_ip);
                write(conn_socket, buffer, num_bytes);
                continue;
            }
            break;
        }

        printf("[INFO] Client %s disconnected. Closing socket\n", client_ip);
        close(conn_socket);
    }

    printf("\n[INFO] Shutdown signal received. Closing listening socket.\n");
    close(listen_socket);
    return 0;
}
