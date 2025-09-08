#include "http_parser.h"

#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h> // For strncasecmp

#define INITIAL_HEADERS_CAPACITY 8

static void trim_whitespace(char** str, size_t* len) {
    if (!str || !*str || !len || *len == 0) return;
    while (*len > 0 && isspace(**str)) { (*str)++; (*len)--; }
    while (*len > 0 && isspace((*str)[*len - 1])) { (*len)--; }
    (*str)[*len] = '\0';
}

static void set_err(http_parse_err_t* err, http_parse_err_t value) {
    if (err) *err = value;
}

http_headers_t* parse_http_headers(char* headers_str, http_parse_err_t* err_code) {
    set_err(err_code, HTTP_PARSE_OK);
    if (!headers_str) return NULL;

    http_headers_t* headers = malloc(sizeof(http_headers_t));
    if (!headers) {
        perror("[ERROR] malloc() for http_headers_t");
        set_err(err_code, HTTP_PARSE_ERR_MALLOC);
        return NULL;
    }

    headers->len = 0;
    headers->capacity = INITIAL_HEADERS_CAPACITY;
    headers->data = malloc(sizeof(http_header_t) * headers->capacity);
    if (!headers->data) {
        perror("[ERROR] malloc() for headers array");
        set_err(err_code, HTTP_PARSE_ERR_MALLOC);
        free(headers);
        return NULL;
    }

    char* curr_line = headers_str;
    char* line_end;
    while ((line_end = strstr(curr_line, "\r\n")) != NULL) {
        if (curr_line == line_end) break;
        *line_end = '\0';

        char* colon = strchr(curr_line, ':');
        if (!colon) {
            fprintf(stderr, "[ERROR] Malformed header line (no colon): \"%s\"\n", curr_line);
            set_err(err_code, HTTP_PARSE_ERR_MALFORMED_HEADER);
            free_http_headers(headers);
            return NULL;
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
                perror("[ERROR] realloc() for headers array");
                set_err(err_code, HTTP_PARSE_ERR_MALLOC);
                free_http_headers(headers);
                return NULL;
            }
            headers->data = new_headers_ptr;
            headers->capacity = new_capacity;
        }
        headers->data[headers->len++] = new_header;

        /* Move past the original "\r\n" */
        curr_line = line_end + 2;
    }

    return headers;
}

/*We only need to free the array and the main struct.
    The char* pointers for type/value point into the original buffer,
    which is managed by the caller.
*/
void free_http_headers(http_headers_t* http_headers) {
    if (!http_headers) return;
    if (http_headers->data) free(http_headers->data);
    free(http_headers);
}

const char* get_header_value(const http_headers_t* http_headers, const char* type) {
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
