/*
 * main.c
 * This file is part of focal, a calendar application for Linux
 * Copyright 2018-2020 Oliver Giles and focal contributors.
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
#include "app-header.h"
#include "async-curl.h"
#include "calendar-collection.h"
#include "calendar-config.h"
#include "calendar.h"
#include "event-panel.h"
#include "event-popup.h"
#include "event.h"
#include "reminder.h"
#include "week-view.h"

#include <curl/curl.h>
#include <gtk/gtk.h>
#include <libical/ical.h>
#include <string.h>
#include <strings.h>

#define FOCAL_TYPE_APP (focal_app_get_type())
G_DECLARE_FINAL_TYPE(FocalApp, focal_app, FOCAL, APP, GtkApplication)

typedef struct {
	int week_start_day;
	int week_end_day;
	int auto_sync_interval;
} FocalPrefs;

struct _FocalApp {
	GtkApplication parent;
	GtkWidget* mainWindow;
	GtkWidget* header;
	char* path_prefs;
	FocalPrefs prefs;
	char* path_accounts;
	GSList* accounts;
	//GSList* calendars;
	CalendarCollection* calendars;
	GtkWidget* weekView;
	GtkWidget* error_label;
	GtkWidget* infoBar;
	GtkWidget* popover;
	GtkWidget* eventDetail;
	GtkWidget* revealer;
	int running_syncs;
	guint sync_timer_id;
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

static void focal_add_event(FocalApp* focal, Event* ev)
{
	event_each_attendee(ev, match_event_to_calendar, focal->calendars);
	week_view_focus_event(FOCAL_WEEK_VIEW(focal->weekView), ev);
	week_view_add_event(FOCAL_WEEK_VIEW(focal->weekView), ev);
}

static void on_event_modified(EventPanel* event_panel, Event* ev, FocalApp* focal)
{
	// needed in case the time, duration or summary changed
	week_view_refresh(FOCAL_WEEK_VIEW(focal->weekView), ev);
}

static void on_open_event_details(EventPanel* event_panel, Event* ev, FocalApp* focal)
{
	gtk_widget_hide(focal->popover);
	event_panel_set_event(FOCAL_EVENT_PANEL(focal->eventDetail), ev);
	gtk_revealer_set_reveal_child(GTK_REVEALER(focal->revealer), TRUE);
	app_header_set_event(FOCAL_APP_HEADER(focal->header), ev);
}

static void close_details_panel(FocalApp* fa)
{
	gtk_revealer_set_reveal_child(GTK_REVEALER(fa->revealer), FALSE);
	app_header_set_event(FOCAL_APP_HEADER(fa->header), NULL);
}

static void cal_event_selected(WeekView* widget, Event* ev, GdkRectangle* rect, FocalApp* fm)
{
	event_popup_set_event(FOCAL_EVENT_POPUP(fm->popover), ev);
	if (ev) {
		gtk_popover_set_pointing_to(GTK_POPOVER(fm->popover), rect);
		gtk_popover_popup(GTK_POPOVER(fm->popover));
	} else {
		gtk_widget_hide(fm->popover);
		// The selection cannot be changed by the user when the details panel is open,
		// but we get this deselection event from the WeekView when it is notified that
		// the event has been deleted on the remote side. So in case the user has it open
		// while a background sync detects a deletion, close the panel to prevent its use
		// of invalid memory
		close_details_panel(fm);
	}
}

static void toggle_calendar(GSimpleAction* action, GVariant* value, FocalApp* fm)
{
	Calendar* calendar = NULL;
	const char* calendar_name;

	calendar_name = strchr(g_action_get_name(G_ACTION(action)), '.') + 1;
	calendar = calendar_collection_get_by_name(fm->calendars, calendar_name);
	g_assert_nonnull(calendar);

	if (g_variant_get_boolean(value))
		week_view_add_calendar(FOCAL_WEEK_VIEW(fm->weekView), calendar);
	else
		week_view_remove_calendar(FOCAL_WEEK_VIEW(fm->weekView), calendar);

	calendar_collection_set_enabled(fm->calendars, calendar, g_variant_get_boolean(value));
	g_simple_action_set_state(action, value);
}

static void calendar_config_modified(FocalApp* fm, Calendar* cal)
{
	// write config back to file
	calendar_config_write_to_file(fm->path_accounts, fm->accounts);
}

static void calendar_synced(FocalApp* fm)
{
	if (--fm->running_syncs == 0) {
		app_header_set_sync_in_progress(FOCAL_APP_HEADER(fm->header), FALSE);
	}
}
static GMenu* create_menu(FocalApp* fm)
{
	GMenu* menu_main = g_menu_new();
	g_menu_append_section(menu_main, NULL, G_MENU_MODEL(fm->calendars));
	g_menu_append(menu_main, "Accounts", "win.accounts");
	g_menu_append(menu_main, "Preferences", "win.prefs");
	return menu_main;
}

static gboolean do_calendar_sync(FocalApp* fm)
{
	// Don't run if a sync is already in progress
	if (fm->running_syncs > 0) {
		g_info("%s", "Ignoring sync request while sync running");
		return G_SOURCE_CONTINUE;
	}

	app_header_set_sync_in_progress(FOCAL_APP_HEADER(fm->header), TRUE);
	fm->running_syncs = g_menu_model_get_n_items(G_MENU_MODEL(fm->calendars));
	calendar_collection_sync_all(fm->calendars);
	return G_SOURCE_CONTINUE;
}

static void on_accounts_changed(FocalApp* fm)
{
	// write modified config back to file
	calendar_config_write_to_file(fm->path_accounts, fm->accounts);
	// recreate calendars from updated config
	calendar_collection_populate_from_config(fm->calendars, fm->accounts);
}

static void open_accounts_dialog(GSimpleAction* simple, GVariant* parameter, gpointer user_data)
{
	FocalApp* fm = (FocalApp*) user_data;
	GtkWidget* accounts = accounts_dialog_new(GTK_WINDOW(fm->mainWindow), &fm->accounts);
	g_signal_connect_swapped(accounts, "accounts-changed", (GCallback) on_accounts_changed, fm);
	g_signal_connect(accounts, "response", G_CALLBACK(gtk_widget_destroy), NULL);
	gtk_widget_show_all(accounts);
}

static void apply_preferences(FocalApp* fa)
{
	week_view_set_day_span(FOCAL_WEEK_VIEW(fa->weekView), fa->prefs.week_start_day, fa->prefs.week_end_day);
	if (fa->sync_timer_id)
		g_source_remove(fa->sync_timer_id);
	if (fa->prefs.auto_sync_interval) {
		fa->sync_timer_id = g_timeout_add_seconds(fa->prefs.auto_sync_interval, G_SOURCE_FUNC(do_calendar_sync), fa);
		g_source_set_name_by_id(fa->sync_timer_id, "[focal] sync_timer");
	}
}

static void open_prefs_dialog(GSimpleAction* simple, GVariant* parameter, gpointer user_data)
{
	FocalApp* fm = FOCAL_APP(user_data);
	GtkWidget* dialog = gtk_dialog_new_with_buttons("Focal", GTK_WINDOW(fm->mainWindow), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_OK", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);

	GtkWidget* combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "1,7", "Monday-Sunday");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "0,6", "Sunday-Saturday");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "1,5", "Monday-Friday");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "0,4", "Sunday-Thursday");
	gchar* id = g_strdup_printf("%d,%d", fm->prefs.week_start_day, fm->prefs.week_end_day);
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), id);
	g_free(id);

	GtkWidget* combo_autosync = gtk_combo_box_text_new();
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_autosync), "0", "Never");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_autosync), "30", "Every 30 seconds");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_autosync), "120", "Every 2 minutes");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_autosync), "600", "Every 10 minutes");
	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_autosync), "3600", "Every 1 hour");
	id = g_strdup_printf("%d", fm->prefs.auto_sync_interval);
	gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_autosync), id);
	g_free(id);

	GtkWidget* grid = gtk_grid_new();
	g_object_set(grid, "column-spacing", 12, "row-spacing", 9, "margin-bottom", 12, "margin-top", 12, NULL);
	gtk_grid_attach(GTK_GRID(grid), g_object_new(GTK_TYPE_LABEL, "label", "<b>Display</b>", "use-markup", TRUE, "halign", GTK_ALIGN_START, NULL), 0, 0, 2, 1);
	gtk_grid_attach(GTK_GRID(grid), g_object_new(GTK_TYPE_LABEL, "label", "Week span:", "halign", GTK_ALIGN_END, NULL), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), combo, 1, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), g_object_new(GTK_TYPE_LABEL, "label", "Automatic sync:", "halign", GTK_ALIGN_END, NULL), 0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), combo_autosync, 1, 2, 1, 1);

	GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	g_object_set(content, "margin", 6, NULL);
	gtk_container_add(GTK_CONTAINER(content), grid);

	gtk_widget_show_all(dialog);
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
		sscanf(gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo)), "%d,%d", &fm->prefs.week_start_day, &fm->prefs.week_end_day);
		fm->prefs.auto_sync_interval = atoi(gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo_autosync)));
		// save preferences
		GKeyFile* kf = g_key_file_new();
		GError* err = NULL;
		g_key_file_load_from_file(kf, fm->path_prefs, G_KEY_FILE_KEEP_COMMENTS, NULL);
		g_key_file_set_integer(kf, "general", "week_start_day", fm->prefs.week_start_day);
		g_key_file_set_integer(kf, "general", "week_end_day", fm->prefs.week_end_day);
		g_key_file_set_integer(kf, "general", "auto_sync_interval", fm->prefs.auto_sync_interval);
		g_key_file_save_to_file(kf, fm->path_prefs, &err);
		g_key_file_free(kf);

		apply_preferences(fm);
	}
	gtk_widget_destroy(dialog);
}

static void workaround_sync_event_detail_with_week_view(GtkWidget* event_panel, GdkRectangle* allocation)
{
	gtk_widget_set_size_request(event_panel, allocation->width, allocation->height);
}

static void handle_calendar_error(FocalApp* fm, Calendar* cal)
{
	char* msg = calendar_get_error(cal);
	g_assert_nonnull(msg);

	// hacky: save a pointer to the calendar whose error is shown now. This is so that
	// when the warning is dismissed and we check to see whether to display errors from
	// other calendars, we know to exclude this one.
	g_object_set_data((GObject*) fm->infoBar, "from-calendar", cal);

	msg = g_strdup_printf("<b>%s</b>: %s", calendar_get_name(cal), msg);
	gtk_label_set_markup(GTK_LABEL(fm->error_label), msg);
	g_free(msg);
	gtk_widget_show(fm->infoBar);
}

static void handle_info_bar_dismissed(FocalApp* fm)
{
	// hacky, see handle_calendar_error
	Calendar* error_from = g_object_get_data((GObject*) fm->infoBar, "from-calendar");

	// show any errors from other calendars
	calendar_collection_foreach (it, fm->calendars) {
		if (it.cal != error_from && calendar_get_error(it.cal)) {
			handle_calendar_error(fm, it.cal);
			return;
		}
	}
	// otherwise hide the bar
	gtk_widget_hide(fm->infoBar);
}

static void focal_create_main_window(GApplication* app, FocalApp* fm)
{
	fm->mainWindow = gtk_application_window_new(GTK_APPLICATION(app));

	fm->weekView = week_view_new();
	fm->eventDetail = g_object_new(FOCAL_TYPE_EVENT_PANEL, NULL);

	const GActionEntry entries[] = {
		{"accounts", open_accounts_dialog},
		{"prefs", open_prefs_dialog}};

	g_action_map_add_action_entries(G_ACTION_MAP(fm->mainWindow), entries, G_N_ELEMENTS(entries), fm);

	fm->popover = g_object_new(FOCAL_TYPE_EVENT_POPUP, "relative-to", fm->weekView, NULL);
	event_popup_set_calendar_collection(FOCAL_EVENT_POPUP(fm->popover), fm->calendars);
	gtk_popover_set_position(GTK_POPOVER(fm->popover), GTK_POS_RIGHT);
	gtk_widget_show_all(fm->eventDetail);

	gtk_window_set_type_hint((GtkWindow*) fm->mainWindow, GDK_WINDOW_TYPE_HINT_DIALOG);

	g_signal_connect(fm->weekView, "event-selected", (GCallback) &cal_event_selected, fm);
	g_signal_connect(fm->eventDetail, "event-modified", (GCallback) &on_event_modified, fm);
	g_signal_connect(fm->popover, "event-modified", (GCallback) &on_event_modified, fm);
	g_signal_connect(fm->popover, "open-details", (GCallback) &on_open_event_details, fm);

	fm->header = g_object_new(FOCAL_TYPE_APP_HEADER, 0);
	g_signal_connect_swapped(fm->header, "nav-back", (GCallback) &close_details_panel, fm);
	g_signal_connect_swapped(fm->header, "nav-prev", (GCallback) &week_view_goto_previous, fm->weekView);
	g_signal_connect_swapped(fm->header, "nav-current", (GCallback) &week_view_goto_current, fm->weekView);
	g_signal_connect_swapped(fm->header, "nav-next", (GCallback) &week_view_goto_next, fm->weekView);
	g_signal_connect_swapped(fm->header, "sync", (GCallback) &do_calendar_sync, fm);
	g_signal_connect_swapped(fm->header, "request-menu", (GCallback) &create_menu, fm);
	g_signal_connect_swapped(fm->weekView, "date-range-changed", (GCallback) &app_header_calendar_view_changed, fm->header);

	gtk_window_set_titlebar(GTK_WINDOW(fm->mainWindow), fm->header);

	GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	fm->error_label = g_object_new(GTK_TYPE_LABEL, "use-markup", TRUE, "wrap", TRUE, 0);
	fm->infoBar = g_object_new(GTK_TYPE_INFO_BAR, "message-type", GTK_MESSAGE_WARNING, "show-close-button", TRUE, 0);
	g_signal_connect_swapped(fm->infoBar, "response", (GCallback) handle_info_bar_dismissed, fm);

	gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(fm->infoBar))), fm->error_label);

	GtkWidget* overlay = gtk_overlay_new();
	GtkWidget* sw = gtk_scrolled_window_new(NULL, NULL);

	fm->revealer = gtk_revealer_new();
	// The goal is a full-screen slide-in from right. Using the default halign of GTK_ALIGN_FILL,
	// the transition happens immediately. This seems to be at least related to the bug described
	// at https://gitlab.gnome.org/GNOME/gtk/issues/1020. Work around this by aligning the revealer
	// to the end of its parent container, and synchronizing the details panel's width to the week
	// view underneath by monitoring its size-allocate signal
	gtk_revealer_set_transition_type(GTK_REVEALER(fm->revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
	g_object_set(fm->revealer, "halign", GTK_ALIGN_END, NULL);
	g_signal_connect_swapped(fm->weekView, "size-allocate", (GCallback) workaround_sync_event_detail_with_week_view, fm->eventDetail);

	// - GtkWindow
	// `- GtkBox
	//  `- GtkInfoBar
	//  `- GtkOverlay
	//   `- GtkScrolledWindow
	//   |`- WeekView
	//   `- GtkRevealer
	//    `- EventDetails
	gtk_container_add(GTK_CONTAINER(fm->mainWindow), box);
	gtk_container_add(GTK_CONTAINER(box), fm->infoBar);
	gtk_container_add(GTK_CONTAINER(box), overlay);
	gtk_container_add(GTK_CONTAINER(overlay), sw);
	gtk_container_add(GTK_CONTAINER(sw), fm->weekView);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), fm->revealer);
	gtk_container_add(GTK_CONTAINER(fm->revealer), fm->eventDetail);

	// reasonable default size
	gtk_window_set_default_size(GTK_WINDOW(fm->mainWindow), 780, 630);

	apply_preferences(fm);

	// minor hack to force a titlebar update. TODO: move the initial week setting outside of weekview?
	week_view_goto_current(FOCAL_WEEK_VIEW(fm->weekView));

	gtk_widget_show_all(fm->mainWindow);
	gtk_widget_hide(fm->infoBar);

	app_header_set_event(FOCAL_APP_HEADER(fm->header), NULL);

	// todo: better separation of ui from calendar models?
	calendar_collection_populate_from_config(fm->calendars, fm->accounts);
}

static void load_preferences(const char* filename, FocalPrefs* out)
{
	// set defaults
	out->week_start_day = 0;	 // Sunday
	out->week_end_day = 6;		 // Saturday
	out->auto_sync_interval = 0; // Auto-sync disabled

	GKeyFile* kf = g_key_file_new();
	GError* err = NULL;
	g_key_file_load_from_file(kf, filename, G_KEY_FILE_KEEP_COMMENTS, &err);
	if (err) {
		g_critical("%s", err->message);
		g_error_free(err);
		g_key_file_free(kf);
		return;
	}
	out->week_start_day = g_key_file_get_integer(kf, "general", "week_start_day", NULL);
	out->week_end_day = g_key_file_get_integer(kf, "general", "week_end_day", NULL);
	out->auto_sync_interval = g_key_file_get_integer(kf, "general", "auto_sync_interval", NULL);
	g_key_file_free(kf);
}

static void calendar_added(FocalApp* fm, Calendar* cal)
{
	week_view_add_calendar(FOCAL_WEEK_VIEW(fm->weekView), cal);
	char* action_name = g_strdup_printf("toggle-calendar.%s", calendar_get_name(cal));
	GSimpleAction* a = g_simple_action_new_stateful(action_name, NULL, g_variant_new_boolean(TRUE));
	g_signal_connect(a, "change-state", (GCallback) toggle_calendar, fm);
	g_action_map_add_action(G_ACTION_MAP(fm->mainWindow), G_ACTION(a));
	g_free(action_name);

	g_signal_connect_swapped(cal, "error", (GCallback) handle_calendar_error, fm);
	// The initial sync might already have an error
	if (calendar_get_error(cal))
		handle_calendar_error(fm, cal);
}

static void calendar_removed(FocalApp* fm, Calendar* cal)
{
	week_view_remove_calendar(FOCAL_WEEK_VIEW(fm->weekView), cal);
	char* action_name = g_strdup_printf("toggle-calendar.%s", calendar_get_name(cal));
	g_action_map_remove_action(G_ACTION_MAP(fm->mainWindow), action_name);
	g_free(action_name);

	g_signal_handlers_disconnect_by_data(cal, fm);
}

static void focal_startup(GApplication* app)
{
	// needed to generate unique uuids for new events
	g_random_set_seed(time(NULL) * getpid());

	async_curl_init();

	FocalApp* fm = FOCAL_APP(app);

	char* config_dir = g_strdup_printf("%s/focal", g_get_user_config_dir());
	g_mkdir_with_parents(config_dir, 0755);
	fm->path_prefs = g_strdup_printf("%s/prefs.conf", config_dir);
	fm->path_accounts = g_strdup_printf("%s/accounts.conf", config_dir);
	g_free(config_dir);

	fm->accounts = calendar_config_load_from_file(fm->path_accounts);
	load_preferences(fm->path_prefs, &fm->prefs);

	fm->calendars = calendar_collection_new();
	g_signal_connect_swapped(fm->calendars, "calendar-added", (GCallback) calendar_added, fm);
	g_signal_connect_swapped(fm->calendars, "calendar-removed", (GCallback) calendar_removed, fm);
	g_signal_connect_swapped(fm->calendars, "sync-done", (GCallback) calendar_synced, fm);
	g_signal_connect_swapped(fm->calendars, "config-changed", (GCallback) calendar_config_modified, fm);

	reminder_init(fm->calendars);

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

	if (fm->sync_timer_id)
		g_source_remove(fm->sync_timer_id);
	g_object_unref(fm->calendars);
	g_slist_free_full(fm->accounts, (GDestroyNotify) calendar_config_free);
	g_free(fm->path_accounts);
	g_free(fm->path_prefs);
	async_curl_cleanup();
	reminder_cleanup();
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
	focal->sync_timer_id = 0;
	focal->running_syncs = 0;
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
