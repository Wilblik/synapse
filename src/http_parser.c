#include "http_parser.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_HEADERS_CAPACITY 8

static http_method_t parse_method(char* method);
static http_parse_err_t parse_headers(char* headers_str, http_headers_t** headers);
static void trim_whitespace(char** str);
static bool is_valid_uri(const char* uri);

http_parse_err_t http_parse_request(char* request_str, http_request_t* request) {
    if (!request || !request_str) return HTTP_PARSE_ERR_PARSER_ERR;

    char* request_line_start = request_str;
    char* request_line_end = strstr(request_str, "\r\n");
    if (!request_line_end) return HTTP_PARSE_ERR_BAD_REQUEST;
    *request_line_end = '\0';

    char* first_space = strchr(request_line_start, ' ');
    if (!first_space) return HTTP_PARSE_ERR_BAD_REQUEST;
    *first_space = '\0';

    char* second_space = strchr(first_space + 1, ' ');
    if (!second_space) return HTTP_PARSE_ERR_BAD_REQUEST;
    *second_space = '\0';

    http_method_t method = parse_method(request_line_start);
    if (method == HTTP_METHOD_UNKNOWN) return HTTP_PARSE_ERR_BAD_REQUEST;
    request->method = method;

    request->uri = first_space + 1;
    if (!is_valid_uri(request->uri))
        return HTTP_PARSE_ERR_BAD_REQUEST;

    request->version = second_space + 1;
    if (strcmp(request->version, "HTTP/1.1") != 0)
        return HTTP_PARSE_ERR_BAD_REQUEST;

    char* headers_start = request_line_end + 2;
    http_parse_err_t parse_err = parse_headers(headers_start, &request->headers);
    if (parse_err != HTTP_PARSE_OK) return parse_err;

    if (http_get_header_value(request->headers, "Host") == NULL)
        return HTTP_PARSE_ERR_BAD_REQUEST;

    request->body_in_file = false;
    request->body = NULL;
    request->body_file = NULL;

    return HTTP_PARSE_OK;
}

/* We only need to free the array and the main struct.
   Rest of the pointers point into the data managed by the caller.
*/
void http_free_request(http_request_t* http_request) {
    if (!http_request->headers) return;
    if (http_request->headers->data) free(http_request->headers->data);
    free(http_request->headers);
}

const char* http_get_header_value(const http_headers_t* http_headers, const char* type) {
    if (!http_headers || !type) return NULL;

    for (size_t i = 0; i < http_headers->len; ++i) {
        if (strcasecmp(http_headers->data[i].type, type) == 0) {
            return http_headers->data[i].value;
        }
    }

    return NULL;
}

void http_print_request(http_request_t* http_request) {
    if (!http_request) return;

    switch (http_request->method) {
        case HTTP_METHOD_GET:
            printf("GET");
            break;
        case HTTP_METHOD_POST:
            printf("POST");
            break;
        case HTTP_METHOD_PUT:
            printf("PUT");
            break;
        case HTTP_METHOD_DELETE:
            printf("DELETE");
            break;
        case HTTP_METHOD_HEAD:
            printf("HEAD");
            break;
        case HTTP_METHOD_OPTIONS:
            printf("OPTIONS");
            break;
        case HTTP_METHOD_PATCH:
            printf("PATCH");
            break;
        case HTTP_METHOD_TRACE:
            printf("TRACE");
            break;
        case HTTP_METHOD_CONNECT:
            printf("CONNECT");
            break;
        default:
            printf("METHOD UNPRINTABLE");
            break;
    }
    printf(" %s %s\n", http_request->uri, http_request->version);

    http_headers_t* headers = http_request->headers;
    for (size_t i = 0; i < headers->len; ++i) {
        http_header_t header = headers->data[i];
        printf("%s:%s\n", header.type, header.value);
    }

    char buffer[1024];
    if (http_request->body_in_file) {
        size_t bytes_read;
        printf("\n");
        while ((bytes_read = fread(buffer, sizeof(char), 1024, http_request->body_file)) > 0) {
            printf("%.*s", (int)bytes_read, buffer);
        }
        printf("\n");
    } else if (http_request->body) {
        printf("\n%s\n", http_request->body);
    }

    printf("\n");
}

