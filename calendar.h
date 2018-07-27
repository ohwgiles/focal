/*
 * calendar.h
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
#ifndef CALENDAR_H
#define CALENDAR_H

#include <gtk/gtk.h>
#include <libical/ical.h>

#include "calendar-config.h"

#define TYPE_CALENDAR (calendar_get_type())
G_DECLARE_DERIVABLE_TYPE(Calendar, calendar, FOCAL, CALENDAR, GObject)

typedef void (*CalendarEachEventCallback)(void* user, Calendar*, icalcomponent*);

struct _CalendarClass {
	GObjectClass parent;
	void (*add_event)(Calendar*, icalcomponent* event);
	void (*update_event)(Calendar*, icalcomponent* event);
	void (*delete_event)(Calendar*, icalcomponent* event);
	void (*each_event)(Calendar*, CalendarEachEventCallback callback, void* user);
	void (*sync)(Calendar*);
};

void calendar_add_event(Calendar* self, icalcomponent* event);

void calendar_update_event(Calendar* self, icalcomponent* event);

void calendar_delete_event(Calendar* self, icalcomponent* event);

void calendar_each_event(Calendar* self, CalendarEachEventCallback callback, void* user);

void calendar_sync(Calendar* self);

const CalendarConfig* calendar_get_config(Calendar* self);

const char* calendar_get_name(Calendar* self);

const char* calendar_get_email(Calendar* self);

GdkRGBA* calendar_get_color(Calendar* self);

// factory method
Calendar* calendar_create(CalendarConfig* config);

#endif // CALENDAR_H
