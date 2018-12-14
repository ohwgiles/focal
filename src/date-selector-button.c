/*
 * date-selector-button.c
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

#include "date-selector-button.h"

struct _DateSelectorButton {
	GtkButton parent;
	GtkWidget* popover;
	GtkWidget* calendar;
	guint day, month, year;
};

static gint signal_date_changed;

enum {
	PROP_0,
	PROP_DAY,
	PROP_MONTH,
	PROP_YEAR
};

G_DEFINE_TYPE(DateSelectorButton, date_selector_button, GTK_TYPE_BUTTON)

static void on_day_selected(DateSelectorButton* dsb)
{
	struct tm t;
	char buffer[48];
	gtk_calendar_get_date(GTK_CALENDAR(dsb->calendar), &t.tm_year, &t.tm_mon, &t.tm_mday);
	g_signal_emit(dsb, signal_date_changed, 0, t.tm_mday, t.tm_mon, t.tm_year);
	t.tm_year -= 1900;
	strftime(buffer, 48, "%d. %B %Y", &t);
	gtk_button_set_label(GTK_BUTTON(dsb), buffer);
}

static void on_clicked(GtkButton* button)
{
	DateSelectorButton* dsb = FOCAL_DATE_SELECTOR_BUTTON(button);
	gtk_widget_show(dsb->calendar);
	gtk_popover_popup(GTK_POPOVER(dsb->popover));
}

void date_selector_button_init(DateSelectorButton* dsb)
{
	dsb->popover = gtk_popover_new(GTK_WIDGET(dsb));
	dsb->calendar = gtk_calendar_new();
	gtk_container_add(GTK_CONTAINER(dsb->popover), dsb->calendar);
	g_signal_connect_swapped(dsb->calendar, "day-selected", G_CALLBACK(on_day_selected), dsb);
}

static void set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
	DateSelectorButton* dsb = FOCAL_DATE_SELECTOR_BUTTON(object);
	switch (prop_id) {
	case PROP_DAY:
		g_object_set_property(G_OBJECT(dsb->calendar), "day", value);
		on_day_selected(dsb);
		break;
	case PROP_MONTH:
		g_object_set_property(G_OBJECT(dsb->calendar), "month", value);
		on_day_selected(dsb);
		//dsb->month = g_value_get_int(value);
		break;
	case PROP_YEAR:
		g_object_set_property(G_OBJECT(dsb->calendar), "year", value);
		on_day_selected(dsb);
		//dsb->year = g_value_get_int(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
	DateSelectorButton* dsb = FOCAL_DATE_SELECTOR_BUTTON(object);
	if (prop_id == PROP_DAY) {
		g_object_get_property(G_OBJECT(dsb->calendar), "day", value);
	} else if (prop_id == PROP_MONTH) {
		g_object_get_property(G_OBJECT(dsb->calendar), "month", value);
	} else if (prop_id == PROP_YEAR) {
		g_object_get_property(G_OBJECT(dsb->calendar), "year", value);
	} else {
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

void date_selector_button_class_init(DateSelectorButtonClass* klass)
{
	GObjectClass* goc = G_OBJECT_CLASS(klass);
	goc->set_property = set_property;
	goc->get_property = get_property;
	g_object_class_install_property(goc, PROP_DAY, g_param_spec_int("day", "day", "", 0, 31, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property(goc, PROP_MONTH, g_param_spec_int("month", "month", "", 0, 11, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property(goc, PROP_YEAR, g_param_spec_int("year", "year", "", 0, G_MAXINT >> 9, 0, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	signal_date_changed = g_signal_new("date-changed", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

	GTK_BUTTON_CLASS(klass)->clicked = on_clicked;
}
