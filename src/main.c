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
#include "week-view.h"

#include <curl/curl.h>
#include <gtk/gtk.h>
#include <libical/ical.h>
#include <strings.h>

#define FOCAL_TYPE_APP (focal_app_get_type())
G_DECLARE_FINAL_TYPE(FocalApp, focal_app, FOCAL, APP, GtkApplication)

struct _FocalApp {
	GtkApplication parent;
	GtkWidget* mainWindow;
	char* config_path;
	GSList* config;
	GSList* calendars;
	GtkWidget* weekView;
	GtkWidget* popover;
	GtkWidget* eventDetail;
};

enum {
	SIGNAL_BROWSER_AUTH_RESP,
	LAST_SIGNAL
};
static gint focal_app_signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE(FocalApp, focal_app, GTK_TYPE_APPLICATION)

static void match_event_to_calendar(Event* ev, icalproperty* attendee, GSList* calendars)
{
	const char* cal_addr = icalproperty_get_attendee(attendee);
	if (strncasecmp(cal_addr, "mailto:", 7) != 0)
		return;
	cal_addr = &cal_addr[7];
	// check each known Calendar for matching address
	for (GSList* c = calendars; c; c = c->next) {
		const char* email = calendar_get_email(FOCAL_CALENDAR(c->data));
		if (email && strcasecmp(email, cal_addr) == 0) {
			event_set_calendar(ev, FOCAL_CALENDAR(c->data));
			// TODO: consider implementing an early break from event_each_attendee
			return;
		}
	}
}

static void cal_event_selected(WeekView* widget, Event* ev, GdkRectangle* rect, FocalApp* fm)
{
	if (ev) {
		gtk_popover_set_pointing_to(GTK_POPOVER(fm->popover), rect);
		event_panel_set_event(FOCAL_EVENT_PANEL(fm->eventDetail), ev);
		gtk_popover_popup(GTK_POPOVER(fm->popover));
	}
}

static void focal_add_event(FocalApp* focal, Event* ev)
{
	event_each_attendee(ev, match_event_to_calendar, focal->calendars);

	Calendar* cal = event_get_calendar(ev);
	// TODO allow selecting a calendar in the dialog
	if (!cal) {
		cal = FOCAL_CALENDAR(focal->calendars->data);
		event_set_calendar(ev, cal);
	}

	week_view_focus_event(FOCAL_WEEK_VIEW(focal->weekView), ev);
	week_view_add_event(FOCAL_WEEK_VIEW(focal->weekView), ev);
}

static void on_event_modified(EventPanel* event_panel, Event* ev, FocalApp* focal)
{
	// needed in case the time, duration or summary changed
	// TODO once moving events between calendars is supported, this will not be sufficient
	week_view_refresh(FOCAL_WEEK_VIEW(focal->weekView), ev);
}

void toggle_calendar(GSimpleAction* action, GVariant* value, FocalApp* fm)
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

static void calendar_synced(FocalApp* fm, Calendar* cal)
{
	// TODO more efficiently?
	week_view_remove_calendar(FOCAL_WEEK_VIEW(fm->weekView), cal);
	week_view_add_calendar(FOCAL_WEEK_VIEW(fm->weekView), cal);
	// The popover might be up and holding a reference to a just-invalidated event
	gtk_widget_hide(fm->popover);
}

static void create_calendars(FocalApp* fm)
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

static void update_window_title(FocalApp* fm)
{
	int week_num = week_view_get_current_week(FOCAL_WEEK_VIEW(fm->weekView));
	char week_title[8];
	snprintf(week_title, 8, "Week %d", week_num);

	gtk_window_set_title(GTK_WINDOW(fm->mainWindow), week_title);
}

static void on_calendar_menu(GtkButton* button, FocalApp* fm)
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

static void on_sync_clicked(GtkButton* button, FocalApp* fm)
{
	for (GSList* p = fm->calendars; p; p = p->next) {
		Calendar* cal = FOCAL_CALENDAR(p->data);
		calendar_sync(cal);
	}
}

static void on_config_changed(GtkWidget* accounts, GSList* new_config, gpointer user_data)
{
	FocalApp* fm = (FocalApp*) user_data;
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
	FocalApp* fm = (FocalApp*) user_data;
	GtkWidget* accounts = accounts_dialog_new(GTK_WINDOW(fm->mainWindow), fm->config);
	g_signal_connect(accounts, "config-changed", (GCallback) on_config_changed, fm);
	g_signal_connect(accounts, "response", G_CALLBACK(gtk_widget_destroy), NULL);
	gtk_widget_show_all(accounts);
}

