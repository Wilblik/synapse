#include <stdio.h>
#include <signal.h>
#include <stdbool.h>

#include "http_parser.h"
#include "http_server.h"

// TODO Test closing inactive connections
// TODO Test incomplete writes
// TODO Test sending large file in HTTP request
// TODO Test performance

#define PORT 8080
#define CONN_TIMEOUT 60

// TODO global http server instance
void sigint_handler(int sig, siginfo_t *info, void *ucontext) {
    (void)sig;
    (void)ucontext;

    http_server_t* http_server = (http_server_t*)info->si_value.sival_ptr;
    http_server_stop(http_server);
}

void on_request(http_conn_t* http_conn, http_request_t* http_req);
void on_bad_request(http_conn_t* http_conn);
void on_server_error(http_conn_t* http_conn);

int main(void) {
    struct sigaction sa = {0};
    sa.sa_sigaction = sigint_handler;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("[ERROR] sigcation()");
        return 1;
    }

    http_server_callbacks_t http_callbacks = {
        .on_request = on_request,
        .on_bad_request = on_bad_request,
        .on_server_error = on_server_error
    };

    http_server_t* http_server = http_server_create(PORT, http_callbacks, CONN_TIMEOUT);
    if (!http_server) return 1;

    http_server_run(http_server);
    http_server_destroy(http_server);

    return 0;
}

void on_request(http_conn_t* http_conn, http_request_t* http_req) {
    http_print_request(http_req);
    const char* res = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    http_server_send_response(http_conn, res);
}

void on_bad_request(http_conn_t* http_conn) {
    const char* res = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
    http_server_send_response(http_conn, res);
}

void on_server_error(http_conn_t* http_conn) {
    const char* res = "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n\r\n";
    http_server_send_response(http_conn, res);
}
