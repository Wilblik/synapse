#include "http_server.h"
#include "http_router.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_PORT 8080
#define DEFAULT_CONN_TIMEOUT 60

typedef struct args_t {
    uint16_t port;
    int conn_timeout;
    bool browse_enabled;
    const char* path;
} args_t;

static volatile http_server_t* g_http_server = NULL;

void sigint_handler(int sig) {
    (void)sig;
    printf("\n[INFO] SIGINT received\n");
    if (g_http_server) {
        http_server_stop((http_server_t*)g_http_server);
    }
}

static args_t parse_args(int argc, char** argv);
static void print_usage(char* program_name);
static bool try_parse_int(char* str_num, long* out_num);

int main(int argc, char** argv) {
    args_t args = parse_args(argc, argv);

    struct sigaction sa = {0};
    sa.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("[ERROR] sigaction()");
        return 1;
    }

    http_server_callbacks_t http_callbacks;
    bool init_success = http_router_init(args.path, args.browse_enabled, &http_callbacks);
    if (!init_success) return 1;

    http_server_t* http_server = http_server_create(args.port, http_callbacks, args.conn_timeout);
    if (!http_server) return 1;
    g_http_server = http_server;

    http_server_run(http_server);
    http_server_destroy(http_server);

    return 0;
}

static args_t parse_args(int argc, char** argv) {
    args_t parsed_args = {0};
    parsed_args.port = DEFAULT_PORT;
    parsed_args.conn_timeout = DEFAULT_CONN_TIMEOUT;
    parsed_args.browse_enabled = true;
    parsed_args.path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "[ERROR] Missing value for port\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            long port = -1;
            if (!try_parse_int(argv[i], &port) || port < 0 || port > 65535) {
                fprintf(stderr, "[ERROR] Incorrect value for port\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            parsed_args.port = port;
        }
        else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--conn_timeout") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "[ERROR] Missing value for connection timeout\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            long timeout = -1;
            if (!try_parse_int(argv[i], &timeout) || timeout < 0) {
                fprintf(stderr, "[ERROR] Incorrect value for connection timeout\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            parsed_args.conn_timeout = timeout;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--no-browse") == 0) {
            parsed_args.browse_enabled = false;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "[ERROR] Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        } else if (parsed_args.path) {
            fprintf(stderr, "[ERROR] Multiple web root paths specified. Only one is allowed\n");
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        } else {
            parsed_args.path = argv[i];
        }
    }

    if (!parsed_args.path) parsed_args.path = "./";

    return parsed_args;
}

static void print_usage(char* program_name) {
    printf("Usage: %s [-p | --port <p>] [-t | --conn_timeout <t>] [-b | --no-browse] [-h | --help] <web_root_path>\n", program_name);
}

static bool try_parse_int(char* str_num, long* out_num) {
    char* end;
    long num = strtol(str_num, &end, 10);
    if (*end != '\0') {
        return false;
    }
    *out_num = num;
    return true;
}
