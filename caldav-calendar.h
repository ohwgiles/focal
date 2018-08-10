/*
 * caldav-calendar.h
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
#ifndef CALDAV_CALENDAR_H
#define CALDAV_CALENDAR_H

#include "calendar.h"

#define CALDAV_CALENDAR_TYPE (caldav_calendar_get_type())
G_DECLARE_FINAL_TYPE(CaldavCalendar, caldav_calendar, FOCAL, CALDAV_CALENDAR, Calendar)

Calendar* caldav_calendar_new(const CalendarConfig* cfg);

void caldav_calendar_sync(CaldavCalendar* cal);

void caldav_calendar_free(CaldavCalendar* cal);

#endif // CALDAV_CALENDAR_H
