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

typedef struct {
	GtkWidget* layout;
	int width;
	int cheight; // child element height
	int cx, cy;  // coordinates of current child label
} AttendeeLayout;

static void attendee_layout_clear(AttendeeLayout* al)
{
	gtk_container_foreach(GTK_CONTAINER(al->layout), (GtkCallback) gtk_widget_destroy, NULL);
}

static void attendee_layout_add(AttendeeLayout* al, const char* name)
{
	gtk_container_add(GTK_CONTAINER(al->layout), gtk_label_new(name));
}

static void attendee_layout_relayout_child(GtkWidget* child, AttendeeLayout* al)
{
	int cwidth;
	gtk_widget_get_preferred_width_for_height(child, al->cheight, NULL, &cwidth);
	if (al->cx + cwidth > al->width) {
		al->cy += al->cheight;
		al->cx = 0;
	}
	gtk_layout_move(GTK_LAYOUT(al->layout), child, al->cx, al->cy);
	al->cx += cwidth;
}

static void attendee_layout_relayout(GtkWidget* layout, GdkRectangle* allocation, AttendeeLayout* al)
{
	al->cx = 0;
	al->cy = 0;
	al->width = allocation->width;
	gtk_container_foreach(GTK_CONTAINER(layout), (GtkCallback) attendee_layout_relayout_child, al);
}

struct _EventPanel {
	GtkBox parent;
	GtkWidget* event_label;
	GtkWidget* edit_button;
	GtkWidget* delete_button;
	GtkWidget* save_button;
	GtkWidget* cancel_button;

	GtkWidget* starts_at;
	GtkWidget* duration;
	GtkTextBuffer* description;
	AttendeeLayout attendees;

	Calendar* selected_calendar;
	CalendarEvent selected_event;
};
G_DEFINE_TYPE(EventPanel, event_panel, GTK_TYPE_BOX)

enum {
	SIGNAL_EVENT_DELETE,
	LAST_SIGNAL
};

static guint event_panel_signals[LAST_SIGNAL] = {0};

static void event_panel_class_init(EventPanelClass* klass)
{
	event_panel_signals[SIGNAL_EVENT_DELETE] = g_signal_new("cal-event-delete", G_TYPE_FROM_CLASS((GObjectClass*) klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);
}

static void event_panel_init(EventPanel* self)
{
}

static void delete_clicked(GtkButton* button, gpointer user_data)
{
	EventPanel* ew = FOCAL_EVENT_PANEL(user_data);
	g_signal_emit(ew, event_panel_signals[SIGNAL_EVENT_DELETE], 0, ew->selected_calendar, &ew->selected_event);
}

static inline GtkWidget* field_label_new(const char* label)
{
	GtkWidget* lbl = gtk_label_new(label);
	gtk_widget_set_halign(lbl, GTK_ALIGN_START);
	gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
	return lbl;
}

GtkWidget* event_panel_new()
{
	EventPanel* e = g_object_new(FOCAL_TYPE_EVENT_PANEL, "orientation", GTK_ORIENTATION_VERTICAL, NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context((GtkWidget*) e), GTK_STYLE_CLASS_BACKGROUND);
	GtkWidget* bar = g_object_new(GTK_TYPE_ACTION_BAR, NULL);

	e->event_label = gtk_label_new("");
	gtk_action_bar_set_center_widget(GTK_ACTION_BAR(bar), e->event_label);

	e->delete_button = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(e->delete_button), gtk_image_new_from_icon_name("edit-delete", GTK_ICON_SIZE_LARGE_TOOLBAR));
	gtk_action_bar_pack_end(GTK_ACTION_BAR(bar), e->delete_button);

	e->edit_button = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(e->edit_button), gtk_image_new_from_icon_name("gtk-edit", GTK_ICON_SIZE_LARGE_TOOLBAR));
	g_signal_connect(e->delete_button, "clicked", (GCallback) &delete_clicked, e);
	gtk_action_bar_pack_start(GTK_ACTION_BAR(bar), e->edit_button);

	GtkWidget* grid = gtk_grid_new();
	g_object_set(grid, "margin", 5, NULL);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);

	e->starts_at = gtk_label_new("");
	gtk_widget_set_halign(e->starts_at, GTK_ALIGN_START);
	e->duration = gtk_label_new("");
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

	gtk_grid_attach(GTK_GRID(grid), e->starts_at, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), e->duration, 1, 0, 1, 1);

	GtkWidget* attendees_scrolled = gtk_scrolled_window_new(0, 0);
	// TODO: infer appropriate height somehow
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(attendees_scrolled), 80);
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(attendees_scrolled), 100);

	e->attendees.layout = gtk_layout_new(NULL, NULL);
	e->attendees.cheight = 20; // TODO: base on actual widget size
	g_signal_connect(G_OBJECT(e->attendees.layout), "size-allocate", G_CALLBACK(attendee_layout_relayout), &e->attendees);
	gtk_container_add(GTK_CONTAINER(attendees_scrolled), e->attendees.layout);

	gtk_grid_attach(GTK_GRID(grid), attendees_scrolled, 0, 1, 2, 1);

	gtk_grid_attach(GTK_GRID(grid), description_scrolled, 0, 2, 2, 1);

	gtk_box_pack_start(GTK_BOX(e), bar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(e), grid, TRUE, TRUE, 0);
	return (GtkWidget*) e;
}

void event_panel_set_event(EventPanel* ew, Calendar* cal, CalendarEvent ce)
{
	attendee_layout_clear(&ew->attendees);
	if (cal && ce.v) {
		char time_buf[64];
		gtk_label_set_label(GTK_LABEL(ew->event_label), icalcomponent_get_summary(ce.v));

		// TODO: timezone conversion
		icaltimetype dt = icalcomponent_get_dtstart(ce.v);
		snprintf(time_buf, 6, "%02d:%02d", dt.hour, dt.minute);
		gtk_label_set_label(GTK_LABEL(ew->starts_at), time_buf);

		// TODO: handle very long events
		struct icaldurationtype dur = icalcomponent_get_duration(ce.v);
		if (dur.hours > 0) {
			if (dur.minutes > 0)
				snprintf(time_buf, 64, "%d hours %d minutes", dur.hours, dur.minutes);
			else
				snprintf(time_buf, 64, "%d hours", dur.hours);
		} else
			snprintf(time_buf, 64, "%d minutes", dur.minutes);
		gtk_label_set_label(GTK_LABEL(ew->duration), time_buf);

		gtk_text_buffer_set_text(GTK_TEXT_BUFFER(ew->description), icalcomponent_get_description(ce.v), -1);
		for (icalproperty* attendee = icalcomponent_get_first_property(ce.v, ICAL_ATTENDEE_PROPERTY); attendee; attendee = icalcomponent_get_next_property(ce.v, ICAL_ATTENDEE_PROPERTY)) {
			attendee_layout_add(&ew->attendees, icalproperty_get_parameter_as_string(attendee, "CN"));
		}
		gtk_widget_show_all(ew->attendees.layout);
		if (gtk_widget_get_realized(ew->attendees.layout)) {
			GtkAllocation alloc;
			gtk_widget_get_allocation(ew->attendees.layout, &alloc);
			attendee_layout_relayout(ew->attendees.layout, &alloc, &ew->attendees);
		}
		ew->selected_calendar = cal;
		ew->selected_event = ce;
	} else {
		ew->selected_calendar = NULL;
		memset(&ew->selected_event, 0, sizeof(CalendarEvent));
	}
}
