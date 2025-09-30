#ifndef HTTP_PARSER_H_
#define HTTP_PARSER_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef enum {
    HTTP_METHOD_UNKNOWN,
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_PATCH,
    HTTP_METHOD_TRACE,
    HTTP_METHOD_CONNECT
} http_method_t;

typedef struct http_header_t {
    char* type;
    char* value;
} http_header_t;

typedef struct http_headers_t {
    http_header_t* data;
    size_t len;
    size_t capacity;
} http_headers_t;

typedef struct http_request_t {
    http_method_t method;
    char* uri;
    char* version;
    http_headers_t* headers;
    bool body_in_file;
    union {
        char* body;
        FILE* body_file;
    };
} http_request_t;

typedef enum {
    HTTP_PARSE_OK,
    HTTP_PARSE_ERR_PARSER_ERR,
    HTTP_PARSE_ERR_BAD_REQUEST,
} http_parse_err_t;

http_parse_err_t http_parse_request(char* request_str, http_request_t* request);
void http_free_request(http_request_t* http_request);
const char* http_get_header_value(const http_headers_t* http_headers, const char* type);
void http_print_request(http_request_t* http_request);

#endif // HTTP_PARSER_H_
