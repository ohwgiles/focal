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
#include "remote-auth-basic.h"
#include "remote-auth-oauth2.h"

typedef struct {
	const CalendarConfig* config;
	RemoteAuth* auth;
	GdkRGBA color;
} CalendarPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(Calendar, calendar, G_TYPE_OBJECT)

enum {
	SIGNAL_SYNC_DONE,
	SIGNAL_REQUEST_PASSWORD,
	SIGNAL_CONFIG_MODIFIED,
	LAST_SIGNAL
};

static gint calendar_signals[LAST_SIGNAL] = {0};

enum {
	PROP_0,
	PROP_CALENDAR_CONFIG,
	PROP_REMOTE_AUTH,
	PROP_LAST
};

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

gboolean calendar_is_read_only(Calendar* self)
{
	return FOCAL_CALENDAR_GET_CLASS(self)->read_only(self);
}

void calendar_load_additional_for_date_range(Calendar* self, icaltime_span range)
{
	CalendarClass* cc = FOCAL_CALENDAR_GET_CLASS(self);
	if (cc->load_additional_for_date_range)
		cc->load_additional_for_date_range(self, range);
}

static void on_config_modified(Calendar* self)
{
	g_signal_emit(self, calendar_signals[SIGNAL_CONFIG_MODIFIED], 0);
}

static void set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(FOCAL_CALENDAR(object));
	if (prop_id == PROP_CALENDAR_CONFIG) {
		priv->config = g_value_get_pointer(value);
	} else if (prop_id == PROP_REMOTE_AUTH) {
		CalendarClass* cc = FOCAL_CALENDAR_GET_CLASS(object);
		RemoteAuth* auth = FOCAL_REMOTE_AUTH(g_value_get_pointer(value));
		if (cc->attach_authenticator)
			cc->attach_authenticator(FOCAL_CALENDAR(object), auth);
		if (auth)
			g_signal_connect_swapped(auth, "config-modified", G_CALLBACK(on_config_modified), object);
	} else {
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

void calendar_class_init(CalendarClass* klass)
{
	GObjectClass* goc = (GObjectClass*) klass;
	calendar_signals[SIGNAL_SYNC_DONE] = g_signal_new("sync-done", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	// TODO: learn how to use G_TYPE_STRING in return value properly...
	calendar_signals[SIGNAL_REQUEST_PASSWORD] = g_signal_new("request-password", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_POINTER, 2, G_TYPE_POINTER, G_TYPE_POINTER);
	calendar_signals[SIGNAL_CONFIG_MODIFIED] = g_signal_new("config-modified", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	goc->set_property = set_property;
	g_object_class_install_property(goc, PROP_CALENDAR_CONFIG, g_param_spec_pointer("cfg", "Calendar Configuration", "", G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property(goc, PROP_REMOTE_AUTH, g_param_spec_pointer("auth", "Remote Authenticator", "", G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
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

const char* calendar_get_location(Calendar* self)
{
	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(self);
	return priv->config->location;
}

#include "caldav-calendar.h"
#include "ics-calendar.h"
#include "oauth2-provider-google.h"
#include "outlook-calendar.h"

Calendar* calendar_create(CalendarConfig* cfg)
{
	Calendar* cal;
	switch (cfg->type) {
	case CAL_TYPE_GOOGLE:
		cfg->location = g_strdup_printf("https://apidata.googleusercontent.com/caldav/v2/%s/events/", cfg->email);
		cal = g_object_new(CALDAV_CALENDAR_TYPE, "cfg", cfg, "auth", g_object_new(REMOTE_AUTH_OAUTH2_TYPE, "cfg", cfg, "provider", g_object_new(TYPE_OAUTH2_PROVIDER_GOOGLE, NULL), NULL), NULL);
		break;
	case CAL_TYPE_CALDAV:
		cal = g_object_new(CALDAV_CALENDAR_TYPE, "cfg", cfg, "auth", g_object_new(REMOTE_AUTH_BASIC_TYPE, "cfg", cfg, NULL), NULL);
		break;
	case CAL_TYPE_OUTLOOK:
		cal = outlook_calendar_new(cfg);
		break;
	case CAL_TYPE_ICS_URL:
		cal = ics_calendar_new(cfg->location);
		break;
	}

	CalendarPrivate* priv = (CalendarPrivate*) calendar_get_instance_private(cal);
	priv->config = cfg;
	double hue = (g_str_hash(priv->config->label) % USHRT_MAX) / (double) USHRT_MAX;
	gtk_hsv_to_rgb(hue, 0.7, 0.7, &priv->color.red, &priv->color.green, &priv->color.blue);
	priv->color.alpha = 0.85;

	return cal;
}
