/*
 * remote-calendar.h
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
#ifndef CALENDAR_REMOTE_H
#define CALENDAR_REMOTE_H

#include "calendar.h"

#define REMOTE_CALENDAR_TYPE (remote_calendar_get_type())
G_DECLARE_FINAL_TYPE(RemoteCalendar, remote_calendar, FOCAL, REMOTE_CALENDAR, Calendar)

Calendar* remote_calendar_new(const char* url, const char* username, const char* password);

void remote_calendar_sync(RemoteCalendar* cal);

void remote_calendar_free(RemoteCalendar* cal);

#endif // CALENDAR_REMOTE_H
