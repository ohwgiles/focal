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
#include <ctype.h>

#include "calendar.h"
#include "cell-renderer-attendee-action.h"
#include "cell-renderer-attendee-partstat.h"
#include "event-panel.h"

struct _EventPanel {
	GtkBox parent;
	GtkWidget* title;
	GtkWidget* starts_at;
	GtkWidget* duration;
	GtkTextBuffer* description;
	GtkWidget* attendees_view;
	GtkListStore* attendees_model;

	Calendar* selected_event_calendar;
	Event* selected_event;
};
G_DEFINE_TYPE(EventPanel, event_panel, GTK_TYPE_BOX)

enum {
	SIGNAL_EVENT_MODIFIED,
	LAST_SIGNAL
};

static guint event_panel_signals[LAST_SIGNAL] = {0};

static void event_panel_class_init(EventPanelClass* klass)
{
	event_panel_signals[SIGNAL_EVENT_MODIFIED] = g_signal_new("event-modified", G_TYPE_FROM_CLASS((GObjectClass*) klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void event_panel_init(EventPanel* self)
{
}

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

static void rsvp_yes_clicked(GtkButton* button, gpointer user_data)
{
	EventPanel* ew = FOCAL_EVENT_PANEL(user_data);
	if (event_set_participation_status(ew->selected_event, ICAL_PARTSTAT_ACCEPTED))
		event_save(ew->selected_event);
}

static void rsvp_maybe_clicked(GtkButton* button, gpointer user_data)
{
	EventPanel* ew = FOCAL_EVENT_PANEL(user_data);
	if (event_set_participation_status(ew->selected_event, ICAL_PARTSTAT_TENTATIVE))
		event_save(ew->selected_event);
}

static void rsvp_no_clicked(GtkButton* button, gpointer user_data)
{
	EventPanel* ew = FOCAL_EVENT_PANEL(user_data);
	if (event_set_participation_status(ew->selected_event, ICAL_PARTSTAT_DECLINED))
		event_save(ew->selected_event);
}

static void delete_clicked(GtkButton* button, gpointer user_data)
{
	EventPanel* ew = FOCAL_EVENT_PANEL(user_data);
	// TODO "are you sure?" popup
	calendar_delete_event(FOCAL_CALENDAR(event_get_calendar(ew->selected_event)), ew->selected_event);
}

static void save_clicked(GtkButton* button, gpointer user_data)
{
	EventPanel* ew = FOCAL_EVENT_PANEL(user_data);
	event_save(ew->selected_event);
}

static inline GtkWidget* field_label_new(const char* label)
{
	GtkWidget* lbl = gtk_label_new(label);
	gtk_widget_set_halign(lbl, GTK_ALIGN_START);
	gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
	return lbl;
}

static gboolean spin_time_on_output(GtkSpinButton* spin, gpointer data)
{
	int value = (int) gtk_adjustment_get_value(gtk_spin_button_get_adjustment(spin));
	char* text = g_strdup_printf("%02d:%02d", value / 60, value % 60);
	gtk_entry_set_text(GTK_ENTRY(spin), text);
	g_free(text);
	return TRUE;
}

gint spin_time_on_input(GtkSpinButton* spin_button, gpointer new_value, gpointer user_data)
{
	const gchar* text = gtk_entry_get_text(GTK_ENTRY(spin_button));
	gint hours, minutes;
	if (sscanf(text, "%d:%d", &hours, &minutes) == 2) {
		*((gdouble*) new_value) = hours * 60 + minutes;
		return TRUE;
	}
	return GTK_INPUT_ERROR;
}

void spin_insert_text(GtkEditable* editable, gchar* text, gint len, gpointer position, gpointer user_data)
{
	char *out, *in;
	for (out = text, in = text; *in;) {
		if (isdigit(*in) || *in == ':')
			*out++ = *in++;
		else
			in++;
	}
	g_signal_handlers_block_by_func((GObject*) editable, (gpointer) &spin_insert_text, user_data);
	gtk_editable_insert_text(editable, text, out - text, position);
	g_signal_handlers_unblock_by_func((GObject*) editable, (gpointer) &spin_insert_text, user_data);
	g_signal_stop_emission_by_name((GObject*) editable, "insert_text");
}

GtkWidget* event_panel_new()
{
	EventPanel* e = g_object_new(FOCAL_TYPE_EVENT_PANEL, "orientation", GTK_ORIENTATION_VERTICAL, NULL);
	GtkWidget* bar = g_object_new(GTK_TYPE_ACTION_BAR, NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context((GtkWidget*) bar), GTK_STYLE_CLASS_TITLEBAR);

	e->title = g_object_new(GTK_TYPE_ENTRY, "hexpand", TRUE, "has-frame", FALSE, NULL);
	gtk_container_add(GTK_CONTAINER(bar), e->title);

	GtkWidget* grid = gtk_grid_new();
	g_object_set(grid, "margin", 5, NULL);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);

	e->starts_at = gtk_spin_button_new_with_range(0, 24 * 60 - 1, 15);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(e->starts_at), FALSE);
	g_object_set(e->starts_at, "width-chars", 5, "max-width-chars", 5, NULL);
	g_signal_connect(e->starts_at, "output", (GCallback) &spin_time_on_output, NULL);
	g_signal_connect(e->starts_at, "input", (GCallback) &spin_time_on_input, NULL);
	g_signal_connect(e->starts_at, "insert-text", (GCallback) &spin_insert_text, NULL);
	gtk_widget_set_halign(e->starts_at, GTK_ALIGN_START);

	e->duration = gtk_spin_button_new_with_range(0, 24 * 60 - 1, 15);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(e->duration), FALSE);
	g_object_set(e->duration, "width-chars", 5, "max-width-chars", 5, NULL);
	g_signal_connect(e->duration, "output", (GCallback) &spin_time_on_output, NULL);
	g_signal_connect(e->duration, "input", (GCallback) &spin_time_on_input, NULL);
	g_signal_connect(e->duration, "insert-text", (GCallback) &spin_insert_text, NULL);
	gtk_widget_set_halign(e->duration, GTK_ALIGN_START);

	GtkWidget* description_scrolled = gtk_scrolled_window_new(0, 0);
	// TODO: infer appropriate height somehow
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(description_scrolled), 150);
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(description_scrolled), 100);
	GtkWidget* description_view = gtk_text_view_new();
	gtk_widget_set_hexpand(description_view, TRUE);
	gtk_container_add(GTK_CONTAINER(description_scrolled), description_view);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(description_view), GTK_WRAP_WORD);
	e->description = gtk_text_view_get_buffer(GTK_TEXT_VIEW(description_view));

	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("@"), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), e->starts_at, 1, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), gtk_label_new("for"), 2, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), e->duration, 3, 0, 1, 1);

	GtkWidget* attendees_scrolled = gtk_scrolled_window_new(0, 0);
	// TODO: infer appropriate height somehow
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(attendees_scrolled), 80);
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(attendees_scrolled), 100);

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

	gtk_grid_attach(GTK_GRID(grid), attendees_scrolled, 0, 1, 4, 1);

	gtk_grid_attach(GTK_GRID(grid), description_scrolled, 0, 2, 4, 1);

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

	btn = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(btn), gtk_image_new_from_icon_name("edit-delete-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR));
	g_signal_connect(btn, "clicked", (GCallback) &delete_clicked, e);
	gtk_action_bar_pack_end(GTK_ACTION_BAR(actions), btn);

	btn = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(btn), gtk_image_new_from_icon_name("document-save-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR));
	g_signal_connect(btn, "clicked", (GCallback) &save_clicked, e);
	gtk_action_bar_pack_end(GTK_ACTION_BAR(actions), btn);

	gtk_box_pack_start(GTK_BOX(e), bar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(e), grid, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(e), actions, TRUE, TRUE, 0);
	return (GtkWidget*) e;
}

static void on_event_title_modified(GtkEntry* entry, EventPanel* ep)
{
	event_set_summary(ep->selected_event, gtk_entry_buffer_get_text(gtk_entry_get_buffer(entry)));
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

static void on_duration_modified(GtkSpinButton* duration, EventPanel* ep)
{
	icaltimetype dtend = event_get_dtstart(ep->selected_event);
	int minutes = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(duration));
	icaltime_adjust(&dtend, 0, minutes / 60, minutes % 60, 0);
	event_set_dtend(ep->selected_event, dtend);
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

void event_panel_set_event(EventPanel* ew, Event* ev)
{
	g_signal_handlers_disconnect_by_func(ew->title, (gpointer) on_event_title_modified, ew);
	g_signal_handlers_disconnect_by_func(ew->starts_at, (gpointer) on_starts_at_modified, ew);
	g_signal_handlers_disconnect_by_func(ew->duration, (gpointer) on_duration_modified, ew);
	g_signal_handlers_disconnect_by_func(ew->description, (gpointer) on_description_modified, ew);

	gtk_list_store_clear(ew->attendees_model);
	if (ev) {
		gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(ew->title)), event_get_summary(ev), -1);

		// TODO: timezone conversion
		icaltimetype dt = event_get_dtstart(ev);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(ew->starts_at), dt.minute + dt.hour * 60);

		// TODO: handle very long events
		struct icaldurationtype dur = event_get_duration(ev);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(ew->duration), dur.minutes + dur.hours * 60);

		gtk_text_buffer_set_text(GTK_TEXT_BUFFER(ew->description), event_get_description(ev), -1);

		populate_attendees(ew->attendees_model, ev);

		g_signal_connect(ew->title, "changed", (GCallback) on_event_title_modified, ew);
		g_signal_connect(ew->starts_at, "changed", G_CALLBACK(on_starts_at_modified), ew);
		g_signal_connect(ew->duration, "changed", G_CALLBACK(on_duration_modified), ew);
		g_signal_connect(ew->description, "changed", G_CALLBACK(on_description_modified), ew);

		// TODO: more radical change to popup if the calendar is read only
		gboolean read_only = calendar_is_read_only(event_get_calendar(ev));
		gtk_widget_set_sensitive(ew->title, !read_only);
		gtk_widget_set_sensitive(ew->starts_at, !read_only);
		gtk_widget_set_sensitive(ew->duration, !read_only);
	}
	ew->selected_event = ev;
}
