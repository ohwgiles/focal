/*
 * cell-renderer-attendee-action.c
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
#include "cell-renderer-attendee-action.h"
#include <libical/ical.h>

#define FOCAL_TYPE_CELL_RENDERER_ATTENDEE_ACTION (cell_renderer_attendee_action_get_type())
#define CELL_RENDERER_ATTENDEE_ACTION(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), FOCAL_TYPE_CELL_RENDERER_ATTENDEE_ACTION, CellRendererAttendeeAction))

typedef struct _CellRendererAttendeeAction CellRendererAttendeeAction;
typedef struct _CellRendererAttendeeActionClass CellRendererAttendeeActionClass;

struct _CellRendererAttendeeAction {
	GtkCellRendererPixbuf parent;
	const icalcomponent* attendee;
};

struct _CellRendererAttendeeActionClass {
	GtkCellRendererPixbufClass parent_class;
};

G_DEFINE_TYPE(CellRendererAttendeeAction, cell_renderer_attendee_action, GTK_TYPE_CELL_RENDERER_PIXBUF)
enum {
	SIGNAL_ACTIVATED,
	NUM_SIGNALS
};
enum {
	PROP_ATTENDEE = 1000
};
static guint attendee_action_signals[NUM_SIGNALS];

GtkCellRenderer* cell_renderer_attendee_action_new(void)
{
	return g_object_new(FOCAL_TYPE_CELL_RENDERER_ATTENDEE_ACTION, NULL);
}

static void cell_renderer_attendee_action_init(CellRendererAttendeeAction* cell_renderer)
{
	g_object_set(cell_renderer, "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE, NULL);
}

static gint cell_renderer_attendee_action_activate(GtkCellRenderer* cell, GdkEvent* event, GtkWidget* widget, const gchar* path, const GdkRectangle* background_area, const GdkRectangle* cell_area, GtkCellRendererState flags)
{
	g_signal_emit(cell, attendee_action_signals[SIGNAL_ACTIVATED], 0, CELL_RENDERER_ATTENDEE_ACTION(cell)->attendee);
	return TRUE;
}

static void cell_renderer_attendee_action_set_property(GObject* object, guint param_id, const GValue* value, GParamSpec* pspec)
{
	CellRendererAttendeeAction* craa = CELL_RENDERER_ATTENDEE_ACTION(object);
	if (param_id == PROP_ATTENDEE) {
		craa->attendee = g_value_get_pointer(value);
		if (craa->attendee) {
			g_object_set(craa, "icon-name", "list-remove-symbolic", NULL);
		} else {
			g_object_set(craa, "icon-name", "list-add-symbolic", NULL);
		}
	} else {

		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, param_id, pspec);
	}
}

static void cell_renderer_attendee_action_class_init(CellRendererAttendeeActionClass* class)
{
	GObjectClass* goc = G_OBJECT_CLASS(class);

	goc->set_property = cell_renderer_attendee_action_set_property;
	g_object_class_install_property(goc, PROP_ATTENDEE, g_param_spec_pointer("attendee", "Event Attendee", "Pointer representing the attendee icalproperty", G_PARAM_CONSTRUCT | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

	GTK_CELL_RENDERER_CLASS(class)->activate = cell_renderer_attendee_action_activate;
	attendee_action_signals[SIGNAL_ACTIVATED] = g_signal_new("activated", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
}
