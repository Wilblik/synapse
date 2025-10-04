#ifndef HTTP_ROUTER_H_
#define HTTP_ROUTER_H_

#include "http_server.h"

bool http_router_init(const char* web_root_path, bool browse_enabled, http_server_callbacks_t* out_callbacks);

#endif // HTTP_ROUTER_H_
