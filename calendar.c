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
#include "calendar-config.h"

typedef struct {
	const CalendarConfig* config;
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
}

const char* calendar_get_name(Calendar* self)
{
	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(self);
	return priv->config->name;
}

void calendar_set_email(Calendar* self, const char* email)
{
}

const char* calendar_get_email(Calendar* self)
{
	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(self);
	return priv->config->email;
}

GdkRGBA* calendar_get_color(Calendar* self)
{
	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(self);
	return &priv->color;
}

#include "remote-calendar.h"
#include "local-calendar.h"

Calendar* calendar_create(CalendarConfig* cfg)
{
	Calendar* cal;
	switch (cfg->type) {
	case CAL_TYPE_CALDAV:
		cal = remote_calendar_new(cfg->d.caldav.url, cfg->d.caldav.user, cfg->d.caldav.pass);
		remote_calendar_sync(FOCAL_REMOTE_CALENDAR(cal));
		break;
	case CAL_TYPE_FILE:
		cal = local_calendar_new(cfg->d.file.path);
		local_calendar_sync(FOCAL_LOCAL_CALENDAR(cal));
		break;
	}

	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(cal);
	priv->config = cfg;
	double hue = (g_str_hash(priv->config->name) % USHRT_MAX) / (double) USHRT_MAX;
	gtk_hsv_to_rgb(hue, 0.7, 0.7, &priv->color.red, &priv->color.green, &priv->color.blue);
	priv->color.alpha = 0.85;

	return cal;
}
