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

typedef enum {
    HTTP_PARSE_OK,
    HTTP_PARSE_ERR_MALLOC,
    HTTP_PARSE_ERR_MALFORMED_HEADER,
    HTTP_PARSE_ERR_EMPTY_HEADER_NAME
} http_parse_err_t;

http_headers_t* parse_http_headers(char* headers_str, http_parse_err_t* err_code);
void free_http_headers(http_headers_t* http_headers);
const char* get_header_value(const http_headers_t* http_headers, const char* type);

#endif // HTTP_PARSER_H_
