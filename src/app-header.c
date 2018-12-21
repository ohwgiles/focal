/*
 * appheader.c
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

#include "app-header.h"
#include "calendar.h"
#include "event.h"

struct _AppHeader {
	GtkHeaderBar parent;
	GtkWidget* btn_current;
	GtkWidget* btn_next;
	GtkWidget* btn_sync;
	GtkWidget* btn_menu;
	GtkWidget* btn_save;
	GtkWidget* btn_delete;
	int week_number;
	struct {
		time_t from;
		time_t until;
	} display_range;
	Event* event;
};

G_DEFINE_TYPE(AppHeader, app_header, GTK_TYPE_HEADER_BAR)

enum {
	SIGNAL_NAV_BACK,
	SIGNAL_NAV_PREV,
	SIGNAL_NAV_CURRENT,
	SIGNAL_NAV_NEXT,
	SIGNAL_REQUEST_MENU,
	SIGNAL_CALS_SYNC,
	SIGNAL_EVENT_SAVE,
	SIGNAL_EVENT_DELETE,
	LAST_SIGNAL
};

static guint app_header_signals[LAST_SIGNAL] = {0};

static void update_title(AppHeader* ah)
{
	GtkWindow* window = gtk_application_get_window_by_id(GTK_APPLICATION(g_application_get_default()), 1);
	if (ah->event) {
		gtk_window_set_title(window, event_get_summary(ah->event));

	} else {
		char title[8];
		snprintf(title, sizeof(title), "Week %d", ah->week_number);
		gtk_window_set_title(window, title);

		// update header subtitle
		char start[28];
		strftime(start, sizeof(start), "%e. %B %G", localtime(&ah->display_range.from));

		// time of current_view.end is midnight, ensure not to display the following day's date: subtract 1h
		time_t day_end = ah->display_range.until - 3600;
		char end[28];
		strftime(end, sizeof(end), "%e. %B %G", localtime(&day_end));

		char subtitle[64];
		snprintf(subtitle, sizeof(subtitle), "%s – %s", start, end);

		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(ah), subtitle);
	}
}

static void app_header_nav_prev(AppHeader* ah)
{
	g_signal_emit(ah, app_header_signals[ah->event ? SIGNAL_NAV_BACK : SIGNAL_NAV_PREV], 0);
}

static void app_header_nav_current(AppHeader* ah)
{
	g_signal_emit(ah, app_header_signals[SIGNAL_NAV_CURRENT], 0);
}

static void app_header_nav_next(AppHeader* ah)
{
	g_signal_emit(ah, app_header_signals[SIGNAL_NAV_NEXT], 0);
}

static void app_header_menu(AppHeader* ah, GtkWidget* button)
{
	GMenu* menu_main;
	g_signal_emit(ah, app_header_signals[SIGNAL_REQUEST_MENU], 0, &menu_main);

	GtkWidget* menu = gtk_popover_new_from_model(GTK_WIDGET(button), G_MENU_MODEL(menu_main));
	gtk_popover_popdown(GTK_POPOVER(menu));
	gtk_widget_show(menu);
}

static void app_header_sync(AppHeader* ah)
{
	g_signal_emit(ah, app_header_signals[SIGNAL_CALS_SYNC], 0);
}

static void app_header_delete(AppHeader* ah)
{
	// TODO "are you sure?" popup
	calendar_delete_event(FOCAL_CALENDAR(event_get_calendar(ah->event)), ah->event);
}

static void app_header_save(AppHeader* ah)
{
	event_save(ah->event);
}

void app_header_init(AppHeader* ah)
{
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(ah), TRUE);
	GtkWidget* nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(nav), "linked");
	GtkWidget* prev = gtk_button_new();
	ah->btn_current = gtk_button_new_with_label("▼");
	ah->btn_next = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(prev), gtk_image_new_from_icon_name("pan-start-symbolic", GTK_ICON_SIZE_MENU));
	gtk_button_set_image(GTK_BUTTON(ah->btn_next), gtk_image_new_from_icon_name("pan-end-symbolic", GTK_ICON_SIZE_MENU));
	gtk_container_add(GTK_CONTAINER(nav), prev);
	gtk_container_add(GTK_CONTAINER(nav), ah->btn_current);
	gtk_container_add(GTK_CONTAINER(nav), ah->btn_next);
	g_signal_connect_swapped(prev, "clicked", (GCallback) &app_header_nav_prev, ah);
	g_signal_connect_swapped(ah->btn_current, "clicked", (GCallback) &app_header_nav_current, ah);
	g_signal_connect_swapped(ah->btn_next, "clicked", (GCallback) &app_header_nav_next, ah);

	ah->btn_menu = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(ah->btn_menu), gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_MENU));
	g_signal_connect_swapped(ah->btn_menu, "clicked", (GCallback) &app_header_menu, ah);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(ah), ah->btn_menu);

	ah->btn_sync = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(ah->btn_sync), gtk_image_new_from_icon_name("emblem-synchronizing-symbolic", GTK_ICON_SIZE_MENU));
	g_signal_connect_swapped(ah->btn_sync, "clicked", (GCallback) &app_header_sync, ah);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(ah), ah->btn_sync);

	ah->btn_delete = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(ah->btn_delete), gtk_image_new_from_icon_name("edit-delete-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR));
	g_signal_connect_swapped(ah->btn_delete, "clicked", (GCallback) &app_header_delete, ah);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(ah), ah->btn_delete);

	ah->btn_save = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(ah->btn_save), gtk_image_new_from_icon_name("document-save-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR));
	g_signal_connect_swapped(ah->btn_save, "clicked", (GCallback) &app_header_save, ah);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(ah), ah->btn_save);

	gtk_header_bar_pack_start(GTK_HEADER_BAR(ah), nav);
}

void app_header_class_init(AppHeaderClass* klass)
{
	app_header_signals[SIGNAL_NAV_BACK] = g_signal_new("nav-back", FOCAL_TYPE_APP_HEADER, G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	app_header_signals[SIGNAL_NAV_CURRENT] = g_signal_new("nav-current", FOCAL_TYPE_APP_HEADER, G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	app_header_signals[SIGNAL_NAV_PREV] = g_signal_new("nav-prev", FOCAL_TYPE_APP_HEADER, G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	app_header_signals[SIGNAL_NAV_NEXT] = g_signal_new("nav-next", FOCAL_TYPE_APP_HEADER, G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
	app_header_signals[SIGNAL_REQUEST_MENU] = g_signal_new("request-menu", FOCAL_TYPE_APP_HEADER, G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_POINTER, 0);
	app_header_signals[SIGNAL_CALS_SYNC] = g_signal_new("sync", FOCAL_TYPE_APP_HEADER, G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

void app_header_set_event(AppHeader* ah, Event* ev)
{
	if ((ah->event = ev)) {
		// Event mode
		// Show save/delete, title text refers to event
		gtk_widget_hide(ah->btn_current);
		gtk_widget_hide(ah->btn_next);
		gtk_widget_hide(ah->btn_menu);
		gtk_widget_hide(ah->btn_sync);
		gtk_widget_show(ah->btn_save);
		gtk_widget_show(ah->btn_delete);
	} else {
		// Calendar mode
		// Show prev/next week, title text refers to current display
		gtk_widget_show(ah->btn_current);
		gtk_widget_show(ah->btn_next);
		gtk_widget_show(ah->btn_menu);
		gtk_widget_show(ah->btn_sync);
		gtk_widget_hide(ah->btn_save);
		gtk_widget_hide(ah->btn_delete);
	}
	update_title(ah);
}

void app_header_calendar_view_changed(AppHeader* ah, int week_number, time_t from, time_t until)
{
	ah->week_number = week_number;
	ah->display_range.from = from;
	ah->display_range.until = until;
	if (!ah->event)
		update_title(ah);
}
