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

enum {
	SIGNAL_SYNC_DONE,
	SIGNAL_REQUEST_PASSWORD,
	LAST_SIGNAL
};

static gint calendar_signals[LAST_SIGNAL] = {0};

void calendar_save_event(Calendar* self, Event* event)
{
	FOCAL_CALENDAR_GET_CLASS(self)->save_event(self, event);
}

void calendar_delete_event(Calendar* self, Event* event)
{
	FOCAL_CALENDAR_GET_CLASS(self)->delete_event(self, event);
}

void calendar_each_event(Calendar* self, CalendarEachEventCallback callback, void* user)
{
	FOCAL_CALENDAR_GET_CLASS(self)->each_event(self, callback, user);
}

void calendar_sync(Calendar* self)
{
	FOCAL_CALENDAR_GET_CLASS(self)->sync(self);
}

void calendar_class_init(CalendarClass* klass)
{
	GObjectClass* goc = (GObjectClass*) klass;
	calendar_signals[SIGNAL_SYNC_DONE] = g_signal_new("sync-done", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	// TODO: learn how to use G_TYPE_STRING in return value properly...
	calendar_signals[SIGNAL_REQUEST_PASSWORD] = g_signal_new("request-password", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_POINTER, 2, G_TYPE_POINTER, G_TYPE_POINTER);
}

void calendar_init(Calendar* self)
{
}

const CalendarConfig* calendar_get_config(Calendar* self)
{
	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(self);
	return priv->config;
}

const char* calendar_get_name(Calendar* self)
{
	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(self);
	return priv->config->label;
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

#include "caldav-calendar.h"
#include "local-calendar.h"

Calendar* calendar_create(CalendarConfig* cfg)
{
	Calendar* cal;
	switch (cfg->type) {
	case CAL_TYPE_GOOGLE:
		// TODO: remove duplication with AccountEditDialog, and handle the case where the configured email doesn't match the actual logged in one
		cfg->location = g_strdup_printf("https://apidata.googleusercontent.com/caldav/v2/%s/events/", cfg->email);
		// fall through
	case CAL_TYPE_CALDAV:
		cal = caldav_calendar_new(cfg);
		break;
	case CAL_TYPE_FILE:
		cal = local_calendar_new(cfg->location);
		break;
	}

	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(cal);
	priv->config = cfg;
	double hue = (g_str_hash(priv->config->label) % USHRT_MAX) / (double) USHRT_MAX;
	gtk_hsv_to_rgb(hue, 0.7, 0.7, &priv->color.red, &priv->color.green, &priv->color.blue);
	priv->color.alpha = 0.85;

	return cal;
}
