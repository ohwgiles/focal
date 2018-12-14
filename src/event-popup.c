/*
 * event-popup.c
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
#include "event-popup.h"
#include "calendar.h"
#include "time-spin-button.h"

struct _EventPopup {
	GtkPopover parent;
	GtkWidget* title;
	GtkWidget* starts_at;
	GtkWidget* duration;
	GtkWidget* btn_save;
	GtkWidget* btn_delete;
	Event* selected_event;
};
G_DEFINE_TYPE(EventPopup, event_popup, GTK_TYPE_POPOVER)

enum {
	SIGNAL_EVENT_MODIFIED,
	SIGNAL_OPEN_DETAILS,
	LAST_SIGNAL
};

static guint event_panel_signals[LAST_SIGNAL] = {0};

static void rsvp_yes_clicked(GtkButton* button, gpointer user_data)
{
	EventPopup* ew = FOCAL_EVENT_POPUP(user_data);
	if (event_set_participation_status(ew->selected_event, ICAL_PARTSTAT_ACCEPTED))
		event_save(ew->selected_event);
}

static void rsvp_maybe_clicked(GtkButton* button, gpointer user_data)
{
	EventPopup* ew = FOCAL_EVENT_POPUP(user_data);
	if (event_set_participation_status(ew->selected_event, ICAL_PARTSTAT_TENTATIVE))
		event_save(ew->selected_event);
}

static void rsvp_no_clicked(GtkButton* button, gpointer user_data)
{
	EventPopup* ew = FOCAL_EVENT_POPUP(user_data);
	if (event_set_participation_status(ew->selected_event, ICAL_PARTSTAT_DECLINED))
		event_save(ew->selected_event);
}

static void delete_clicked(GtkButton* button, gpointer user_data)
{
	EventPopup* ew = FOCAL_EVENT_POPUP(user_data);
	// TODO "are you sure?" popup
	calendar_delete_event(FOCAL_CALENDAR(event_get_calendar(ew->selected_event)), ew->selected_event);
}

static void save_clicked(GtkButton* button, gpointer user_data)
{
	EventPopup* ew = FOCAL_EVENT_POPUP(user_data);
	event_save(ew->selected_event);
}

static void open_details(GtkButton* button, gpointer user_data)
{
	EventPopup* ew = FOCAL_EVENT_POPUP(user_data);
	g_signal_emit(ew, event_panel_signals[SIGNAL_OPEN_DETAILS], 0, ew->selected_event);
}

static inline GtkWidget* field_label_new(const char* label)
{
	GtkWidget* lbl = gtk_label_new(label);
	gtk_widget_set_halign(lbl, GTK_ALIGN_START);
	gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
	return lbl;
}

static void event_popup_class_init(EventPopupClass* klass)
{
	event_panel_signals[SIGNAL_EVENT_MODIFIED] = g_signal_new("event-modified", G_TYPE_FROM_CLASS((GObjectClass*) klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
	event_panel_signals[SIGNAL_OPEN_DETAILS] = g_signal_new("open-details", G_TYPE_FROM_CLASS((GObjectClass*) klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void event_popup_init(EventPopup* e)
{
	GtkWidget* box = g_object_new(GTK_TYPE_BOX, "orientation", GTK_ORIENTATION_VERTICAL, NULL);
	GtkWidget* bar = g_object_new(GTK_TYPE_ACTION_BAR, NULL);
	// TODO: is this a good idea? Maybe theme-dependent?
	//gtk_style_context_add_class(gtk_widget_get_style_context((GtkWidget*) bar), GTK_STYLE_CLASS_TITLEBAR);

	e->title = g_object_new(GTK_TYPE_ENTRY, NULL);
	gtk_container_add(GTK_CONTAINER(bar), e->title);

	GtkWidget* grid = gtk_grid_new();
	g_object_set(grid, "margin", 5, NULL);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);

	e->starts_at = g_object_new(FOCAL_TYPE_TIME_SPIN_BUTTON, 0);
	e->duration = g_object_new(FOCAL_TYPE_TIME_SPIN_BUTTON, 0);

	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("@"), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), e->starts_at, 1, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("for"), 2, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), e->duration, 3, 0, 1, 1);

	GtkWidget* actions = g_object_new(GTK_TYPE_ACTION_BAR, NULL);

	GtkWidget* btn;
	btn = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(btn), gtk_image_new_from_icon_name("emblem-ok-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR));
	g_signal_connect(btn, "clicked", (GCallback) &rsvp_yes_clicked, e);
	gtk_action_bar_pack_start(GTK_ACTION_BAR(actions), btn);

	btn = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(btn), gtk_image_new_from_icon_name("dialog-question-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR));
	g_signal_connect(btn, "clicked", (GCallback) &rsvp_maybe_clicked, e);
	gtk_action_bar_pack_start(GTK_ACTION_BAR(actions), btn);

	btn = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(btn), gtk_image_new_from_icon_name("dialog-error-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR));
	g_signal_connect(btn, "clicked", (GCallback) &rsvp_no_clicked, e);
	gtk_action_bar_pack_start(GTK_ACTION_BAR(actions), btn);

	e->btn_delete = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(e->btn_delete), gtk_image_new_from_icon_name("edit-delete-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR));
	g_signal_connect(e->btn_delete, "clicked", (GCallback) &delete_clicked, e);
	gtk_action_bar_pack_end(GTK_ACTION_BAR(actions), e->btn_delete);

	e->btn_save = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(e->btn_save), gtk_image_new_from_icon_name("document-save-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR));
	g_signal_connect(e->btn_save, "clicked", (GCallback) &save_clicked, e);
	gtk_action_bar_pack_end(GTK_ACTION_BAR(actions), e->btn_save);

	btn = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(btn), gtk_image_new_from_icon_name("view-more-horizontal-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR));
	g_signal_connect(btn, "clicked", (GCallback) &open_details, e);
	gtk_action_bar_pack_end(GTK_ACTION_BAR(actions), btn);

	gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), grid, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), actions, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(e), box);
	gtk_widget_show_all(box);
}

static void on_event_title_modified(GtkEntry* entry, EventPopup* ep)
{
	event_set_summary(ep->selected_event, gtk_entry_buffer_get_text(gtk_entry_get_buffer(entry)));
	g_signal_emit(ep, event_panel_signals[SIGNAL_EVENT_MODIFIED], 0, ep->selected_event);
}

static void on_starts_at_modified(GtkSpinButton* starts_at, EventPopup* ep)
{
	icaltimetype dtstart = event_get_dtstart(ep->selected_event);
	int minutes = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(starts_at));
	dtstart.hour = minutes / 60;
	dtstart.minute = minutes % 60;
	event_set_dtstart(ep->selected_event, dtstart);
	g_signal_emit(ep, event_panel_signals[SIGNAL_EVENT_MODIFIED], 0, ep->selected_event);
}

static void on_duration_modified(GtkSpinButton* duration, EventPopup* ep)
{
	icaltimetype dtend = event_get_dtstart(ep->selected_event);
	int minutes = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(duration));
	icaltime_adjust(&dtend, 0, minutes / 60, minutes % 60, 0);
	event_set_dtend(ep->selected_event, dtend);
	g_signal_emit(ep, event_panel_signals[SIGNAL_EVENT_MODIFIED], 0, ep->selected_event);
}

void event_popup_set_event(EventPopup* ew, Event* ev)
{
	g_signal_handlers_disconnect_by_func(ew->title, (gpointer) on_event_title_modified, ew);
	g_signal_handlers_disconnect_by_func(ew->starts_at, (gpointer) on_starts_at_modified, ew);
	g_signal_handlers_disconnect_by_func(ew->duration, (gpointer) on_duration_modified, ew);

	if ((ew->selected_event = ev)) {
		gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(ew->title)), event_get_summary(ev), -1);

		// TODO: timezone conversion
		icaltimetype dt = event_get_dtstart(ev);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(ew->starts_at), dt.minute + dt.hour * 60);

		// TODO: handle very long events
		struct icaldurationtype dur = event_get_duration(ev);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(ew->duration), dur.minutes + dur.hours * 60);

		g_signal_connect(ew->title, "changed", (GCallback) on_event_title_modified, ew);
		g_signal_connect(ew->starts_at, "value-changed", G_CALLBACK(on_starts_at_modified), ew);
		g_signal_connect(ew->duration, "changed", G_CALLBACK(on_duration_modified), ew);

		gboolean editable = !calendar_is_read_only(event_get_calendar(ev));
		gtk_widget_set_sensitive(ew->title, editable);
		gtk_widget_set_sensitive(ew->starts_at, editable);
		gtk_widget_set_sensitive(ew->duration, editable);
		gtk_widget_set_sensitive(ew->btn_save, editable);
		gtk_widget_set_sensitive(ew->btn_delete, editable);
	}
}
