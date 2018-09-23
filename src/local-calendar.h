/*
 * local-calendar.h
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
#ifndef LOCAL_CALENDAR_H
#define LOCAL_CALENDAR_H

#include "calendar.h"

#define LOCAL_CALENDAR_TYPE (local_calendar_get_type())
G_DECLARE_FINAL_TYPE(LocalCalendar, local_calendar, FOCAL, LOCAL_CALENDAR, Calendar)

Calendar* local_calendar_new(const char* path);

#endif // LOCAL_CALENDAR_H