static http_method_t parse_method(char* method_str) {
    if (strcmp(method_str, "GET")     == 0) return HTTP_METHOD_GET;
    if (strcmp(method_str, "POST")    == 0) return HTTP_METHOD_POST;
    if (strcmp(method_str, "PUT")     == 0) return HTTP_METHOD_PUT;
    if (strcmp(method_str, "DELETE")  == 0) return HTTP_METHOD_DELETE;
    if (strcmp(method_str, "HEAD")    == 0) return HTTP_METHOD_HEAD;
    if (strcmp(method_str, "OPTIONS") == 0) return HTTP_METHOD_OPTIONS;
    if (strcmp(method_str, "PATCH")   == 0) return HTTP_METHOD_PATCH;
    if (strcmp(method_str, "TRACE")   == 0) return HTTP_METHOD_TRACE;
    if (strcmp(method_str, "CONNECT") == 0) return HTTP_METHOD_CONNECT;

    return HTTP_METHOD_UNKNOWN;
}

static http_parse_err_t parse_headers(char* headers_str, http_headers_t** headers_out) {
    if (!headers_str) return HTTP_PARSE_ERR_PARSER_ERR;

    http_headers_t* headers = malloc(sizeof(http_headers_t));
    if (!headers) return HTTP_PARSE_ERR_PARSER_ERR;

    headers->len = 0;
    headers->capacity = INITIAL_HEADERS_CAPACITY;
    headers->data = malloc(sizeof(http_header_t) * headers->capacity);
    if (!headers->data) {
        free(headers);
        return HTTP_PARSE_ERR_PARSER_ERR;
    }

    char* curr_line = headers_str;
    char* line_end;
    while ((line_end = strstr(curr_line, "\r\n")) != NULL) {
        if (curr_line == line_end) break;
        *line_end = '\0';

        char* colon = strchr(curr_line, ':');
        if (!colon) {
            free(headers->data);
            free(headers);
            return HTTP_PARSE_ERR_BAD_REQUEST;
        }
        *colon = '\0';

        http_header_t new_header = {
            .type = curr_line,
            .value = colon + 1,
        };

        trim_whitespace(&new_header.type);
        trim_whitespace(&new_header.value);

        if (headers->len >= headers->capacity) {
            size_t new_capacity = headers->capacity * 2;
            http_header_t* new_headers_ptr = realloc(headers->data, sizeof(http_header_t) * new_capacity);
            if (!new_headers_ptr) {
                free(headers->data);
                free(headers);
                return HTTP_PARSE_ERR_PARSER_ERR;
            }
            headers->data = new_headers_ptr;
            headers->capacity = new_capacity;
        }
        headers->data[headers->len++] = new_header;

        /* Move past the original "\r\n" */
        curr_line = line_end + 2;
    }

    *headers_out = headers;
    return HTTP_PARSE_OK;
}

static void trim_whitespace(char** str) {
    if (!str || !*str || **str == '\0') return;

    /* Trim start */
    while (isspace(**str)) (*str)++;

    /* Find end */
    char* end = *str;
    while (*end != '\0') end++;

    /* Trim end */
    while (end > *str && isspace(*(end - 1))) end--;
    *end = '\0';
}

static bool is_unreserved(char c) {
    return isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

static bool is_valid_uri(const char* uri) {
    if (!uri || uri[0] != '/') return false;

    for (size_t i = 0; i < strlen(uri); ++i) {
        char c = uri[i];

        if (is_unreserved(c)) continue;
        if (c == '%') {
            if (i + 2 < strlen(uri) && isxdigit(uri[i+1]) && isxdigit(uri[i+2])) {
                i += 2;
                continue;
            }
            return false; // Malformed percent-encoding
        }

        switch (c) {
            case '/':
            case ':': case '@':
            case '!': case '$': case '&':
            case '+': case ',': case ';': case '=':
            case '(': case ')': case '*': case '\'':
                continue;
            default:
                return false;
        }
    }

    return true;
}
