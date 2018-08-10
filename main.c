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
#include "accounts-dialog.h"
#include "async-curl.h"
#include "calendar-config.h"
#include "event-panel.h"
#include "event-private.h"
#include "week-view.h"

#include <curl/curl.h>
#include <gtk/gtk.h>
#include <libical/ical.h>
#include <strings.h>

typedef struct {
	GtkWidget* mainWindow;
	char* config_path;
	GSList* config;
	GSList* calendars;
	GtkWidget* weekView;
	GtkWidget* popover;
	GtkWidget* eventDetail;
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

static void focal_add_event(FocalMain* focal, icalcomponent* ev)
{
	Calendar* cal = NULL;
	icalparameter* partstat = NULL;

	for (icalproperty* attendees = icalcomponent_get_first_property(ev, ICAL_ATTENDEE_PROPERTY); attendees; attendees = icalcomponent_get_next_property(ev, ICAL_ATTENDEE_PROPERTY)) {
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
				// break from both loops
				attendees = NULL;
				break;
			}
		}
	}

	// TODO allow selecting a calendar in the dialog
	if (!cal)
		cal = FOCAL_CALENDAR(focal->calendars->data);

	week_view_add_event(FOCAL_WEEK_VIEW(focal->weekView), cal, ev);

	GtkWidget* dialog;
	dialog = gtk_message_dialog_new(GTK_WINDOW(focal->mainWindow),
									GTK_DIALOG_DESTROY_WITH_PARENT,
									GTK_MESSAGE_ERROR,
									GTK_BUTTONS_YES_NO,
									"Add event \"%s\" to calendar?",
									icalcomponent_get_summary(ev));
	int resp = gtk_dialog_run(GTK_DIALOG(dialog));
	if (resp == GTK_RESPONSE_YES) {
		// TODO allow selecting a different response
		if (partstat) {
			icalparameter_set_partstat(partstat, ICAL_PARTSTAT_ACCEPTED);
		}

		calendar_add_event(cal, ev);
	} else {
		week_view_remove_event(FOCAL_WEEK_VIEW(focal->weekView), ev);
	}
	gtk_widget_destroy(dialog);
}

static void cal_event_selected(WeekView* widget, Calendar* cal, icalcomponent* ev, GdkRectangle* rect, FocalMain* fm)
{
	if (ev) {
		gtk_popover_set_pointing_to(GTK_POPOVER(fm->popover), rect);
		event_panel_set_event(FOCAL_EVENT_PANEL(fm->eventDetail), cal, ev);
		gtk_popover_popup(GTK_POPOVER(fm->popover));
	}
}

static void event_delete(EventPanel* event_panel, Calendar* cal, icalcomponent* ev, FocalMain* focal)
{
	// TODO "are you sure?" popup
	calendar_delete_event(FOCAL_CALENDAR(cal), ev);
	week_view_remove_event(FOCAL_WEEK_VIEW(focal->weekView), ev);
}

static void event_save(EventPanel* event_panel, Calendar* cal, icalcomponent* ev, FocalMain* focal)
{
	if (icalcomponent_has_private(ev)) {
		calendar_update_event(FOCAL_CALENDAR(cal), ev);
	} else {
		calendar_add_event(FOCAL_CALENDAR(cal), ev);
	}
	// needed in case the time, duration or summary changed
	// TODO once moving events between calendars is supported, this will not be sufficient
	week_view_refresh(FOCAL_WEEK_VIEW(focal->weekView), ev);
}

void toggle_calendar(GSimpleAction* action, GVariant* value, FocalMain* fm)
{
	const char* calendar_name = strchr(g_action_get_name(G_ACTION(action)), '.') + 1;
	Calendar* calendar = NULL;
	for (GSList* p = fm->calendars; p; p = p->next) {
		if (strcmp(calendar_name, calendar_get_name(FOCAL_CALENDAR(p->data))) == 0) {
			calendar = FOCAL_CALENDAR(p->data);
			break;
		}
	}

	if (!calendar)
		return;

	if (g_variant_get_boolean(value))
		week_view_add_calendar(FOCAL_WEEK_VIEW(fm->weekView), calendar);
	else
		week_view_remove_calendar(FOCAL_WEEK_VIEW(fm->weekView), calendar);

	g_simple_action_set_state(action, value);
}

static void calendar_synced(FocalMain* fm, Calendar* cal)
{
	// TODO more efficiently?
	week_view_remove_calendar(FOCAL_WEEK_VIEW(fm->weekView), cal);
	week_view_add_calendar(FOCAL_WEEK_VIEW(fm->weekView), cal);
}

