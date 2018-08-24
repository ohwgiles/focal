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

void async_curl_init();

void async_curl_add_request(CURL* handle, AsyncCurlCallback cb, void* user);

void async_curl_cleanup();

#endif //ASYNC_CURL_H
