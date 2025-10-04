/**
 * @file http_router.h
 * @brief Defines the interface for the HTTP router component.
 *
 * The router is responsible for handling incoming HTTP requests and serving
 * static files or directory listings from a specified root directory.
 */

#ifndef HTTP_ROUTER_H_
#define HTTP_ROUTER_H_

#include "http_server.h"

/**
 * @brief Initializes the HTTP router and provides callbacks for the HTTP server.
 * @param web_root_path The root directory from which to serve files.
 * @param browse_enabled A flag to enable or disable directory browsing.
 * @param out_callbacks A pointer to a structure that will be filled with the router's callback functions.
 * @return A boolean indicating whether initialization was successful.
 */
bool http_router_init(const char* web_root_path, bool browse_enabled, http_server_callbacks_t* out_callbacks);

#endif // HTTP_ROUTER_H_
