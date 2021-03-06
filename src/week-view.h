/*
 * week-view.h
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
#ifndef WEEK_VIEW_H
#define WEEK_VIEW_H

#include <gtk/gtk.h>

#define FOCAL_TYPE_WEEK_VIEW (week_view_get_type())
G_DECLARE_FINAL_TYPE(WeekView, week_view, FOCAL, WEEK_VIEW, GtkDrawingArea)

typedef struct _Event Event;
typedef struct _Calendar Calendar;
typedef struct _CalendarCollection CalendarCollection;

GtkWidget* week_view_new(void);

void week_view_add_event(WeekView* wv, Event* vevent);
void week_view_remove_event(WeekView* wv, Event* vevent);
void week_view_focus_event(WeekView* wv, Event* event);
void week_view_add_calendar(WeekView* widget, Calendar* cal);
void week_view_remove_calendar(WeekView* wv, Calendar* cal);
int week_view_get_week(WeekView* wv);
void week_view_goto_previous(WeekView* wv);
void week_view_goto_current(WeekView* wv);
void week_view_goto_next(WeekView* wv);
void week_view_refresh(WeekView* wv, Event* ev);
void week_view_set_day_span(WeekView* wv, int weekday_start, int weekday_end);

#endif // WEEK_VIEW_H
