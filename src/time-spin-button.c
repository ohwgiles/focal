/*
 * time-spin-button.c
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

#include "time-spin-button.h"

#include <ctype.h>

struct _TimeSpinButton {
	GtkSpinButton parent;
};

static GtkEditableInterface* gtk_spin_button_editable_interface = NULL;

// prevent the insertion of non-numerals/colon
static void spin_insert_text(GtkEditable* editable, const gchar* text, gint len, gint* position)
{
	gchar *out = (gchar*) text, *in = (gchar*) text;
	while (*in) {
		if (isdigit(*in) || *in == ':')
			*out++ = *in++;
		else
			in++;
	}
	// pass to GtkSpinButton's implementation of insert_text
	gtk_spin_button_editable_interface->insert_text(editable, text, out - text, position);
}

static void time_spin_button_editable_init(GtkEditableInterface* iface)
{
	gtk_spin_button_editable_interface = g_type_interface_peek_parent(iface);
	iface->insert_text = spin_insert_text;
}

G_DEFINE_TYPE_WITH_CODE(TimeSpinButton, time_spin_button, GTK_TYPE_SPIN_BUTTON, G_IMPLEMENT_INTERFACE(GTK_TYPE_EDITABLE, time_spin_button_editable_init))

static gboolean spin_time_on_output(GtkSpinButton* spin)
{
	int value = (int) gtk_adjustment_get_value(gtk_spin_button_get_adjustment(spin));
	char* text = g_strdup_printf("%02d:%02d", value / 60, value % 60);
	gtk_entry_set_text(GTK_ENTRY(spin), text);
	g_free(text);
	return TRUE;
}

static gint spin_time_on_input(GtkSpinButton* spin_button, gdouble* new_value)
{
	const gchar* text = gtk_entry_get_text(GTK_ENTRY(spin_button));
	gint hours, minutes;
	if (sscanf(text, "%d:%d", &hours, &minutes) == 2) {
		*new_value = hours * 60 + minutes;
		return TRUE;
	}
	return GTK_INPUT_ERROR;
}

void time_spin_button_init(TimeSpinButton* tsb)
{
	gtk_spin_button_set_range(GTK_SPIN_BUTTON(tsb), 0, 24 * 60 - 1);
	gtk_spin_button_set_increments(GTK_SPIN_BUTTON(tsb), 15, 60);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(tsb), FALSE);
	g_object_set(tsb, "width-chars", 5, "max-width-chars", 5, NULL);
}

void time_spin_button_class_init(TimeSpinButtonClass* klass)
{
	GTK_SPIN_BUTTON_CLASS(klass)->input = spin_time_on_input;
	GTK_SPIN_BUTTON_CLASS(klass)->output = spin_time_on_output;
}
