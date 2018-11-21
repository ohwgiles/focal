/*
 * ics-calendar.h
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
#ifndef ICS_CALENDAR_H
#define ICS_CALENDAR_H

#include "calendar.h"

#define ICS_CALENDAR_TYPE (ics_calendar_get_type())
G_DECLARE_FINAL_TYPE(IcsCalendar, ics_calendar, FOCAL, ICS_CALENDAR, Calendar)

Calendar* ics_calendar_new(const char* path);

#endif // ICS_CALENDAR_H
