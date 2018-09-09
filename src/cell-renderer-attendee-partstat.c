/*
 * cell-renderer-attendee-partstat.c
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
#include "cell-renderer-attendee-partstat.h"
#include <libical/ical.h>

#define FOCAL_TYPE_CELL_RENDERER_ATTENDEE_PARTSTAT (cell_renderer_attendee_partstat_get_type())
#define CELL_RENDERER_ATTENDEE_PARTSTAT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), FOCAL_TYPE_CELL_RENDERER_ATTENDEE_PARTSTAT, CellRendererAttendeePartStat))

typedef struct _CellRendererAttendeePartStat CellRendererAttendeePartStat;
typedef struct _CellRendererAttendeePartStatClass CellRendererAttendeePartStatClass;

struct _CellRendererAttendeePartStat {
	GtkCellRendererPixbuf parent;
};

struct _CellRendererAttendeePartStatClass {
	GtkCellRendererPixbufClass parent_class;
};

G_DEFINE_TYPE(CellRendererAttendeePartStat, cell_renderer_attendee_partstat, GTK_TYPE_CELL_RENDERER_PIXBUF)
enum {
	PROP_PARTSTAT_STATUS = 1000
};

GtkCellRenderer* cell_renderer_attendee_partstat_new(void)
{
	return g_object_new(FOCAL_TYPE_CELL_RENDERER_ATTENDEE_PARTSTAT, NULL);
}

static void cell_renderer_attendee_partstat_init(CellRendererAttendeePartStat* cell_renderer)
{
}

static void cell_renderer_attendee_partstat_set_property(GObject* object, guint param_id, const GValue* value, GParamSpec* pspec)
{
	CellRendererAttendeePartStat* crps = CELL_RENDERER_ATTENDEE_PARTSTAT(object);
	if (param_id == PROP_PARTSTAT_STATUS) {
		switch (g_value_get_int(value)) {
		case ICAL_PARTSTAT_ACCEPTED:
			g_object_set(crps, "icon-name", "emblem-ok-symbolic", NULL);
			break;
		case ICAL_PARTSTAT_TENTATIVE:
			g_object_set(crps, "icon-name", "dialog-question-symbolic", NULL);
			break;
		case ICAL_PARTSTAT_DECLINED:
			g_object_set(crps, "icon-name", "dialog-error-symbolic", NULL);
			break;
		default:
			g_object_set(crps, "icon-name", NULL, NULL);
			break;
		}
	} else {
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, param_id, pspec);
	}
}

static void cell_renderer_attendee_partstat_class_init(CellRendererAttendeePartStatClass* class)
{
	GObjectClass* goc = G_OBJECT_CLASS(class);

	goc->set_property = cell_renderer_attendee_partstat_set_property;
	g_object_class_install_property(goc, PROP_PARTSTAT_STATUS, g_param_spec_int("partstat", "Event Attendee participation status", "Integer representing icalparameter_status enum", 0, ICAL_PARTSTAT_NONE, ICAL_PARTSTAT_NONE, G_PARAM_CONSTRUCT | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}
