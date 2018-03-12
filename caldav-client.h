/*
 * caldav-client.h
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
#ifndef CALDAV_CLIENT_H
#define CALDAV_CLIENT_H

#include <glib.h>
#include <libical/ical.h>

struct _CaldavClient;
typedef struct _CaldavClient CaldavClient;

CaldavClient* caldav_client_new(const char* url, const char* user, const char* pass, gboolean verify_cert);

gboolean caldav_client_init(CaldavClient*);

gboolean caldav_client_put(CaldavClient*, icalcomponent* event, const char* url);

gboolean caldav_client_delete(CaldavClient*, icalcomponent* event, const char* url);

GSList* caldav_client_sync(CaldavClient*);

void caldav_client_free(CaldavClient*);

#endif // CALDAV_CLIENT_H
