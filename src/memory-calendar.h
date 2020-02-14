/*
 * memory-calendar.h
 * This file is part of focal, a calendar application for Linux
 * Copyright 2019 Oliver Giles and focal contributors.
 *
 * Focal is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Focal is distributed without any explicit or implied warranty.
 * You should have received a copy of the GNU General Public License
 * version 3 with focal. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef MEMORY_CALENDAR_H
#define MEMORY_CALENDAR_H

#include "calendar.h"

#define MEMORY_CALENDAR_TYPE (memory_calendar_get_type())
G_DECLARE_FINAL_TYPE(MemoryCalendar, memory_calendar, FOCAL, MEMORY_CALENDAR, Calendar)

Calendar* memory_calendar_new(void);

#endif // MEMORY_CALENDAR_H
