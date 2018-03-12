/*
 * calendar.c
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
#include "calendar.h"

G_DEFINE_TYPE(Calendar, calendar, G_TYPE_OBJECT)

void calendar_add_event(Calendar* self, icalcomponent* event)
{
	FOCAL_CALENDAR_GET_CLASS(self)->add_event(self, event);
}

void calendar_delete_event(Calendar* self, icalcomponent* event)
{
	FOCAL_CALENDAR_GET_CLASS(self)->delete_event(self, event);
}

void calendar_each_event(Calendar* self, CalendarEachEventCallback callback, void* user)
{
	FOCAL_CALENDAR_GET_CLASS(self)->each_event(self, callback, user);
}

void calendar_class_init(CalendarClass* klass)
{
}

void calendar_init(Calendar* self)
{
}

void calendar_set_email(Calendar* self, const char* email)
{
	g_object_set_data((GObject*) self, "email", g_strdup(email));
}

const char* calendar_get_email(Calendar* self)
{
	g_object_get_data((GObject*) self, "email");
}
