/*
 * event-popup.h
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
#ifndef EVENT_POPUP_H
#define EVENT_POPUP_H

#include <gtk/gtk.h>

#define FOCAL_TYPE_EVENT_POPUP (event_popup_get_type())
G_DECLARE_FINAL_TYPE(EventPopup, event_popup, FOCAL, EVENT_POPUP, GtkPopover)

typedef struct _Event Event;
typedef struct _CalendarCollection CalendarCollection;

void event_popup_set_calendar_collection(EventPopup* ep, CalendarCollection* cc);

void event_popup_set_event(EventPopup* ep, Event* ev);

#endif //EVENT_POPUP_H
