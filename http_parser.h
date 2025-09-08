#ifndef HTTP_PARSER_H_
#define HTTP_PARSER_H_

#include <stdlib.h>

typedef struct http_header_t {
    char* type;
    size_t type_len;
    char* value;
    size_t value_len;
} http_header_t;

typedef struct http_headers_t {
    http_header_t* data;
    size_t len;
    size_t capacity;
} http_headers_t;

typedef struct http_request_t {
    char* method;
    char* uri;
    char* version;
    http_headers_t* headers;
} http_request_t;

typedef enum {
    HTTP_PARSE_OK,
    HTTP_PARSE_ERR_WRONG_USAGE,
    HTTP_PARSE_ERR_MALLOC,
    HTTP_PARSE_ERR_MALFORMED_REQUEST,
    HTTP_PARSE_ERR_MALFORMED_HEADER,
    HTTP_PARSE_ERR_EMPTY_HEADER_NAME
} http_parse_err_t;

http_parse_err_t http_parse_request(char* request_str, http_request_t* request);
void http_free_request(http_request_t* http_request);
const char* http_get_header_value(const http_headers_t* http_headers, const char* type);
void http_print_request(http_request_t* request);

#endif // HTTP_PARSER_H_