static void create_calendars(FocalMain* fm)
{
	for (GSList* p = fm->config; p; p = p->next) {
		CalendarConfig* cfg = p->data;
		Calendar* cal = calendar_create(cfg);
		g_signal_connect_swapped(cal, "sync-done", (GCallback) calendar_synced, fm);
		fm->calendars = g_slist_append(fm->calendars, cal);
		calendar_sync(cal);
	}

	// create window actions
	for (GSList* p = fm->calendars; p; p = p->next) {
		char* action_name = g_strdup_printf("toggle-calendar.%s", calendar_get_name(FOCAL_CALENDAR(p->data)));
		GSimpleAction* a = g_simple_action_new_stateful(action_name, NULL, g_variant_new_boolean(TRUE));
		g_signal_connect(a, "change-state", (GCallback) toggle_calendar, fm);
		g_action_map_add_action(G_ACTION_MAP(fm->mainWindow), G_ACTION(a));
		g_free(action_name);
	}
}

static void update_window_title(FocalMain* fm)
{
	int week_num = week_view_get_current_week(FOCAL_WEEK_VIEW(fm->weekView));
	char week_title[8];
	snprintf(week_title, 8, "Week %d", week_num);

	gtk_window_set_title(GTK_WINDOW(fm->mainWindow), week_title);
}

static void on_nav_previous(GtkButton* button, FocalMain* fm)
{
	week_view_previous(FOCAL_WEEK_VIEW(fm->weekView));
	update_window_title(fm);
}

static void on_nav_next(GtkButton* button, FocalMain* fm)
{
	week_view_next(FOCAL_WEEK_VIEW(fm->weekView));
	update_window_title(fm);
}

static void on_calendar_menu(GtkButton* button, FocalMain* fm)
{
	GMenu* menu_main = g_menu_new();
	GMenu* menu_calanders = g_menu_new();
	for (GSList* p = fm->calendars; p; p = p->next) {
		Calendar* cal = FOCAL_CALENDAR(p->data);
		char* action_name = g_strdup_printf("win.toggle-calendar.%s", calendar_get_name(cal));
		g_menu_append(menu_calanders, calendar_get_name(cal), action_name);
		g_free(action_name);
	}

	g_menu_append_section(menu_main, NULL, G_MENU_MODEL(menu_calanders));
	g_menu_append(menu_main, "Accounts", "win.accounts");

	GtkWidget* menu = gtk_popover_new_from_model(GTK_WIDGET(button), G_MENU_MODEL(menu_main));
	gtk_popover_popdown(GTK_POPOVER(menu));
	gtk_widget_show(menu);
}

static void on_sync_clicked(GtkButton* button, FocalMain* fm)
{
	for (GSList* p = fm->calendars; p; p = p->next) {
		Calendar* cal = FOCAL_CALENDAR(p->data);
		calendar_sync(cal);
	}
}

static void on_config_changed(GtkWidget* accounts, GSList* new_config, gpointer user_data)
{
	FocalMain* fm = (FocalMain*) user_data;
	// remove all calendars from view and remove window action
	for (GSList* p = fm->calendars; p; p = p->next) {
		week_view_remove_calendar(FOCAL_WEEK_VIEW(fm->weekView), p->data);
		char* action_name = g_strdup_printf("win.toggle-calendar.%s", calendar_get_name(p->data));
		g_action_map_remove_action(G_ACTION_MAP(fm->mainWindow), action_name);
		g_free(action_name);
	}
	// free all calendars and configs
	g_slist_free_full(fm->calendars, g_object_unref);
	fm->calendars = NULL;
	// write new config back to file
	calendar_config_write_to_file(fm->config_path, new_config);
	// recreate calendars from new config
	fm->config = new_config;
	create_calendars(fm);
}

static void open_accounts_dialog(GSimpleAction* simple, GVariant* parameter, gpointer user_data)
{
	FocalMain* fm = (FocalMain*) user_data;
	GtkWidget* accounts = accounts_dialog_new(GTK_WINDOW(fm->mainWindow), fm->config);
	g_signal_connect(accounts, "config-changed", (GCallback) on_config_changed, fm);
	gtk_widget_show_all(accounts);
	g_signal_connect(accounts, "response", G_CALLBACK(gtk_widget_destroy), NULL);
}

