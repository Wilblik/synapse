/**
 * @file http_router.c
 * @brief Implements the routing logic that maps HTTP requests to filesystem resources.
 *
 * This file provides the core request handling callbacks for the HTTP server.
 * It is responsible for validating incoming requests, resolving the request URI
 * to a safe path within the web root directory, and serving the corresponding
 * resource. It can serve static files, generate directory listings if enabled,
 * or return appropriate error responses (e.g., 404 Not Found, 403 Forbidden).
 */

#include "http_router.h"
#include "http_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

static void on_request(http_conn_t* http_conn, http_request_t* http_req);
static void on_bad_request(http_conn_t* http_conn);
static void on_server_error(http_conn_t* http_conn);
static void handle_dir_request(http_conn_t* http_conn, const char* path, const char* uri);
static void handle_file_request(http_conn_t* http_conn, const char* path);
static void send_error_response(http_conn_t* http_conn, int status_code, const char* status_message);
static const char* get_mime_type(const char* path);

static char g_web_root_path[PATH_MAX];
static bool g_browse_enabled = true;

/**
 * @brief Returns the callbacks for the http_server
 *
 * @param web_root_path The root directory from which to serve files.
 * @param browse_enabled A flag to enable or disable directory browsing.
 * @param out_callback callbacks for the http_server.
 * @return boolean indicating wheter initialization was a success.
 */
bool http_router_init(const char* web_root_path, bool browse_enabled, http_server_callbacks_t* out_callbacks) {
    http_server_callbacks_t http_callbacks = {
        .on_request = on_request,
        .on_bad_request = on_bad_request,
        .on_server_error = on_server_error
    };

    *out_callbacks = http_callbacks;

    if (realpath(web_root_path, g_web_root_path) == NULL) {
        perror("[ERROR] Could not resolve web root path");
        return false;
    }

    g_browse_enabled = browse_enabled;

    return true;
}

/**
 * @brief Main request handler. It routes to requested files.
 *
 * @param http_conn The connection object.
 * @param http_req The parsed HTTP request object.
 */
static void on_request(http_conn_t* http_conn, http_request_t* http_req) {
    if (http_req->method != HTTP_METHOD_GET) {
        send_error_response(http_conn, 405, "Method Not Allowed");
        return;
    }

    /* Prevent directory traversal attacks like /../../etc/passwd */
    if (strstr(http_req->uri, "..")) {
        send_error_response(http_conn, 400, "Bad Request");
        return;
    }

    char requested_path[PATH_MAX];
    snprintf(requested_path, sizeof(requested_path), "%s%s", g_web_root_path, http_req->uri);

    /* Use realpath to resolve symbolic links and get the canonical path */
    char resolved_path[PATH_MAX];
    if (realpath(requested_path, resolved_path) == NULL) {
        send_error_response(http_conn, 404, "Not Found");
        return;
    }

    /* Ensure the web rooth path is within the web root directory */
    if (strncmp(resolved_path, g_web_root_path, strlen(g_web_root_path)) != 0) {
        send_error_response(http_conn, 403, "Forbidden");
        return;
    }

    struct stat path_stat;
    if (stat(resolved_path, &path_stat) != 0) {
        send_error_response(http_conn, 404, "Not Found");
        return;
    }

    if (S_ISDIR(path_stat.st_mode)) {
        handle_dir_request(http_conn, resolved_path, http_req->uri);
    } else if (S_ISREG(path_stat.st_mode)) {
        handle_file_request(http_conn, resolved_path);
    } else {
        /* Path is not a regular file or directory (e.g. a socket) */
        send_error_response(http_conn, 403, "Forbidden");
    }
}

/**
 * @brief Bad request handler. It sends error response.
 *
 * @param http_conn The connection object.
 */
static void on_bad_request(http_conn_t* http_conn) {
    send_error_response(http_conn, 400, "Bad Request");
}

/**
 * @brief Internal server error handler. It sends error response.
 *
 * @param http_conn The connection object.
 */
static void on_server_error(http_conn_t* http_conn) {
    send_error_response(http_conn, 500, "Internal Server Error");
}


/**
 * @brief Handles a request for a directory.
 *
 * If directory browsing is enabled, it generates and serves a directory listing.
 * If disabled, it looks for an 'index.html' file in the directory and serves it.
 *
 * @param http_conn The connection object.
 * @param path The absolute path to the directory.
 * @param uri The original request URI.
 */