static void focal_create_main_window(GApplication* app, FocalApp* fm)
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
	g_signal_connect_swapped(fm->weekView, "date-range-changed", (GCallback) &update_window_title, fm);
	g_signal_connect(fm->eventDetail, "event-modified", (GCallback) &on_event_modified, fm);

	GtkWidget* header = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
	GtkWidget* nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(nav), "linked");
	GtkWidget *prev = gtk_button_new(), *next = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(prev), gtk_image_new_from_icon_name("pan-start-symbolic", GTK_ICON_SIZE_MENU));
	gtk_button_set_image(GTK_BUTTON(next), gtk_image_new_from_icon_name("pan-end-symbolic", GTK_ICON_SIZE_MENU));
	gtk_container_add(GTK_CONTAINER(nav), prev);
	gtk_container_add(GTK_CONTAINER(nav), next);
	g_signal_connect_swapped(prev, "clicked", (GCallback) &week_view_previous, fm->weekView);
	g_signal_connect_swapped(next, "clicked", (GCallback) &week_view_next, fm->weekView);

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

static void focal_startup(GApplication* app)
{
	// needed to generate unique uuids for new events
	srand(time(NULL) * getpid());

	async_curl_init();

	FocalApp* fm = FOCAL_APP(app);
	const char *config_dir, *home;
	if ((config_dir = g_getenv("XDG_CONFIG_HOME")))
		asprintf(&fm->config_path, "%s/focal.conf", config_dir);
	else if ((home = g_getenv("HOME")))
		asprintf(&fm->config_path, "%s/.config/focal.conf", home);
	else
		return (void) fprintf(stderr, "Could not find .config path\n");

	fm->config = calendar_config_load_from_file(fm->config_path);

	g_application_activate(app);
}

static void focal_activate(GApplication* app)
{
	FocalApp* fm = FOCAL_APP(app);
	focal_create_main_window(app, fm);
}

static void focal_shutdown(GApplication* app)
{
	FocalApp* fm = FOCAL_APP(app);
	g_slist_free_full(fm->calendars, g_object_unref);
	g_slist_free_full(fm->config, (GDestroyNotify) calendar_config_free);
	free(fm->config_path);
	async_curl_cleanup();
}

static gint focal_cmdline(GApplication* application, GApplicationCommandLine* command_line)
{
	gint argc;
	gint ret = 0;
	gchar** argv = g_application_command_line_get_arguments(command_line, &argc);
	for (int i = 1; i < argc; ++i) {
		const char* auth_scheme = "net.ohwg.focal:/";
		gchar* query;
		if (g_ascii_strncasecmp(argv[i], auth_scheme, strlen(auth_scheme)) == 0 && (query = strchr(argv[i], '?'))) {
			gchar *cookie = NULL, *code = NULL, *sp;
			for (char* p = strtok_r(query + 1, "&", &sp); p; p = strtok_r(NULL, "&", &sp)) {
				if (g_ascii_strncasecmp(p, "state=", strlen("state=")) == 0) {
					cookie = &p[strlen("state=")];
				} else if (g_ascii_strncasecmp(p, "code=", strlen("code=")) == 0) {
					code = &p[strlen("code=")];
				}
			}
			if (cookie && code)
				g_signal_emit(application, focal_app_signals[SIGNAL_BROWSER_AUTH_RESP], 0, cookie, code);
			continue;
		}

		GFile* file = g_application_command_line_create_file_for_arg(command_line, argv[i]);
		if (!file)
			continue;

		Event* e = event_new_from_ics_file(g_file_get_path(file));
		if (e) {
			focal_add_event(FOCAL_APP(application), e);
		} else {
			g_application_command_line_print(command_line, "Could not read file %s\n", argv[i]);
			ret = 1;
		}
		g_object_unref(file);
	}
	g_strfreev(argv);
	return ret;
}

static void focal_app_class_init(FocalAppClass* klass)
{
	GObjectClass* goc = G_OBJECT_CLASS(klass);
	focal_app_signals[SIGNAL_BROWSER_AUTH_RESP] = g_signal_new("browser-auth-response", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);
}

static void focal_app_init(FocalApp* focal)
{
}

int main(int argc, char** argv)
{
	int status;
	GtkApplication* app = g_object_new(FOCAL_TYPE_APP, "application-id", "net.ohwg.focal", "flags", G_APPLICATION_HANDLES_COMMAND_LINE, NULL);
	g_signal_connect(app, "startup", G_CALLBACK(focal_startup), NULL);
	g_signal_connect(app, "activate", G_CALLBACK(focal_activate), NULL);
	g_signal_connect(app, "shutdown", G_CALLBACK(focal_shutdown), NULL);
	g_signal_connect(app, "command-line", G_CALLBACK(focal_cmdline), NULL);
	g_application_register(G_APPLICATION(app), NULL, NULL);

	status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);

	return status;
}
