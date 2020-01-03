/*
 * event-panel.c
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
#include "calendar.h"
#include "cell-renderer-attendee-action.h"
#include "cell-renderer-attendee-partstat.h"
#include "date-selector-button.h"
#include "time-spin-button.h"

struct _EventPanel {
	GtkBox parent;
	GtkWidget* title;
	GtkWidget* location;
	GtkWidget* all_day;
	GtkWidget* starts_date;
	GtkWidget* starts_time;
	GtkWidget* ends_date;
	GtkWidget* ends_time;
	GtkWidget* reminder;
	GtkTextBuffer* description;
	GtkWidget* attendees_view;
	GtkListStore* attendees_model;

	Event* selected_event;
};
G_DEFINE_TYPE(EventPanel, event_panel, GTK_TYPE_BOX)

enum {
	SIGNAL_EVENT_MODIFIED,
	LAST_SIGNAL
};

static guint event_panel_signals[LAST_SIGNAL] = {0};

static void model_set_attendee(Event* ev, icalproperty* attendee, GtkListStore* model)
{
	GtkTreeIter iter;
	gtk_list_store_append(model, &iter);
	icalparameter* partstat = icalproperty_get_first_parameter(attendee, ICAL_PARTSTAT_PARAMETER);
	icalparameter* cn = icalproperty_get_first_parameter(attendee, ICAL_CN_PARAMETER);
	gtk_list_store_set(model, &iter,
					   0, partstat ? icalparameter_get_partstat(partstat) : ICAL_PARTSTAT_NONE,
					   1, cn ? icalparameter_get_cn(cn) : icalproperty_get_attendee(attendee),
					   2, attendee,
					   3, FALSE,
					   -1);
}

static void populate_attendees(GtkListStore* model, Event* ev)
{
	GtkTreeIter iter;
	event_each_attendee(ev, model_set_attendee, model);
	// empty entry for adding an attendee
	gtk_list_store_append(model, &iter);
	gtk_list_store_set(model, &iter, 3, TRUE, -1);
}

void on_attendee_added(EventPanel* self, gchar* path, gchar* new_text, GtkCellRendererText* cell_renderer)
{
	event_add_attendee(self->selected_event, new_text);
	gtk_list_store_clear(self->attendees_model);
	populate_attendees(self->attendees_model, self->selected_event);
}

static void on_attendee_action(EventPanel* self, icalproperty* attendee)
{
	// a valid attendee means un-invite, NULL means a new one should be added
	if (attendee) {
		event_remove_attendee(self->selected_event, attendee);
		populate_attendees(self->attendees_model, self->selected_event);
	} else {
		// The plus button just focuses the combo box (otherwise it is invisible)
		gint n_rows = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(self->attendees_model), NULL);
		GtkTreePath* path = gtk_tree_path_new_from_indices(n_rows - 1, -1);
		// TODO this doesn't properly focus the combo box editor :(
		gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(self->attendees_view), path, gtk_tree_view_get_column(GTK_TREE_VIEW(self->attendees_view), 1), NULL, TRUE);
		gtk_widget_grab_focus(self->attendees_view);
	}
}

static inline GtkWidget* field_label_new(const char* label)
{
	GtkWidget* lbl = gtk_label_new(label);
	gtk_widget_set_halign(lbl, GTK_ALIGN_START);
	gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
	return lbl;
}

static void on_start_date_changed(DateSelectorButton* dsb, guint day, guint month, guint year, EventPanel* ep)
{
	icaltimetype dtstart = event_get_dtstart(ep->selected_event);
	dtstart.day = day;
	dtstart.month = month;
	dtstart.year = year;
	event_set_dtstart(ep->selected_event, dtstart);
	g_signal_emit(ep, event_panel_signals[SIGNAL_EVENT_MODIFIED], 0, ep->selected_event);
}

static void on_end_date_changed(DateSelectorButton* dsb, guint day, guint month, guint year, EventPanel* ep)
{
	icaltimetype dtend = event_get_dtend(ep->selected_event);
	dtend.day = day;
	dtend.month = month;
	dtend.year = year;
	event_set_dtend(ep->selected_event, dtend);
	g_signal_emit(ep, event_panel_signals[SIGNAL_EVENT_MODIFIED], 0, ep->selected_event);
}

static void event_panel_class_init(EventPanelClass* klass)
{
	event_panel_signals[SIGNAL_EVENT_MODIFIED] = g_signal_new("event-modified", G_TYPE_FROM_CLASS((GObjectClass*) klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void event_panel_init(EventPanel* e)
{
	gtk_style_context_add_class(gtk_widget_get_style_context((GtkWidget*) e), GTK_STYLE_CLASS_BACKGROUND);

	e->title = g_object_new(GTK_TYPE_ENTRY, "hexpand", TRUE, "placeholder-text", "Event Title", NULL);

	GtkWidget* grid = gtk_grid_new();
	g_object_set(grid, "margin", 5, NULL);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);

	e->location = gtk_entry_new();

	e->all_day = gtk_check_button_new_with_label("All day");
	e->starts_date = g_object_new(FOCAL_TYPE_DATE_SELECTOR_BUTTON, NULL);
	e->starts_time = g_object_new(FOCAL_TYPE_TIME_SPIN_BUTTON, 0);
	e->ends_date = g_object_new(FOCAL_TYPE_DATE_SELECTOR_BUTTON, NULL);
	e->ends_time = g_object_new(FOCAL_TYPE_TIME_SPIN_BUTTON, 0);

	GtkListStore* reminder_model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	GtkTreeIter iter;
	gtk_list_store_append(reminder_model, &iter);
	gtk_list_store_set(reminder_model, &iter, 0, "5 minutes before", 1, "-PT5M", -1);
	gtk_list_store_append(reminder_model, &iter);
	gtk_list_store_set(reminder_model, &iter, 0, "10 minutes before", 1, "-PT10M", -1);
	gtk_list_store_append(reminder_model, &iter);
	gtk_list_store_set(reminder_model, &iter, 0, "15 minutes before", 1, "-PT15M", -1);
	gtk_list_store_append(reminder_model, &iter);
	gtk_list_store_set(reminder_model, &iter, 0, "30 minutes before", 1, "-PT30M", -1);
	gtk_list_store_append(reminder_model, &iter);
	gtk_list_store_set(reminder_model, &iter, 0, "1 hour before", 1, "-PT1H", -1);

	e->reminder = gtk_combo_box_text_new();
	gtk_combo_box_set_model(GTK_COMBO_BOX(e->reminder), GTK_TREE_MODEL(reminder_model));
	gtk_combo_box_set_id_column(GTK_COMBO_BOX(e->reminder), 1);

	GtkWidget* description_scrolled = g_object_new(GTK_TYPE_SCROLLED_WINDOW, "expand", TRUE, NULL);
	GtkWidget* description_view = gtk_text_view_new();
	gtk_widget_set_hexpand(description_view, TRUE);
	gtk_container_add(GTK_CONTAINER(description_scrolled), description_view);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(description_view), GTK_WRAP_WORD);
	e->description = gtk_text_view_get_buffer(GTK_TEXT_VIEW(description_view));

	GtkWidget* attendees_scrolled = g_object_new(GTK_TYPE_SCROLLED_WINDOW, "expand", TRUE, NULL);
	e->attendees_model = gtk_list_store_new(4, G_TYPE_INT, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_BOOLEAN);
	e->attendees_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(e->attendees_model));

	GtkCellRenderer* cell_renderer = cell_renderer_attendee_partstat_new();
	gtk_tree_view_append_column(GTK_TREE_VIEW(e->attendees_view), gtk_tree_view_column_new_with_attributes(NULL, cell_renderer, "partstat", 0, NULL));
	cell_renderer = gtk_cell_renderer_combo_new();
	// TODO: set model property with colleague list
	g_object_set(cell_renderer, "text-column", 1, "editable", TRUE, NULL);
	g_signal_connect_swapped(cell_renderer, "edited", (GCallback) on_attendee_added, e);
	GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(NULL, cell_renderer, "text", 1, "has-entry", 3, NULL);
	gtk_tree_view_column_set_expand(col, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(e->attendees_view), col);
	cell_renderer = cell_renderer_attendee_action_new();
	g_signal_connect_swapped(cell_renderer, "activated", (GCallback) on_attendee_action, e);
	gtk_tree_view_append_column(GTK_TREE_VIEW(e->attendees_view), gtk_tree_view_column_new_with_attributes(NULL, cell_renderer, "attendee", 2, NULL));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(e->attendees_view), FALSE);
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(e->attendees_view)), GTK_SELECTION_NONE);

	gtk_container_add(GTK_CONTAINER(attendees_scrolled), e->attendees_view);

	gtk_grid_attach(GTK_GRID(grid), e->title, 0, 0, 3, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Location"), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), e->location, 1, 1, 2, 1);
	gtk_grid_attach(GTK_GRID(grid), e->all_day, 1, 2, 2, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Starts"), 0, 3, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), e->starts_date, 1, 3, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), e->starts_time, 2, 3, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Ends"), 0, 4, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), e->ends_date, 1, 4, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), e->ends_time, 2, 4, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Reminder"), 0, 5, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), e->reminder, 1, 5, 2, 1);
	gtk_grid_attach(GTK_GRID(grid), description_scrolled, 0, 6, 3, 1);

	gtk_grid_attach(GTK_GRID(grid), g_object_new(GTK_TYPE_LABEL, "use-markup", TRUE, "label", "<span size=\"x-large\">Attendees</span>", NULL), 3, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), attendees_scrolled, 3, 1, 1, 6);

	gtk_box_pack_start(GTK_BOX(e), grid, TRUE, TRUE, 0);
}

static void on_event_title_modified(GtkEntry* entry, EventPanel* ep)
{
	event_set_summary(ep->selected_event, gtk_entry_buffer_get_text(gtk_entry_get_buffer(entry)));
	g_signal_emit(ep, event_panel_signals[SIGNAL_EVENT_MODIFIED], 0, ep->selected_event);
}

static void on_location_modified(GtkEntry* entry, EventPanel* ep)
{
	event_set_location(ep->selected_event, gtk_entry_buffer_get_text(gtk_entry_get_buffer(entry)));
	g_signal_emit(ep, event_panel_signals[SIGNAL_EVENT_MODIFIED], 0, ep->selected_event);
}

static void on_all_day_modified(GtkCheckButton* button, EventPanel* ep)
{
	icaltimetype dtstart = event_get_dtstart(ep->selected_event);
	icaltimetype dtend = event_get_dtstart(ep->selected_event);
	dtstart.is_date = dtend.is_date = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
	event_set_dtstart(ep->selected_event, dtstart);
	event_set_dtend(ep->selected_event, dtend);
	g_signal_emit(ep, event_panel_signals[SIGNAL_EVENT_MODIFIED], 0, ep->selected_event);
}

static void on_starts_at_modified(GtkSpinButton* starts_at, EventPanel* ep)
{
	icaltimetype dtstart = event_get_dtstart(ep->selected_event);
	int minutes = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(starts_at));
	dtstart.hour = minutes / 60;
	dtstart.minute = minutes % 60;
	event_set_dtstart(ep->selected_event, dtstart);
	g_signal_emit(ep, event_panel_signals[SIGNAL_EVENT_MODIFIED], 0, ep->selected_event);
}

static void on_ends_at_modified(GtkSpinButton* ends_at, EventPanel* ep)
{
	icaltimetype dtend = event_get_dtstart(ep->selected_event);
	int minutes = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ends_at));
	dtend.hour = minutes / 60;
	dtend.minute = minutes % 60;
	event_set_dtend(ep->selected_event, dtend);
	g_signal_emit(ep, event_panel_signals[SIGNAL_EVENT_MODIFIED], 0, ep->selected_event);
}

static void on_reminder_changed(GtkComboBox* reminder, EventPanel* ep)
{
	const char* id = gtk_combo_box_get_active_id(reminder);
	event_set_alarm_trigger(ep->selected_event, id);
	g_signal_emit(ep, event_panel_signals[SIGNAL_EVENT_MODIFIED], 0, ep->selected_event);
}

static void on_description_modified(GtkTextBuffer* description, EventPanel* ew)
{
	GtkTextIter start, end;
	gtk_text_buffer_get_start_iter(description, &start);
	gtk_text_buffer_get_end_iter(description, &end);
	char* desc = gtk_text_buffer_get_text(description, &start, &end, FALSE);
	event_set_description(ew->selected_event, desc);
	g_free(desc);
	// No signal emitted since the description is not visible in the main view anyway
}

static void event_updated(EventPanel* ep, Event* old_event, Event* new_event, Calendar* cal)
{
	// TODO: maybe notify the user that the event has changed out from underneath them?
	if (old_event == ep->selected_event && new_event) {
		event_panel_set_event(ep, new_event);
	}
}

void event_panel_set_event(EventPanel* ew, Event* ev)
{
	g_signal_handlers_disconnect_by_func(ew->title, (gpointer) on_event_title_modified, ew);
	g_signal_handlers_disconnect_by_func(ew->location, (gpointer) on_location_modified, ew);
	g_signal_handlers_disconnect_by_func(ew->all_day, (gpointer) on_all_day_modified, ew);
	g_signal_handlers_disconnect_by_func(ew->starts_time, (gpointer) on_starts_at_modified, ew);
	g_signal_handlers_disconnect_by_func(ew->ends_time, (gpointer) on_ends_at_modified, ew);
	g_signal_handlers_disconnect_by_func(ew->description, (gpointer) on_description_modified, ew);
	g_signal_handlers_disconnect_by_func(ew->starts_date, (gpointer) on_start_date_changed, ew);
	g_signal_handlers_disconnect_by_func(ew->ends_date, (gpointer) on_end_date_changed, ew);
	g_signal_handlers_disconnect_by_func(ew->reminder, (gpointer) on_reminder_changed, ew);

	gtk_list_store_clear(ew->attendees_model);
	if ((ew->selected_event = ev)) {
		gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(ew->title)), event_get_summary(ev), -1);
		gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(ew->location)), event_get_location(ev), -1);

		// TODO: timezone conversion
		icaltimetype ds = event_get_dtstart(ev), de = event_get_dtend(ev);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(ew->starts_time), ds.minute + ds.hour * 60);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(ew->ends_time), de.minute + de.hour * 60);

		g_object_set(ew->starts_date, "day", ds.day, "month", ds.month - 1, "year", ds.year, NULL);
		g_object_set(ew->ends_date, "day", de.day, "month", de.month - 1, "year", de.year, NULL);

		gtk_combo_box_set_active_id(GTK_COMBO_BOX(ew->reminder), event_get_alarm_trigger(ev));

		gtk_text_buffer_set_text(GTK_TEXT_BUFFER(ew->description), event_get_description(ev), -1);

		populate_attendees(ew->attendees_model, ev);

		g_signal_connect(ew->title, "changed", (GCallback) on_event_title_modified, ew);
		g_signal_connect(ew->location, "changed", G_CALLBACK(on_location_modified), ew);
		g_signal_connect(ew->all_day, "toggled", G_CALLBACK(on_all_day_modified), ew);
		g_signal_connect(ew->starts_time, "value-changed", G_CALLBACK(on_starts_at_modified), ew);
		g_signal_connect(ew->ends_time, "value-changed", G_CALLBACK(on_ends_at_modified), ew);
		g_signal_connect(ew->description, "changed", G_CALLBACK(on_description_modified), ew);
		g_signal_connect(ew->starts_date, "date-changed", G_CALLBACK(on_start_date_changed), ew);
		g_signal_connect(ew->ends_date, "date-changed", G_CALLBACK(on_end_date_changed), ew);
		g_signal_connect(ew->reminder, "changed", G_CALLBACK(on_reminder_changed), ew);

		// TODO: more radical change to popup if the calendar is read only
		gboolean editable = !calendar_is_read_only(event_get_calendar(ev));
		gtk_widget_set_sensitive(ew->title, editable);
		gtk_widget_set_sensitive(ew->starts_date, editable);
		gtk_widget_set_sensitive(ew->ends_date, editable);

		// TODO what if the event doesn't have a calendar yet?
		g_signal_connect_swapped(event_get_calendar(ev), "event-updated", G_CALLBACK(event_updated), ew);
	}
}
