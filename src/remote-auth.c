/*
 * remote-auth.c
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
#include "remote-auth.h"
#include "async-curl.h"
#include "calendar-config.h"
#include <gtk/gtk.h>
#include <libsecret/secret.h>

G_DEFINE_TYPE(RemoteAuth, remote_auth, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_CALENDAR_CONFIG,
};

enum {
	SIGNAL_AUTH_CANCELLED,
	SIGNAL_CONFIG_MODIFIED,
	LAST_SIGNAL
};

static gint remote_auth_signals[LAST_SIGNAL] = {0};

void remote_auth_new_request(RemoteAuth* ba, void (*callback)(), void* user, void* arg)
{
	FOCAL_REMOTE_AUTH_GET_CLASS(ba)->new_request(ba, callback, user, arg);
}

static void set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
	g_assert_not_reached();
}

void remote_auth_class_init(RemoteAuthClass* klass)
{
	GObjectClass* goc = (GObjectClass*) klass;
	// set_property intentionally not provided. Child class must use g_object_class_override_property
	// and provide set_property in order to get a handle to the CalendarConfig structure.
	goc->set_property = set_property;
	g_object_class_install_property(goc, PROP_CALENDAR_CONFIG, g_param_spec_pointer("cfg", "Calendar Configuration", "", G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	remote_auth_signals[SIGNAL_AUTH_CANCELLED] = g_signal_new("cancelled", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	remote_auth_signals[SIGNAL_CONFIG_MODIFIED] = g_signal_new("config-modified", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

void remote_auth_init(RemoteAuth* ra)
{
}

void remote_auth_invalidate_credential(RemoteAuth* ba, void (*callback)(), void* user, void* arg)
{
	FOCAL_REMOTE_AUTH_GET_CLASS(ba)->invalidate_credential(ba, callback, user, arg);
}
