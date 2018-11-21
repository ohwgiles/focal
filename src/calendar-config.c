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
	free(cfg->label);
	free(cfg->location);
	free(cfg->email);
	free(cfg->cookie);
	free(cfg->login);
	free(cfg);
}

const char* calendar_type_as_string(CalendarAccountType type)
{
	switch (type) {
	case CAL_TYPE_CALDAV:
		return "CalDAV";
	case CAL_TYPE_GOOGLE:
		return "Google Calendar";
	case CAL_TYPE_OUTLOOK:
		return "Outlook 365";
	case CAL_TYPE_ICS_URL:
		return "iCal URL";
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
		gchar* type = g_key_file_get_string(keyfile, groups[i], "type", NULL);
		if (g_strcmp0(type, "caldav") == 0) {
			cfg->type = CAL_TYPE_CALDAV;
			cfg->location = g_key_file_get_string(keyfile, groups[i], "url", NULL);
			cfg->login = g_key_file_get_string(keyfile, groups[i], "user", NULL);
		} else if (g_strcmp0(type, "google") == 0) {
			cfg->type = CAL_TYPE_GOOGLE;
			cfg->cookie = g_key_file_get_string(keyfile, groups[i], "cookie", NULL);
		} else if (g_strcmp0(type, "outlook") == 0) {
			cfg->type = CAL_TYPE_OUTLOOK;
			cfg->cookie = g_key_file_get_string(keyfile, groups[i], "cookie", NULL);
		} else if (g_strcmp0(type, "ics") == 0) {
			cfg->type = CAL_TYPE_ICS_URL;
			cfg->location = g_key_file_get_string(keyfile, groups[i], "url", NULL);
		} else {
			fprintf(stderr, "Unknown calendar type `%s'\n", type);
			return NULL;
		}
		g_free(type);

		cfg->label = strdup(groups[i]);
		cfg->email = g_key_file_get_string(keyfile, groups[i], "email", NULL);
		calendar_configs = g_slist_append(calendar_configs, cfg);
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
			g_key_file_set_string(keyfile, cfg->label, "type", "caldav");
			g_key_file_set_string(keyfile, cfg->label, "url", cfg->location);
			g_key_file_set_string(keyfile, cfg->label, "user", cfg->login);
			break;
		case CAL_TYPE_GOOGLE:
			g_key_file_set_string(keyfile, cfg->label, "type", "google");
			g_key_file_set_string(keyfile, cfg->label, "cookie", cfg->cookie);
			break;
		case CAL_TYPE_OUTLOOK:
			g_key_file_set_string(keyfile, cfg->label, "type", "outlook");
			g_key_file_set_string(keyfile, cfg->label, "cookie", cfg->cookie);
			break;
		case CAL_TYPE_ICS_URL:
			g_key_file_set_string(keyfile, cfg->label, "type", "ics");
			g_key_file_set_string(keyfile, cfg->label, "url", cfg->location);
			break;
		}
		g_key_file_set_string(keyfile, cfg->label, "email", cfg->email);
	}

	if (!g_key_file_save_to_file(keyfile, config_file, &error)) {
		fprintf(stderr, "Error saving key file: %s", error->message);
		free(error);
	}
	g_key_file_free(keyfile);
}
