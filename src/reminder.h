/*
 * reminder.h
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
#ifndef REMINDER_H
#define REMINDER_H

#include <gtk/gtk.h>

typedef struct _CalendarCollection CalendarCollection;

void reminder_init(CalendarCollection* cc);

// Set up timers for all events in the passed list of calendars
// which will occur soon. This method should be called every time
// the calendars change. Existing timers which are still valid
// will remain unchanged.
// TODO: support updating events from just a single calendar
// TODO: periodic sync of notifications even when calendars don't sync
void reminder_sync_notifications(GSList* calendars);

void reminder_cleanup(void);

#endif //REMINDER_H
