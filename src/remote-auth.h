/*
 * remote-auth.h
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
#ifndef REMOTE_AUTH_H
#define REMOTE_AUTH_H

#include <curl/curl.h>
#include <gtk/gtk.h>

typedef struct _RemoteAuth RemoteAuth;

typedef struct _Calendar Calendar;

typedef struct _CalendarConfig CalendarConfig;

// A RemoteAuth object is responsible for authenticating against a remote
// server. The user of this API receives an authenticated CURL handle with
// which content operations (CalDAV or other API).
// The user pointer here is supplied as the first argument to all callbacks.
RemoteAuth* remote_auth_new(CalendarConfig* cfg, gpointer user);

// Sets a callback to be invoked when the user was prompted for credentials
// but declined to provide them. The user of this class can use this to
// reset its internal state or propagate the error onwards.
void remote_auth_set_declined_callback(RemoteAuth* ba, void (*callback)(gpointer));

// The primary method, this function will invoke the provided callback with
// and authenticated CURL object. According to the CalendarConfig provided
// in remote_auth_new, this could be basic authentication or OAuth2. This
// function will take care of querying the keychain via libsecret, prompting
// the user if no credentials are available, storing the latest credentials
// or tokens back in the keychain, and requesting a new OAuth2 access token
// from the refresh token if necessary.
void remote_auth_new_request(RemoteAuth* ba, void (*callback)(), void* arg);

// This method should be called if the provided CURL handle results in a
// 401 Unauthorized response. It will invalidate the credential stored in
// the keyring and trigger the process anew as in remote_auth_new_request.
void remote_auth_invalidate_credential(RemoteAuth* ba, void (*callback)(), void* arg);

// Frees all memory associated with the RemoteAuth object.
void remote_auth_free(RemoteAuth* ba);

#endif //REMOTE_AUTH_H
