/*
 * appheader.h
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
#ifndef APPHEADER_H
#define APPHEADER_H

#include <gtk/gtk.h>

#define FOCAL_TYPE_APP_HEADER (app_header_get_type())
G_DECLARE_FINAL_TYPE(AppHeader, app_header, FOCAL, APP_HEADER, GtkHeaderBar)

typedef struct _Event Event;

void app_header_set_event(AppHeader* ah, Event* ev);

void app_header_calendar_view_changed(AppHeader*, int week_number, time_t from, time_t until);

#endif // APPHEADER_H
