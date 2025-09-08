#include "http_parser.h"

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h> // For strncasecmp

#define INITIAL_HEADERS_CAPACITY 8

static void trim_whitespace(char** str, size_t* len);
static void free_headers(http_headers_t* http_headers);
static http_parse_err_t parse_headers(char* headers_str, http_headers_t** headers);

http_parse_err_t http_parse_request(char* request_str, http_request_t* request) {
    if (!request) return HTTP_PARSE_ERR_WRONG_USAGE;

    char* request_line_start = request_str;
    char* request_line_end = strstr(request_str, "\r\n");
    if (!request_line_end) return HTTP_PARSE_ERR_MALFORMED_REQUEST;
    *request_line_end = '\0';

    char* first_space = strchr(request_line_start, ' ');
    if (!first_space) return HTTP_PARSE_ERR_MALFORMED_REQUEST;
    *first_space = '\0';

    char* second_space = strchr(first_space + 1, ' ');
    if (!second_space) return HTTP_PARSE_ERR_MALFORMED_REQUEST;
    *second_space = '\0';

    request->method = request_line_start;
    request->uri = first_space + 1;
    request->version = second_space + 1;

    char* headers_start = request_line_end + 2;
    http_parse_err_t headers_parsing_result = parse_headers(headers_start, &request->headers);
    return headers_parsing_result;
}

/*We only need to free the array and the main struct.
    The char* pointers for type/value point into the original buffer,
    which is managed by the caller.
*/
void http_free_request(http_request_t *http_request) {
    free_headers(http_request->headers);
    http_request->headers = NULL;
    http_request->method = NULL;
    http_request->uri = NULL;
    http_request->version = NULL;
}

const char* http_get_header_value(const http_headers_t* http_headers, const char* type) {
    if (!http_headers || !type) return NULL;

    for (size_t i = 0; i < http_headers->len; ++i) {
        /* Use strncasecmp for case-insensitive comparison, which is required by HTTP spec. */
        bool prefix_found = strncasecmp(http_headers->data[i].type, type, http_headers->data[i].type_len) == 0;
        bool strs_are_same_len = type[http_headers->data[i].type_len] == '\0';
        if (prefix_found && strs_are_same_len) {
            return http_headers->data[i].value;
        }
    }

    return NULL;
}

void http_print_request(http_request_t* request) {
    printf("%s %s %s\n", request->method, request->uri, request->version);
    for (size_t i = 0; i < request->headers->len; ++i) {
        printf("%s:%s\n", request->headers->data[i].type, request->headers->data[i].value);
    }
}

static http_parse_err_t parse_headers(char* headers_str, http_headers_t** headers_out) {
    if (!headers_str) return HTTP_PARSE_ERR_WRONG_USAGE;

    http_headers_t* headers = malloc(sizeof(http_headers_t));
    if (!headers) return HTTP_PARSE_ERR_MALLOC;

    headers->len = 0;
    headers->capacity = INITIAL_HEADERS_CAPACITY;
    headers->data = malloc(sizeof(http_header_t) * headers->capacity);
    if (!headers->data) {
        free(headers);
        return HTTP_PARSE_ERR_MALLOC;
    }

    char* curr_line = headers_str;
    char* line_end;
    while ((line_end = strstr(curr_line, "\r\n")) != NULL) {
        if (curr_line == line_end) break;
        *line_end = '\0';

        char* colon = strchr(curr_line, ':');
        if (!colon) {
            free_headers(headers);
            return HTTP_PARSE_ERR_MALFORMED_HEADER;
        }

        http_header_t new_header = {
            .type = curr_line,
            .type_len = colon - curr_line,
            .value = colon + 1,
            .value_len = line_end - (colon + 1)
        };

        trim_whitespace(&new_header.type, &new_header.type_len);
        trim_whitespace(&new_header.value, &new_header.value_len);

        if (headers->len >= headers->capacity) {
            size_t new_capacity = headers->capacity * 2;
            http_header_t* new_headers_ptr = realloc(headers->data, sizeof(http_header_t) * new_capacity);
            if (!new_headers_ptr) {
                free_headers(headers);
                return HTTP_PARSE_ERR_MALLOC;
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

static void trim_whitespace(char** str, size_t* len) {
    if (!str || !*str || !len || *len == 0) return;
    while (*len > 0 && isspace(**str)) { (*str)++; (*len)--; }
    while (*len > 0 && isspace((*str)[*len - 1])) { (*len)--; }
    (*str)[*len] = '\0';
}

static void free_headers(http_headers_t* http_headers) {
    if (!http_headers) return;
    if (http_headers->data) free(http_headers->data);
    free(http_headers);
}
