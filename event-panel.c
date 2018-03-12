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

struct _EventPanel {
	GtkBox parent;
	GtkWidget* bar;
	GtkWidget* details_button;
	GtkWidget* event_label;
	GtkWidget* edit_button;
	GtkWidget* delete_button;
	GtkWidget* save_button;
	GtkWidget* cancel_button;

	GtkWidget* details;
	GtkWidget* description;
	GtkWidget* invited;

	icalcomponent* event;
};
G_DEFINE_TYPE(EventPanel, event_panel, GTK_TYPE_VBOX)

enum {
	SIGNAL_EVENT_DELETE,
	LAST_SIGNAL
};

static gint event_panel_signals[LAST_SIGNAL] = {0};

static void event_panel_class_init(EventPanelClass* klass)
{
	event_panel_signals[SIGNAL_EVENT_DELETE] = g_signal_new("cal-event-delete", G_TYPE_FROM_CLASS((GObjectClass*) klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void event_panel_init(EventPanel* self)
{
	gtk_widget_set_halign((GtkWidget*) self, GTK_ALIGN_FILL);
	gtk_widget_set_valign((GtkWidget*) self, GTK_ALIGN_END);
}

static void show_details(GtkToggleButton* togglebutton, gpointer user_data)
{
	EventPanel* e = FOCAL_EVENT_PANEL(user_data);
	gtk_widget_set_visible(e->details, gtk_toggle_button_get_active(togglebutton));
}

static void first_show(GtkWidget* widget, gpointer user_data)
{
	EventPanel* ew = FOCAL_EVENT_PANEL(user_data);
	gtk_widget_hide(ew->details);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ew->details_button), FALSE);
	gtk_widget_hide(ew->bar);
}

static void delete_clicked(GtkButton* button, gpointer user_data)
{
	EventPanel* ew = FOCAL_EVENT_PANEL(user_data);
	g_signal_emit(ew, event_panel_signals[SIGNAL_EVENT_DELETE], 0, ew->event);
}

GtkWidget* event_panel_new()
{
	EventPanel* e = g_object_new(FOCAL_TYPE_EVENT_PANEL, NULL);
	e->bar = g_object_new(GTK_TYPE_ACTION_BAR, NULL);

	e->details_button = gtk_toggle_button_new();
	gtk_button_set_image(GTK_BUTTON(e->details_button), gtk_image_new_from_icon_name("document-properties", GTK_ICON_SIZE_LARGE_TOOLBAR));
	gtk_action_bar_pack_start(GTK_ACTION_BAR(e->bar), e->details_button);
	g_signal_connect(e->details_button, "toggled", (GCallback) &show_details, e);

	e->event_label = gtk_label_new("");
	gtk_action_bar_pack_start(GTK_ACTION_BAR(e->bar), e->event_label);

	e->delete_button = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(e->delete_button), gtk_image_new_from_icon_name("edit-delete", GTK_ICON_SIZE_LARGE_TOOLBAR));
	gtk_action_bar_pack_end(GTK_ACTION_BAR(e->bar), e->delete_button);

	e->edit_button = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(e->edit_button), gtk_image_new_from_icon_name("gtk-edit", GTK_ICON_SIZE_LARGE_TOOLBAR));
	g_signal_connect(e->delete_button, "clicked", (GCallback) &delete_clicked, e);
	gtk_action_bar_pack_end(GTK_ACTION_BAR(e->bar), e->edit_button);

	e->details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget* scroll = gtk_scrolled_window_new(0, 0);
	// TODO
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 400);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 400);
	GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	e->description = gtk_label_new("");
	gtk_label_set_line_wrap(GTK_LABEL(e->description), TRUE);
	gtk_container_add(GTK_CONTAINER(scroll), e->description);
	gtk_box_pack_start(GTK_BOX(left), scroll, 0, 0, 15);
	//    gtk_box_pack_start(GTK_BOX(h), fr, 0, 0, 0);
	//gtk_action_bar_set_center_widget(GTK_ACTION_BAR(e->details), h);

	GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	e->invited = gtk_flow_box_new();

	gtk_box_pack_start(GTK_BOX(right), e->invited, 0, 0, 15);

	//gtk_box_pack_start(GTK_BOX(e->details), left, 0, 0, 15);
	gtk_box_pack_start(GTK_BOX(e->details), right, 0, 0, 15);

	// hack
	//    GtkStyleContext* sc = gtk_widget_get_style_context(e->bar);
	//    GdkRGBA col;
	//    gtk_style_context_get_background_color(sc, GTK_STATE_FLAG_NORMAL, &col);
	//    gtk_widget_override_background_color(e->details, GTK_STATE_FLAG_NORMAL, &col);

	g_signal_connect(GTK_WIDGET(e), "show", (GCallback) &first_show, e);
	gtk_box_pack_start(GTK_BOX(e), e->bar, 0, 0, 0);
	gtk_box_pack_start(GTK_BOX(e), e->details, 0, 0, 0);
	return (GtkWidget*) e;
}

void event_panel_set_event(EventPanel* ew, icalcomponent* ev)
{
	if (ev) {
		gtk_label_set_label(GTK_LABEL(ew->event_label), icalcomponent_get_summary(ev));
		gtk_label_set_label(GTK_LABEL(ew->description), icalcomponent_get_description(ev));
		for (icalproperty* attendee = icalcomponent_get_first_property(ev, ICAL_ATTENDEE_PROPERTY); attendee; attendee = icalcomponent_get_next_property(ev, ICAL_ATTENDEE_PROPERTY)) {
			// TODO: populate attendee
			// icalproperty_get_parameter_as_string(attendees, "CN")
			// gtk_container_add(GTK_CONTAINER(ew->invited), gtk_label_new("test")); //icalproperty_get_parameter_as_string(attendees,"CN")));
		}
		gtk_widget_show(ew->bar);
	} else {
		gtk_widget_hide(ew->details);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ew->details_button), FALSE);
		gtk_widget_hide(ew->bar);
	}
	ew->event = ev;
}
