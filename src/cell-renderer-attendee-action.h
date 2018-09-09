/*
 * cell-renderer-attendee-action.h
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
#ifndef CELL_RENDERER_ATTENDEE_ACTION_H
#define CELL_RENDERER_ATTENDEE_ACTION_H

#include <gtk/gtk.h>

GtkCellRenderer* cell_renderer_attendee_partstat_new(void);

GtkCellRenderer* cell_renderer_attendee_entry_new(void);

GtkCellRenderer* cell_renderer_attendee_action_new(void);

#endif // CELL_RENDERER_ATTENDEE_ACTION_H
