/*
 * calendar-config.h
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
#ifndef CALENDAR_CONFIG_H
#define CALENDAR_CONFIG_H

#include <gtk/gtk.h>

typedef enum {
	CAL_TYPE_CALDAV,
	CAL_TYPE_GOOGLE,
	CAL_TYPE_OUTLOOK,
	CAL_TYPE_ICS_URL,

	CAL_TYPE__FIRST = CAL_TYPE_CALDAV,
	CAL_TYPE__LAST = CAL_TYPE_ICS_URL,
} CalendarAccountType;

typedef struct _CalendarConfig {
	gchar* label;
	gchar* location;
	gchar* email;
	gchar* cookie;
	gchar* login;
	CalendarAccountType type;
} CalendarConfig;

void calendar_config_free(CalendarConfig* cfg);

const char* calendar_type_as_string(CalendarAccountType type);

GSList* calendar_config_load_from_file(const char* config_file);

void calendar_config_write_to_file(const char* config_file, GSList* confs);

#endif // CALENDAR_CONFIG_H
