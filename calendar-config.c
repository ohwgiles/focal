/*
 * calendar-config.c
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

#include "calendar-config.h"

void calendar_config_free(CalendarConfig* cfg)
{
	free(cfg->name);
	free(cfg->email);
	switch (cfg->type) {
	case CAL_TYPE_CALDAV:
		free(cfg->d.caldav.url);
		free(cfg->d.caldav.user);
		free(cfg->d.caldav.pass);
		break;
	case CAL_TYPE_FILE:
		free(cfg->d.file.path);
		break;
	}
	free(cfg);
}

const char* calendar_type_as_string(CalendarAccountType type)
{
	switch (type) {
	case CAL_TYPE_CALDAV:
		return "CalDAV";
	case CAL_TYPE_FILE:
		return "Local iCal File";
	}
	return NULL;
}

GSList* calendar_config_load_from_file(const char* config_file)
{
	GKeyFile* keyfile = g_key_file_new();
	gchar** groups;
	gsize num_cals = 0;
	GSList* calendar_configs = NULL;

	g_key_file_load_from_file(keyfile, config_file, G_KEY_FILE_KEEP_COMMENTS, NULL);
	groups = g_key_file_get_groups(keyfile, &num_cals);

	for (int i = 0; i < num_cals; ++i) {
		CalendarConfig* cfg = g_new0(CalendarConfig, 1);
		//Calendar* cal;
		gchar* type = g_key_file_get_string(keyfile, groups[i], "type", NULL);
		if (g_strcmp0(type, "caldav") == 0) {
			cfg->type = CAL_TYPE_CALDAV;
			cfg->d.caldav.url = g_key_file_get_string(keyfile, groups[i], "url", NULL);
			cfg->d.caldav.user = g_key_file_get_string(keyfile, groups[i], "user", NULL);
			cfg->d.caldav.pass = g_key_file_get_string(keyfile, groups[i], "pass", NULL);
			/*
			cal = remote_calendar_new(url, user, pass);
			remote_calendar_sync(FOCAL_REMOTE_CALENDAR(cal));
			g_free(url);
			g_free(user);
			g_free(pass);*/
		} else if (g_strcmp0(type, "file") == 0) {
			cfg->type = CAL_TYPE_FILE;
			cfg->d.file.path = g_key_file_get_string(keyfile, groups[i], "path", NULL);
			/*
			cal = local_calendar_new(path);
			local_calendar_sync(FOCAL_LOCAL_CALENDAR(cal));
			g_free(path);*/
		} else {
			fprintf(stderr, "Unknown calendar type `%s'\n", type);
			return NULL;
		}
		g_free(type);

		cfg->name = strdup(groups[i]);
		cfg->email = g_key_file_get_string(keyfile, groups[i], "email", NULL);
		/*
		calendar_set_name(cal, groups[i]);
		char* email = g_key_file_get_string(config, groups[i], "email", NULL);
		calendar_set_email(cal, email);
		g_free(email);*/

		calendar_configs = g_slist_append(calendar_configs, cfg);
		//fm->calendars = g_slist_append(fm->calendars, cal);
	}
	g_strfreev(groups);
	g_key_file_free(keyfile);

	return calendar_configs;
}

void calendar_config_write_to_file(const char* config_file, GSList* confs)
{
	GKeyFile* keyfile = g_key_file_new();
	GError* error = NULL;

	for (GSList* p = confs; p; p = p->next) {
		CalendarConfig* cfg = p->data;
		switch (cfg->type) {
		case CAL_TYPE_CALDAV:
			g_key_file_set_string(keyfile, cfg->name, "type", "caldav");
			g_key_file_set_string(keyfile, cfg->name, "url", cfg->d.caldav.url);
			g_key_file_set_string(keyfile, cfg->name, "user", cfg->d.caldav.user);
			g_key_file_set_string(keyfile, cfg->name, "pass", cfg->d.caldav.pass);
			break;
		case CAL_TYPE_FILE:
			g_key_file_set_string(keyfile, cfg->name, "type", "file");
			g_key_file_set_string(keyfile, cfg->name, "path", cfg->d.file.path);
			break;
		}
		g_key_file_set_string(keyfile, cfg->name, "email", cfg->email);
	}

	if (!g_key_file_save_to_file(keyfile, config_file, &error)) {
		fprintf(stderr, "Error saving key file: %s", error->message);
		free(error);
	}
	g_key_file_free(keyfile);
}
