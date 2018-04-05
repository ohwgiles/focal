/*
 * main.c
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
#include "event-panel.h"
#include "remote-calendar.h"
#include "rpc.h"
#include "week-view.h"

#include <curl/curl.h>
#include <gtk/gtk.h>
#include <libical/ical.h>
#include <strings.h>

typedef struct {
	GtkWidget* mainWindow;
	GSList* calendars;
	GtkWidget* weekView;
} FocalMain;

static char* icc_read_stream(char* s, size_t sz, void* ud)
{
	return fgets(s, sz, (FILE*) ud);
}

static icalcomponent* icalcomponent_from_file(const char* path)
{
	FILE* stream = fopen(path, "r");
	if (!stream)
		return NULL;

	icalcomponent* c;
	char* line;
	icalparser* parser = icalparser_new();
	icalparser_set_gen_data(parser, stream);
	do {
		line = icalparser_get_line(parser, &icc_read_stream);
		c = icalparser_add_line(parser, line);
		if (c)
			return c;
	} while (line);
	return NULL;
}

static void focal_add_event(FocalMain* focal, icalcomponent* vev)
{
	Calendar* cal = NULL;
	icalparameter* partstat = NULL;

	for (icalproperty* attendees = icalcomponent_get_first_property(vev, ICAL_ATTENDEE_PROPERTY); attendees; attendees = icalcomponent_get_next_property(vev, ICAL_ATTENDEE_PROPERTY)) {
		const char* cal_addr = icalproperty_get_attendee(attendees);
		if (strncasecmp(cal_addr, "mailto:", 7) != 0)
			continue;
		cal_addr = &cal_addr[7];
		// check each known Calendar for matching address
		for (GSList* c = focal->calendars; c; c = c->next) {
			const char* email = calendar_get_email(FOCAL_CALENDAR(c->data));
			if (email && strcasecmp(email, cal_addr) == 0) {
				cal = FOCAL_CALENDAR(c->data);
				partstat = icalproperty_get_first_parameter(attendees, ICAL_PARTSTAT_PARAMETER);
				// TODO what is the effect of this on common caldav servers?
				icalproperty_remove_parameter_by_kind(attendees, ICAL_RSVP_PARAMETER);
			}
		}
	}

	// TODO allow selecting a calendar in the dialog
	if (!cal)
		cal = FOCAL_CALENDAR(focal->calendars->data);

	week_view_add_event(FOCAL_WEEK_VIEW(focal->weekView), cal, vev);

	GtkWidget* dialog;
	dialog = gtk_message_dialog_new(GTK_WINDOW(focal->mainWindow),
									GTK_DIALOG_DESTROY_WITH_PARENT,
									GTK_MESSAGE_ERROR,
									GTK_BUTTONS_YES_NO,
									"Add event \"%s\" to calendar?",
									icalcomponent_get_summary(vev));
	int resp = gtk_dialog_run(GTK_DIALOG(dialog));
	if (resp == GTK_RESPONSE_YES) {
		// TODO allow selecting a different response
		if (partstat) {
			icalparameter_set_partstat(partstat, ICAL_PARTSTAT_ACCEPTED);
		}

		calendar_add_event(cal, vev);
	} else {
		week_view_remove_event(FOCAL_WEEK_VIEW(focal->weekView), vev);
	}
	gtk_widget_destroy(dialog);
}

static void rpc_handle_cmd(const char* cmd, void* data)
{
	icalcomponent* c = icalcomponent_from_file(cmd);
	if (c) {
		icalcomponent* vev = icalcomponent_get_first_real_component(c);
		if (vev)
			focal_add_event(data, vev);
	}
}

static void cal_event_selected(WeekView* widget, Calendar* cal, icalcomponent* e, EventPanel* ew)
{
	event_panel_set_event(ew, cal, e);
}

static void event_delete(EventPanel* event_panel, Calendar* cal, icalcomponent* ev, FocalMain* focal)
{
	// TODO "are you sure?" popup
	calendar_delete_event(FOCAL_CALENDAR(cal), ev);
	week_view_remove_event(FOCAL_WEEK_VIEW(focal->weekView), ev);
}

static void load_calendar_config(FocalMain* fm)
{
	GKeyFile* config = g_key_file_new();
	gchar** groups;
	gsize num_cals = 0;
	const char *config_dir, *home;
	char* config_file;

	if ((config_dir = g_getenv("XDG_CONFIG_HOME")))
		asprintf(&config_file, "%s/focal.conf", config_dir);
	else if ((home = g_getenv("HOME")))
		asprintf(&config_file, "%s/.config/focal.conf", home);
	else
		return (void) fprintf(stderr, "Could not find .config path\n");

	g_key_file_load_from_file(config, config_file, G_KEY_FILE_KEEP_COMMENTS, NULL);
	groups = g_key_file_get_groups(config, &num_cals);

	for (int i = 0; i < num_cals; ++i) {
		gchar* url = g_key_file_get_string(config, groups[i], "url", NULL);
		gchar* user = g_key_file_get_string(config, groups[i], "user", NULL);
		gchar* pass = g_key_file_get_string(config, groups[i], "pass", NULL);
		gchar* email = g_key_file_get_string(config, groups[i], "email", NULL);

		Calendar* rc = remote_calendar_new(url, user, pass);
		calendar_set_email(rc, email);
		remote_calendar_sync(FOCAL_REMOTE_CALENDAR(rc));

		fm->calendars = g_slist_append(fm->calendars, rc);

		week_view_add_calendar(FOCAL_WEEK_VIEW(fm->weekView), rc);
	}
	g_strfreev(groups);
}

int main(int argc, char** argv)
{
	rpc_status_t rpc = rpc_init();
	if (rpc == RPC_BIND_ERROR) {
		return 1;
	} else if (rpc == RPC_BIND_INUSE) {
		rpc_connect();
		for (int i = 1; i < argc; ++i) {
			// todo realpath
			rpc_send_command(argv[i]);
		}
		return 0;
	}

	gtk_init(&argc, &argv);

	FocalMain fm = {0};
	fm.mainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	fm.weekView = week_view_new();
	load_calendar_config(&fm);

	GtkWidget* event_panel = event_panel_new();

	rpc_server(&rpc_handle_cmd, &fm);

	gtk_window_set_type_hint((GtkWindow*) fm.mainWindow, GDK_WINDOW_TYPE_HINT_DIALOG);

	g_signal_connect(fm.weekView, "event-selected", (GCallback) &cal_event_selected, event_panel);
	g_signal_connect(event_panel, "cal-event-delete", (GCallback) &event_delete, &fm);

	GtkWidget* header = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);

	GtkWidget* overlay = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(overlay), fm.weekView);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), event_panel);
	gtk_window_set_titlebar(GTK_WINDOW(fm.mainWindow), header);
	gtk_container_add(GTK_CONTAINER(fm.mainWindow), overlay);

	g_signal_connect(fm.mainWindow, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	gtk_window_set_default_size(GTK_WINDOW(fm.mainWindow), 780, 630);

	// create window title
	int week_num = week_view_get_current_week(FOCAL_WEEK_VIEW(fm.weekView));
	char week_title[8];
	snprintf(week_title, 8, "Week %d", week_num);

	gtk_window_set_title(GTK_WINDOW(fm.mainWindow), week_title);

	gtk_widget_show_all(fm.mainWindow);

	// handle invitations on command line
	for (int i = 1; i < argc; ++i)
		rpc_handle_cmd(argv[i], &fm);

	gtk_main();
}