static void focal_create_main_window(GApplication* app, FocalMain* fm)
{
	fm->mainWindow = gtk_application_window_new(GTK_APPLICATION(app));
	fm->weekView = week_view_new();
	fm->eventDetail = event_panel_new();

	// todo: better separation of ui from calendar models?
	create_calendars(fm);

	const GActionEntry entries[] = {
		{"accounts", open_accounts_dialog},
	};

	g_action_map_add_action_entries(G_ACTION_MAP(fm->mainWindow), entries, G_N_ELEMENTS(entries), fm);

	fm->popover = gtk_popover_new(fm->weekView);
	gtk_popover_set_position(GTK_POPOVER(fm->popover), GTK_POS_RIGHT);
	gtk_container_add(GTK_CONTAINER(fm->popover), fm->eventDetail);
	gtk_widget_show_all(fm->eventDetail);

	gtk_window_set_type_hint((GtkWindow*) fm->mainWindow, GDK_WINDOW_TYPE_HINT_DIALOG);

	g_signal_connect(fm->weekView, "event-selected", (GCallback) &cal_event_selected, fm);
	g_signal_connect(fm->eventDetail, "cal-event-delete", (GCallback) &event_delete, fm);
	g_signal_connect(fm->eventDetail, "cal-event-save", (GCallback) &event_save, fm);

	GtkWidget* header = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
	GtkWidget* nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(nav), "linked");
	GtkWidget *prev = gtk_button_new(), *next = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(prev), gtk_image_new_from_icon_name("pan-start-symbolic", GTK_ICON_SIZE_MENU));
	gtk_button_set_image(GTK_BUTTON(next), gtk_image_new_from_icon_name("pan-end-symbolic", GTK_ICON_SIZE_MENU));
	gtk_container_add(GTK_CONTAINER(nav), prev);
	gtk_container_add(GTK_CONTAINER(nav), next);
	g_signal_connect(prev, "clicked", (GCallback) &on_nav_previous, fm);
	g_signal_connect(next, "clicked", (GCallback) &on_nav_next, fm);

	GtkWidget* menu = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(menu), gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_MENU));
	g_signal_connect(menu, "clicked", (GCallback) &on_calendar_menu, fm);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu);

	GtkWidget* syncbutton = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(syncbutton), gtk_image_new_from_icon_name("emblem-synchronizing-symbolic", GTK_ICON_SIZE_MENU));
	g_signal_connect(syncbutton, "clicked", (GCallback) &on_sync_clicked, fm);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), syncbutton);

	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), nav);
	gtk_window_set_titlebar(GTK_WINDOW(fm->mainWindow), header);

	GtkWidget* sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(sw), fm->weekView);
	gtk_container_add(GTK_CONTAINER(fm->mainWindow), sw);

	gtk_window_set_default_size(GTK_WINDOW(fm->mainWindow), 780, 630);

	update_window_title(fm);

	gtk_widget_show_all(fm->mainWindow);
}

static void focal_startup(GApplication* app, FocalMain* fm)
{
	// needed to generate unique uuids for new events
	srand(time(NULL) * getpid());

	async_curl_init();

	const char *config_dir, *home;
	if ((config_dir = g_getenv("XDG_CONFIG_HOME")))
		asprintf(&fm->config_path, "%s/focal.conf", config_dir);
	else if ((home = g_getenv("HOME")))
		asprintf(&fm->config_path, "%s/.config/focal.conf", home);
	else
		return (void) fprintf(stderr, "Could not find .config path\n");

	fm->config = calendar_config_load_from_file(fm->config_path);
}

static void focal_activate(GApplication* app, FocalMain* fm)
{
	focal_create_main_window(app, fm);
}

static void focal_shutdown(GApplication* app, FocalMain* fm)
{
	g_slist_free_full(fm->calendars, g_object_unref);
	g_slist_free_full(fm->config, (GDestroyNotify) calendar_config_free);
	free(fm->config_path);
	async_curl_cleanup();
}

static void focal_open(GApplication* app, GFile** files, gint n_files, gchar* hint, FocalMain* fm)
{
	if (!fm->mainWindow)
		focal_create_main_window(app, fm);
	for (int i = 0; i < n_files; ++i) {
		icalcomponent* c = icalcomponent_from_file(g_file_get_path(files[i]));
		if (c) {
			icalcomponent* vev = icalcomponent_get_first_real_component(c);
			if (vev) {
				focal_add_event(fm, vev);
			}
		}
	}
}

int main(int argc, char** argv)
{
	int status;
	GtkApplication* app = gtk_application_new("net.ohwg.focal", G_APPLICATION_HANDLES_OPEN);
	FocalMain fm = {0};

	g_signal_connect(app, "startup", G_CALLBACK(focal_startup), &fm);
	g_signal_connect(app, "activate", G_CALLBACK(focal_activate), &fm);
	g_signal_connect(app, "shutdown", G_CALLBACK(focal_shutdown), &fm);
	g_signal_connect(app, "open", G_CALLBACK(focal_open), &fm);
	g_application_register(G_APPLICATION(app), NULL, NULL);

	status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);

	return status;
}