static void handle_dir_request(http_conn_t* http_conn, const char* path, const char* uri) {
    if (!g_browse_enabled) {
        char index_path[PATH_MAX];
        snprintf(index_path, sizeof(index_path), "%s/index.html", path);

        struct stat st;
        if (stat(index_path, &st) == 0 && S_ISREG(st.st_mode)) {
            handle_file_request(http_conn, index_path);
        } else {
            send_error_response(http_conn, 403, "Forbidden");
        }
        return;
    }

    DIR* dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "[ERROR] Could not open requested dir '%s'\n", path);
        send_error_response(http_conn, 500, "Internal Server Error");
        return;
    }

    size_t body_cap = 4096;
    char* body = malloc(body_cap);
    if (!body) {
        fprintf(stderr, "[ERROR] Could not allocate memory for response body\n");
        send_error_response(http_conn, 500, "Internal Server Error");
        closedir(dir);
        return;
    }

    size_t body_len = snprintf(body, body_cap,
                               "<html><head><title>Index of %s</title></head>"
                               "<body><h1>Index of %s</h1><hr><ul>",
                               uri, uri);

    if (strcmp(uri, "/") != 0) {
        body_len += snprintf(body + body_len, body_cap - body_len,
                             "<li><a href=\"..\">..</a></li>");
    }

    struct dirent* dir_entry;
    while ((dir_entry = readdir(dir)) != NULL) {
        if (strcmp(dir_entry->d_name, ".") == 0 || strcmp(dir_entry->d_name, "..") == 0) continue;

        char entry_path[PATH_MAX];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", path, dir_entry->d_name);
        struct stat st;
        if (stat(entry_path, &st) != 0) continue;

        const char* name = dir_entry->d_name;
        const char* suffix = S_ISDIR(st.st_mode) ? "/" : "";

        size_t needed = strlen("<li><a href=\"\"></a></li>") + (2 * strlen(name)) + (2 * strlen(suffix)) + 10;
        if (body_len + needed >= body_cap) {
            body_cap *= 2;
            char* new_body = realloc(body, body_cap);
            if (!new_body) {
                fprintf(stderr, "[ERROR] Could not reallocate memory for response body\n");
                free(body);
                closedir(dir);
                send_error_response(http_conn, 500, "Internal Server Error");
                return;
            }
            body = new_body;
        }
        body_len += snprintf(body + body_len, body_cap - body_len,
                             "<li><a href=\"%s%s\">%s%s</a></li>", name, suffix, name, suffix);
    }
    closedir(dir);

    body_len += snprintf(body + body_len, body_cap - body_len, "</ul><hr></body></html>");

    char headers[512];
    int headers_len = snprintf(headers, sizeof(headers),
                               "HTTP/1.1 200 OK\r\n"
                               "Content-Type: text/html\r\n"
                               "Content-Length: %zu\r\n\r\n",
                               body_len);

    http_server_send_data(http_conn, headers, headers_len);
    http_server_send_data(http_conn, body, body_len);

    free(body);
}

/**
 * @brief Handles a request for a single file.
 *
 * @param http_conn The connection object.
 * @param path The absolute path to the file to be served.
 */
static void handle_file_request(http_conn_t* http_conn, const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        send_error_response(http_conn, 403, "Forbidden");
        return;
    }

    struct stat st;
    if (fstat(fileno(file), &st) != 0) {
        fprintf(stderr, "[ERROR] Could not read file stats for '%s'\n", path);
        send_error_response(http_conn, 500, "Internal Server Error");
        fclose(file);
        return;
    }

    long file_size = st.st_size;
    const char* mime_type = get_mime_type(path);

    char headers[512];
    int headers_len = snprintf(headers, sizeof(headers),
                               "HTTP/1.1 200 OK\r\n"
                               "Content-Type: %s\r\n"
                               "Content-Length: %ld\r\n\r\n",
                               mime_type, file_size);

    http_server_send_data(http_conn, headers, headers_len);

    char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (!http_server_send_data(http_conn, buffer, bytes_read)) {
            fprintf(stderr, "[ERROR] Failed to send file chunk\n");
            break;
        }
    }

    fclose(file);
}


/**
 * @brief Sends a generic error response.
 *
 * @param http_conn The connection object.
 * @param status_code The HTTP status code (e.g., 404).
 * @param status_message The HTTP status message (e.g., "Not Found").
 */
static void send_error_response(http_conn_t* http_conn, int status_code, const char* status_message) {
    char body[256];
    int body_len = snprintf(body, sizeof(body),
                            "<html><head><title>%d %s</title></head>"
                            "<body><h1>%d %s</h1></body></html>",
                            status_code, status_message, status_code, status_message);

    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n\r\n%s",
             status_code, status_message, body_len, body);

    if (http_server_send_data(http_conn, response, strlen(response))) {
        http_server_close_conn(http_conn);
    }
}

/**
 * @brief Determines the MIME type of a file based on its extension.
 *
 * @param path The path to the file.
 * @return A string representing the MIME type. Defaults to "application/octet-stream".
 */
static const char* get_mime_type(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot || dot == path) return "application/octet-stream";
    if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0) return "text/html";
    if (strcasecmp(dot, ".css") == 0) return "text/css";
    if (strcasecmp(dot, ".js") == 0) return "application/javascript";
    if (strcasecmp(dot, ".json") == 0) return "application/json";
    if (strcasecmp(dot, ".txt") == 0) return "text/plain";
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(dot, ".png") == 0) return "image/png";
    if (strcasecmp(dot, ".gif") == 0) return "image/gif";
    if (strcasecmp(dot, ".svg") == 0) return "image/svg+xml";
    if (strcasecmp(dot, ".ico") == 0) return "image/vnd.microsoft.icon";
    return "application/octet-stream";
}
