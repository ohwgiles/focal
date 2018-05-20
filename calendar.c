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

typedef struct {
	char* name;
	GdkRGBA color;
} CalendarPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(Calendar, calendar, G_TYPE_OBJECT)

void calendar_add_event(Calendar* self, icalcomponent* event)
{
	FOCAL_CALENDAR_GET_CLASS(self)->add_event(self, event);
}

void calendar_update_event(Calendar* self, icalcomponent* event)
{
	FOCAL_CALENDAR_GET_CLASS(self)->update_event(self, event);
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

void calendar_set_name(Calendar* self, const char* name)
{
	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(self);
	g_free(priv->name);
	priv->name = g_strdup(name);
	double hue = (g_str_hash(name) % USHRT_MAX) / (double) USHRT_MAX;
	gtk_hsv_to_rgb(hue, 0.7, 0.7, &priv->color.red, &priv->color.green, &priv->color.blue);
	priv->color.alpha = 0.85;
}

void calendar_set_email(Calendar* self, const char* email)
{
	g_object_set_data((GObject*) self, "email", g_strdup(email));
}

const char* calendar_get_email(Calendar* self)
{
	return g_object_get_data((GObject*) self, "email");
}

GdkRGBA* calendar_get_color(Calendar* self)
{
	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(self);
	return &priv->color;
}
