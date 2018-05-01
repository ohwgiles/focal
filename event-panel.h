/*
 * event-panel.h
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
#ifndef EVENT_PANEL_H
#define EVENT_PANEL_H

#include <gtk/gtk.h>
#include <libical/ical.h>

#include "calendar.h"

#define FOCAL_TYPE_EVENT_PANEL (event_panel_get_type())
G_DECLARE_FINAL_TYPE(EventPanel, event_panel, FOCAL, EVENT_PANEL, GtkBox)

GtkWidget* event_panel_new();

void event_panel_set_event(EventPanel* ew, Calendar* cal, CalendarEvent ev);

#endif //EVENT_PANEL_H
