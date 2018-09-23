/*
 * async-curl.h
 * This file is part of focal, a calendar application for Linux
 * Copyright 2018 Oliver Giles and focal contributors.
 *
 * Focal is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Focal is distributed without any explicit or implied warranty.
 * You should have received a copy of the GNU General Public License
 * version 3 with focal. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef ASYNC_CURL_H
#define ASYNC_CURL_H

#include <curl/curl.h>

typedef void (*AsyncCurlCallback)(CURL* handle, CURLcode ret, void* user);

// Call once at start of application. Configures libcurl-multi.
void async_curl_init();

// Helper method to fill a GString with a CURL handle's HTTP response body.
// Usage:
//   GString* str = g_string_new(NULL);
//   curl_easy_setopt(curl, CURLOPT_WRITEDATA, str);
//   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);
// Remember to free the GString afterwards.
size_t curl_write_to_gstring(char* ptr, size_t size, size_t nmemb, void* userdata);

// Adds a CURL request to be performed asynchronously. The CURL* handle
// and the headers list will be freed automatically when the request finishes
// (ownership transferred). The headers list may be NULL. The callback will
// be invoked when the request completes.
void async_curl_add_request(CURL* handle, struct curl_slist* headers, AsyncCurlCallback cb, void* user);

// Call once before application exit. Cleans up libcurl multi.
void async_curl_cleanup();

#endif //ASYNC_CURL_H
