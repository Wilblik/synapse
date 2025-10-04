/**
 * @file http_parser.h
 * @brief Defines structures and functions for parsing HTTP requests.
 */

#ifndef HTTP_PARSER_H_
#define HTTP_PARSER_H_

#include <stdio.h>
#include <stdbool.h>

/**
 * @brief Enumeration of supported HTTP methods.
 */
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

/**
 * @brief Represents a single HTTP header as a key-value pair.
 */
typedef struct http_header_t {
    char* type;  /**< The header name (e.g., "Content-Type"). */
    char* value; /**< The header value. */
} http_header_t;

/**
 * @brief Represents a collection of HTTP headers.
 */
typedef struct http_headers_t {
    http_header_t* data; /**< A dynamic array of headers. */
    size_t len;          /**< The number of headers in the array. */
    size_t capacity;     /**< The allocated capacity of the array. */
} http_headers_t;

/**
 * @brief Represents a parsed HTTP request.
 */
typedef struct http_request_t {
    http_method_t method; /**< The HTTP method used. */
    char* uri;            /**< The request URI. */
    char* version;        /**< The HTTP protocol version. */
    http_headers_t* headers; /**< A collection of headers. */
    bool body_in_file;    /**< Flag indicating if the body is stored in a file. */
    union {
        char* body;         /**< Pointer to the request body in memory. */
        FILE* body_file;    /**< File pointer to the request body on disk. */
    };
} http_request_t;

/**
 * @brief Enumeration for HTTP parsing error codes.
 */
typedef enum {
    HTTP_PARSE_OK,               /**< Parsing was successful. */
    HTTP_PARSE_ERR_PARSER_ERR,   /**< An internal parser error occurred (e.g., malloc failure). */
    HTTP_PARSE_ERR_BAD_REQUEST,  /**< The request string was malformed. */
} http_parse_err_t;

/**
 * @brief Parses an HTTP request string into an http_request_t structure.
 *
 * This function performs an in-place modification of the request_str by inserting null terminators.
 * Pointers in the resulting http_request_t structure will point to locations within request_str.
 *
 * @param request_str The raw HTTP request string to parse.
 * @param request The http_request_t structure to populate.
 * @return An http_parse_err_t indicating the outcome of the parsing operation.
 */
http_parse_err_t http_parse_request(char* request_str, http_request_t* request);

/**
 * @brief Frees memory allocated for an http_request_t structure.
 *
 * Specifically, this frees the headers structure and the array of header data.
 * It does not free the request string itself, which is managed by the caller.
 *
 * @param http_request Pointer to the http_request_t to be freed.
 */
void http_free_request(http_request_t* http_request);

/**
 * @brief Retrieves the value of a specific header from a list of headers.
 * @param http_headers Pointer to the http_headers_t structure.
 * @param type The name of the header to find (case-insensitive).
 * @return The value of the header if found, otherwise NULL.
 */
const char* http_get_header_value(const http_headers_t* http_headers, const char* type);

/**
 * @brief Prints the contents of an HTTP request for debugging purposes.
 * @param http_request Pointer to the http_request_t structure to print.
 */
void http_print_request(http_request_t* http_request);

#endif // HTTP_PARSER_H_
