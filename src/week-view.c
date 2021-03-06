/*
 * week-view.c
 * This file is part of focal, a calendar application for Linux
 * Copyright 2018-2019 Oliver Giles and focal contributors.
 *
 * Focal is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Focal is distributed without any explicit or implied warranty.
 * You should have received a copy of the GNU General Public License
 * version 3 with focal. If not, see <http://www.gnu.org/licenses/>.
 */
#include <ctype.h>
#include <gtk/gtk.h>
#include <libical/ical.h>
#include <stdlib.h>
#include <string.h>

#include "memory-calendar.h"
#include "week-view.h"

struct _EventWidget {
	Event* ev;
	// cached time values for faster drawing
	int minutes_from, minutes_to;
	int new_dayindex; // redundant, but helps performant dragging between days
	// list pointer
	struct _EventWidget* next;
};
typedef struct _EventWidget EventWidget;

struct _WeekView {
	GtkDrawingArea drawing_area;
	int x, y, width, height;
	struct {
		GdkRGBA bg;
		GdkRGBA bg_title_cells;
		GdkRGBA fg;
		GdkRGBA fg_50;
		GdkRGBA header_divider;
		GdkRGBA marker_current_time;
		GdkRGBA fg_current_day;
	} colors;
	double scroll_pos;
	GtkAdjustment* adj;
	GSList* calendars;

	// Array index represents column in week view, which might be Sunday
	// or Monday. Use the dayindex typedef and dayindex_from_icaltime
	// function to avoid mistakes
	EventWidget* events_week[7];
	EventWidget* events_allday[7];
	Event* current_selection; // TODO should probably be an EventWidget or otherwise a specific recurrence

	int shown_week; // 1-based, note libical is 0-based
	int shown_year;
	int weekday_start; // 0-based, note libical is 1-based
	int weekday_end;
	icaltimezone* current_tz;
	icaltime_span current_view;
	struct {
		gboolean within_shown_range;
		int day;
		int weekday; // 0-based, note libical is 1-based
		int minutes;
		int week; // 1-based, note libical is 0-based
		int year;
	} now;
	Calendar* unsaved_events;

	enum { DRAG_ACTION_NONE,
		   DRAG_ACTION_MOVE,
		   DRAG_ACTION_RESIZE } drag_action;
	enum { RESIZE_EDGE_NONE,
		   RESIZE_EDGE_TOP,
		   RESIZE_EDGE_BOTTOM } resize_edge;
	gboolean double_click;
	GdkCursor* resize_cursor;
	EventWidget* hover_event;
	struct {
		double x, y;
	} button_press_origin;
	int button_press_minute_offset;
};

// Documentative typedef representing an index into WeekView.events_week.
// It represents a column in the week view, which may be Sunday or Monday
// depending on user preferences.
typedef int dayindex;

enum {
	SIGNAL_EVENT_SELECTED,
	SIGNAL_DATE_RANGE_CHANGED,
	LAST_SIGNAL
};

static gint week_view_signals[LAST_SIGNAL] = {0};

enum {
	PROP_0,
	PROP_HADJUSTMENT,
	PROP_VADJUSTMENT,
	PROP_HSCROLL_POLICY,
	PROP_VSCROLL_POLICY,
};

#define HEADER_HEIGHT 35.5
#define ALLDAY_HEIGHT 20.0

static gboolean has_all_day(WeekView* wv)
{
	return wv->events_allday[0] || wv->events_allday[1] || wv->events_allday[2] || wv->events_allday[3] || wv->events_allday[4] || wv->events_allday[5] || wv->events_allday[6];
}

// Implemented from GtkScrollable, causes the scroll bar to start below the header
static gboolean get_border(GtkScrollable* scrollable, GtkBorder* border)
{
	border->top = HEADER_HEIGHT + (has_all_day(FOCAL_WEEK_VIEW(scrollable)) ? ALLDAY_HEIGHT : 0);
	return TRUE;
}

static void week_view_scrollable_init(GtkScrollableInterface* iface)
{
	iface->get_border = get_border;
}

G_DEFINE_TYPE_WITH_CODE(WeekView, week_view, GTK_TYPE_DRAWING_AREA, G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, week_view_scrollable_init))

#define SIDEBAR_WIDTH 25.5
#define HALFHOUR_HEIGHT 30.0

static dayindex dayindex_from_icaltime(WeekView* wv, icaltimetype dt)
{
	// icaltime_day_of_week is 1-based, wv->weekday_start is 0-based, but both "start" on Sunday.
	// So if the user configures Monday as the first day of the week, a simple subtraction
	//   icaltime_day_of_week(dt) - ICAL_SUNDAY_WEEKDAY - wv->weekday_start
	// will yield an incorrect (negative) result
	int ical_dow = icaltime_day_of_week(dt);
	g_assert_true(ical_dow >= ICAL_SUNDAY_WEEKDAY && ical_dow <= ICAL_SATURDAY_WEEKDAY);
	return (ical_dow - ICAL_SUNDAY_WEEKDAY - wv->weekday_start + 7) % 7;
}

static void draw_event(WeekView* wv, cairo_t* cr, Event* tmp, PangoLayout* layout, double x, double y, int width, int height)
{
	static GdkRGBA grey = {0.7, 0.7, 0.7, 0.85};

	GdkRGBA* color = event_get_calendar(tmp) == wv->unsaved_events ? &grey : event_get_color(tmp);
	cairo_set_source_rgba(cr, color->red, color->green, color->blue, color->alpha - (event_get_dirty(tmp) ? 0.3 : 0.0));
	cairo_rectangle(cr, x + 1, y + 1, width - 2, height - 2);
	cairo_fill(cr);

	pango_layout_set_width(layout, PANGO_SCALE * (width - 8));
	pango_layout_set_height(layout, PANGO_SCALE * (height - 2));
	pango_layout_set_text(layout, event_get_summary(tmp), -1);

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_move_to(cr, x + 3, y + 1);
	pango_cairo_show_layout(cr, layout);
}

static void week_view_draw(WeekView* wv, cairo_t* cr)
{
	const int num_days = wv->weekday_end - wv->weekday_start + 1;
	const double dashes[] = {1.0};

	cairo_set_line_width(cr, 1.0);

	const int first_visible_halfhour = wv->scroll_pos / HALFHOUR_HEIGHT;
	const int day_width = (double) (wv->width - SIDEBAR_WIDTH) / num_days;
	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

	cairo_set_font_size(cr, 12);

	const double day_begin_yoffset = HEADER_HEIGHT + (has_all_day(wv) ? ALLDAY_HEIGHT : 0);

	// bg of hours legend
	gdk_cairo_set_source_rgba(cr, &wv->colors.bg_title_cells);
	cairo_rectangle(cr, 0, 0, SIDEBAR_WIDTH, wv->height);
	cairo_fill(cr);

	// horizontal hour and half-hour divider lines
	for (int hh = first_visible_halfhour;; ++hh) {
		double y = wv->y + day_begin_yoffset + hh * HALFHOUR_HEIGHT - (int) wv->scroll_pos;
		if (y > wv->y + wv->height)
			break;
		if (hh % 2 == 0) {
			gdk_cairo_set_source_rgba(cr, &wv->colors.fg_50);
			cairo_set_dash(cr, NULL, 0, 0);
			cairo_move_to(cr, wv->x, y);
			cairo_rel_line_to(cr, wv->width, 0);
			cairo_stroke(cr);
			// draw hour labels
			char hour_label[8];
			cairo_move_to(cr, wv->x + 5, y + 13);
			sprintf(hour_label, "%02d", hh / 2);
			gdk_cairo_set_source_rgba(cr, &wv->colors.fg);
			cairo_show_text(cr, hour_label);
		} else {
			gdk_cairo_set_source_rgba(cr, &wv->colors.fg);
			cairo_set_dash(cr, dashes, 1, 0);
			cairo_move_to(cr, wv->x + SIDEBAR_WIDTH, y);
			cairo_rel_line_to(cr, wv->width, 0);
			cairo_stroke(cr);
		}
	}

	PangoLayout* layout = pango_cairo_create_layout(cr);
	pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

	PangoFontDescription* font_desc = pango_font_description_from_string("sans 9");
	pango_layout_set_font_description(layout, font_desc);
	pango_font_description_free(font_desc);

	// draw events
	for (int d = 0; d < num_days; ++d) {
		for (EventWidget* tmp = wv->events_week[d]; tmp; tmp = tmp->next) {
			const double yminutescale = HALFHOUR_HEIGHT / 30.0;
			draw_event(wv, cr, tmp->ev, layout,
					   wv->x + SIDEBAR_WIDTH + tmp->new_dayindex * day_width,
					   tmp->minutes_from * yminutescale + wv->y + day_begin_yoffset - (int) wv->scroll_pos,
					   day_width,
					   (tmp->minutes_to - tmp->minutes_from) * yminutescale);
		}
	}

	// current time indicator line
	if (wv->now.within_shown_range) {
		double nowY = wv->y + day_begin_yoffset + wv->now.minutes * HALFHOUR_HEIGHT / 30 - (int) wv->scroll_pos;
		gdk_cairo_set_source_rgba(cr, &wv->colors.marker_current_time);
		cairo_set_dash(cr, NULL, 0, 0);
		cairo_move_to(cr, wv->x + SIDEBAR_WIDTH + (wv->now.weekday - wv->weekday_start) * day_width, nowY);
		cairo_rel_line_to(cr, day_width, 0);
		cairo_stroke(cr);
	}

	// header bg
	gdk_cairo_set_source_rgba(cr, &wv->colors.bg_title_cells);
	cairo_rectangle(cr, 0, 0, wv->width, day_begin_yoffset);
	cairo_fill(cr);

	// all-day events
	for (int d = 0; d < num_days; ++d) {
		for (EventWidget* tmp = wv->events_allday[d]; tmp; tmp = tmp->next) {
			draw_event(wv, cr, tmp->ev, layout,
					   wv->x + SIDEBAR_WIDTH + tmp->new_dayindex * day_width,
					   wv->y + HEADER_HEIGHT,
					   day_width,
					   ALLDAY_HEIGHT);
		}
	}
	g_object_unref(layout);

	// vertical lines for days
	cairo_set_dash(cr, NULL, 0, 0);

	icaltimetype day = icaltime_from_timet_with_zone(wv->current_view.start, 1, wv->current_tz);
	for (int d = 0; d < num_days; ++d, icaltime_adjust(&day, 1, 0, 0, 0)) {
		double x = wv->x + SIDEBAR_WIDTH + d * day_width;
		char day_label[16];
		time_t tt = icaltime_as_timet(day);
		struct tm* t = localtime(&tt);

		// day of month
		strftime(day_label, 16, "%e", t);
		cairo_move_to(cr, x + 8, wv->y + HEADER_HEIGHT - 14);
		cairo_set_font_size(cr, 14);

		if (wv->now.within_shown_range && day.day == wv->now.day) {
			gdk_cairo_set_source_rgba(cr, &wv->colors.fg_current_day);
		} else {
			gdk_cairo_set_source_rgba(cr, &wv->colors.fg);
		}
		cairo_show_text(cr, day_label);

		// weekday abbreviation
		strftime(day_label, 16, "%a", t);

		for (char* p = day_label; *p; ++p)
			*p = toupper(*p);
		cairo_move_to(cr, x + 30, wv->y + HEADER_HEIGHT - 14);
		cairo_set_font_size(cr, 14);
		cairo_show_text(cr, day_label);

		gdk_cairo_set_source_rgba(cr, &wv->colors.fg_50);
		cairo_move_to(cr, x, wv->y + HEADER_HEIGHT);
		cairo_rel_line_to(cr, 0, wv->height);
		cairo_stroke(cr);

		gdk_cairo_set_source_rgba(cr, &wv->colors.header_divider);
		cairo_move_to(cr, x, wv->y);
		cairo_rel_line_to(cr, 0, HEADER_HEIGHT);
		cairo_stroke(cr);
	}

	// top bar
	gdk_cairo_set_source_rgba(cr, &wv->colors.bg);
	cairo_move_to(cr, wv->x, wv->y + HEADER_HEIGHT);
	cairo_rel_line_to(cr, wv->width, 0);
	cairo_move_to(cr, wv->x, wv->y + day_begin_yoffset);
	cairo_rel_line_to(cr, wv->width, 0);
	cairo_stroke(cr);
}

static gboolean on_draw_event(GtkWidget* widget, cairo_t* cr, gpointer user_data)
{
	WeekView* wv = FOCAL_WEEK_VIEW(widget);
	week_view_draw(wv, cr);
	return FALSE;
}

static int minutes_from_ypos(WeekView* wv, gdouble y)
{
	const double day_begin_yoffset = HEADER_HEIGHT + (has_all_day(wv) ? ALLDAY_HEIGHT : 0);
	return (y - day_begin_yoffset + wv->scroll_pos) * 30 / HALFHOUR_HEIGHT;
}

static gboolean ypos_in_allday_region(WeekView* wv, gdouble y)
{
	if (!has_all_day(wv))
		return FALSE;
	return y < (HEADER_HEIGHT + ALLDAY_HEIGHT);
}

static dayindex dayindex_from_xpos(WeekView* wv, gdouble x)
{
	const int num_days = (wv->weekday_end - wv->weekday_start + 1);
	return num_days * (x - SIDEBAR_WIDTH) / (wv->width - SIDEBAR_WIDTH);
}

static void update_cursor_position(WeekView* wv, gdouble x, gdouble y)
{
	EventWidget* ew = NULL;
	unsigned edge = RESIZE_EDGE_NONE;
	int cursor_minutes = minutes_from_ypos(wv, y);
	dayindex di = dayindex_from_xpos(wv, x);

	if (ypos_in_allday_region(wv, y)) {
		wv->hover_event = wv->events_allday[di];
	} else {
		const int resizeThreshold = 5;
		for (ew = wv->events_week[di]; ew; ew = ew->next) {
			if (abs(ew->minutes_from - cursor_minutes) < resizeThreshold) {
				edge = RESIZE_EDGE_TOP;
				break;
			} else if (abs(ew->minutes_to - cursor_minutes) < resizeThreshold) {
				edge = RESIZE_EDGE_BOTTOM;
				break;
			} else if (ew->minutes_from < cursor_minutes && cursor_minutes < ew->minutes_to) {
				break;
			}
		}

		wv->hover_event = ew;
	}

	// Call gdk_window_set_cursor infrequently.
	if (wv->resize_edge != edge) {
		wv->resize_edge = edge;
		gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(wv)), (ew && wv->resize_edge != RESIZE_EDGE_NONE) ? wv->resize_cursor : NULL);
	}
}

static gboolean on_press_event(GtkWidget* widget, GdkEventButton* event, gpointer data)
{
	WeekView* wv = FOCAL_WEEK_VIEW(widget);
	if (event->button != GDK_BUTTON_PRIMARY)
		return TRUE;

	// We can get a press event without a motion event, for example by selecting an
	// event, moving the cursor while the popup prevents us from receiving motion events,
	// then dismissing the popup and clicking again without moving. So always update
	// the new pointer position when a press is received
	update_cursor_position(wv, event->x, event->y);

	if (event->x < SIDEBAR_WIDTH)
		return TRUE;

	wv->double_click = (event->type == GDK_2BUTTON_PRESS);
	wv->button_press_origin.x = event->x;
	wv->button_press_origin.y = event->y;

	if (wv->hover_event && wv->resize_edge != RESIZE_EDGE_NONE) {
		wv->drag_action = DRAG_ACTION_RESIZE;
		return TRUE;
	}

	if (wv->hover_event && wv->resize_edge == RESIZE_EDGE_NONE) {
		wv->drag_action = DRAG_ACTION_MOVE;
		const double day_begin_yoffset = HEADER_HEIGHT + (has_all_day(wv) ? ALLDAY_HEIGHT : 0);
		wv->button_press_minute_offset = (event->y - day_begin_yoffset + wv->scroll_pos) * 30 / HALFHOUR_HEIGHT - wv->hover_event->minutes_from;
		return TRUE;
	}

	return TRUE;
}

static GdkRectangle rect_from_event_widget(WeekView* wv, EventWidget* ew)
{
	GdkRectangle rect;
	const int num_days = (wv->weekday_end - wv->weekday_start + 1);
	rect.width = (wv->width - SIDEBAR_WIDTH) / num_days;
	rect.x = ew->new_dayindex * rect.width + SIDEBAR_WIDTH;
	if (event_get_dtstart(ew->ev).is_date) { // all-day event
		rect.y = HEADER_HEIGHT;
		rect.height = ALLDAY_HEIGHT;
	} else {
		const double day_begin_yoffset = HEADER_HEIGHT + (has_all_day(wv) ? ALLDAY_HEIGHT : 0);
		rect.y = day_begin_yoffset + (ew->minutes_from - wv->scroll_pos) * HALFHOUR_HEIGHT / 30;
		rect.height = (ew->minutes_to - ew->minutes_from) * HALFHOUR_HEIGHT / 30;
	}
	return rect;
}

static void create_new_event_at_position(WeekView* wv, gdouble x, gdouble y)
{
	GdkRectangle rect;
	icaltimetype dtstart, dtend;

	dayindex di = dayindex_from_xpos(wv, x);
	time_t at = wv->current_view.start + di * 24 * 3600;

	if (ypos_in_allday_region(wv, y)) {
		dtstart = dtend = icaltime_from_timet_with_zone(at, TRUE, wv->current_tz);
	} else {
		double day_begin_yoffset = HEADER_HEIGHT + (has_all_day(wv) ? ALLDAY_HEIGHT : 0);
		int minutes = minutes_from_ypos(wv, y);
		// dtstart: round down to closest quarter-hour
		at += 15 * (minutes / 15) * 60;
		dtstart = dtend = icaltime_from_timet_with_zone(at, FALSE, wv->current_tz);
		// duration: default event is 30min long
		icaltime_adjust(&dtend, 0, 0, 30, 0);
		rect.y = day_begin_yoffset + (dtstart.hour * 60 + dtstart.minute - wv->scroll_pos) * HALFHOUR_HEIGHT / 30;
		rect.height = (dtend.hour * 60 + dtend.minute - dtstart.hour * 60 - dtstart.minute) * HALFHOUR_HEIGHT / 30;
	}
	// https://github.com/libical/libical/blob/master/src/test/timezones.c#L96
	dtstart.zone = dtend.zone = wv->current_tz;

	Event* ev = event_new("New Event", dtstart, dtend, wv->current_tz);

	event_set_calendar(ev, wv->unsaved_events);
	event_save(ev);

	wv->current_selection = ev;
	g_signal_emit(wv, week_view_signals[SIGNAL_EVENT_SELECTED], 0, ev, &rect);
}

static gboolean on_release_event(GtkWidget* widget, GdkEventButton* event, gpointer data)
{
	WeekView* wv = FOCAL_WEEK_VIEW(widget);
	if (event->button != GDK_BUTTON_PRIMARY)
		return TRUE;

	// Handle completion of resize and move operations
	if (wv->drag_action != DRAG_ACTION_NONE) {
		wv->drag_action = DRAG_ACTION_NONE;
		g_assert_nonnull(wv->hover_event);
		EventWidget* ew = wv->hover_event;

		struct icaldurationtype duration = event_get_duration(ew->ev);
		icaltimetype start = event_get_dtstart(ew->ev);
		if (!start.is_date) {
			start.hour = ew->minutes_from / 60;
			start.minute = ew->minutes_from % 60;
			duration.hours = (ew->minutes_to - ew->minutes_from) / 60;
			duration.minutes = (ew->minutes_to - ew->minutes_from) % 60;
		}

		// If the dayindex has changed, find the event in the EventWidget cache and reposition it
		EventWidget** cache = start.is_date ? wv->events_allday : wv->events_week;
		for (int i = 0, n = wv->weekday_end - wv->weekday_start + 1; i < n; ++i) {
			for (EventWidget** p = &cache[i]; *p; p = &(*p)->next) {
				if (*p == ew) {
					if (ew->new_dayindex != i) {
						start.day += (ew->new_dayindex - i);
						*p = (*p)->next;
						ew->next = cache[ew->new_dayindex];
						cache[ew->new_dayindex] = ew;
					}
					break;
				}
			}
		}

		if (icaldurationtype_as_int(duration) != icaldurationtype_as_int(event_get_duration(ew->ev)) || icaltime_compare(start, event_get_dtstart(ew->ev)) != 0) {
			event_set_dtstart(ew->ev, start);
			event_set_dtend(ew->ev, icaltime_add(start, duration));
			gtk_widget_queue_draw((GtkWidget*) wv);
			return TRUE;
		}
	}

	// otherwise, it was a regular click, so check if we should select an event
	EventWidget* ew = wv->hover_event;
	if (ew) {
		GdkRectangle rect = rect_from_event_widget(wv, ew);
		wv->current_selection = ew->ev;
		g_signal_emit(wv, week_view_signals[SIGNAL_EVENT_SELECTED], 0, ew->ev, &rect);
	} else if (wv->double_click) {
		wv->double_click = FALSE;
		// double-click: request to create an event
		if (wv->calendars == NULL) {
			// TODO report error (no calendar configured) via UI. TBD: ask whether to open accounts configuration
			return TRUE;
		}
		create_new_event_at_position(wv, wv->button_press_origin.x, wv->button_press_origin.y);
	} else {
		// deselect
		wv->current_selection = NULL;
		g_signal_emit(wv, week_view_signals[SIGNAL_EVENT_SELECTED], 0, NULL);
	}

	return TRUE;
}

static gboolean on_motion_event(GtkWidget* widget, GdkEventMotion* event, gpointer user)
{
	WeekView* wv = FOCAL_WEEK_VIEW(widget);

	const int num_days = (wv->weekday_end - wv->weekday_start + 1);
	const double day_begin_yoffset = HEADER_HEIGHT + (has_all_day(wv) ? ALLDAY_HEIGHT : 0);

	int minutes = (event->y - day_begin_yoffset + wv->scroll_pos) * 30 / HALFHOUR_HEIGHT;

	if (wv->drag_action == DRAG_ACTION_RESIZE) {
		g_assert_nonnull(wv->hover_event);
		// snap the event to every 15 minutes
		minutes += 8;
		minutes -= minutes % 15;
		// don't let the end time preceed the start time etc. Minimum duration 15min.
		if (wv->resize_edge == RESIZE_EDGE_TOP) {
			if (minutes > wv->hover_event->minutes_to - 15)
				minutes = wv->hover_event->minutes_to - 15;
			wv->hover_event->minutes_from = minutes;
		} else {
			if (minutes < wv->hover_event->minutes_from + 15)
				minutes = wv->hover_event->minutes_from + 15;
			wv->hover_event->minutes_to = minutes;
		}
		gtk_widget_queue_draw(GTK_WIDGET(wv));
		return TRUE;
	} else if (wv->drag_action == DRAG_ACTION_MOVE) {
		g_assert_nonnull(wv->hover_event);
		minutes -= wv->button_press_minute_offset;
		// snap the event to every 15 minutes
		minutes += 8;
		minutes -= minutes % 15;
		// maintain the same distance from the drag point to the end time
		int dur = wv->hover_event->minutes_to - wv->hover_event->minutes_from;
		wv->hover_event->minutes_from = minutes;
		wv->hover_event->minutes_to = minutes + dur;
		wv->hover_event->new_dayindex = num_days * (event->x - SIDEBAR_WIDTH) / (wv->width - SIDEBAR_WIDTH);
		gtk_widget_queue_draw(GTK_WIDGET(wv));
		return TRUE;
	} else {
		update_cursor_position(wv, event->x, event->y);
	}

	// otherwise, check for mouseover collisions
	if (event->x < SIDEBAR_WIDTH)
		return TRUE;

	return TRUE;
}

static void adjustment_changed(GtkAdjustment* adjustment, WeekView* wv)
{
	wv->scroll_pos = gtk_adjustment_get_value(adjustment);
	gtk_widget_queue_draw(GTK_WIDGET(wv));
}

static void set_vadjustment(WeekView* wv, GtkAdjustment* adjustment)
{
	if (!adjustment)
		return;

	// this function should only be called once with a real adjustment
	g_assert_null(wv->adj);

	wv->adj = g_object_ref_sink(adjustment);
	g_signal_connect(adjustment, "value-changed", G_CALLBACK(adjustment_changed), wv);
}

static void set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
	if (prop_id == PROP_HADJUSTMENT) {
		// ignored, horizontal scrolling not supported
	} else if (prop_id == PROP_VADJUSTMENT) {
		set_vadjustment(FOCAL_WEEK_VIEW(object), g_value_get_object(value));
	} else {
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
	if (prop_id == PROP_HADJUSTMENT) {
		// set to NULL, horizontal scrolling not supported
		g_value_set_object(value, NULL);
	} else if (prop_id == PROP_VADJUSTMENT) {
		g_value_set_object(value, FOCAL_WEEK_VIEW(object)->adj);
	} else if (prop_id == PROP_HSCROLL_POLICY || prop_id == PROP_VSCROLL_POLICY) {
		g_value_set_enum(value, GTK_SCROLL_MINIMUM);
	} else {
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

static void week_view_dispose(GObject* gobject)
{
	WeekView* wv = FOCAL_WEEK_VIEW(gobject);
	g_clear_object(&wv->adj);
}

static void clear_all_events(WeekView* wv)
{
	for (int i = 0; i < 7; ++i) {
		for (EventWidget* p = wv->events_week[i]; p;) {
			EventWidget* next = p->next;
			if (p == wv->hover_event)
				wv->hover_event = NULL;
			free(p);
			p = next;
		}
		wv->events_week[i] = NULL;
		for (EventWidget* p = wv->events_allday[i]; p;) {
			EventWidget* next = p->next;
			free(p);
			p = next;
		}
		wv->events_allday[i] = NULL;
	}
}

static void week_view_finalize(GObject* gobject)
{
	WeekView* wv = FOCAL_WEEK_VIEW(gobject);
	icaltimezone_free(wv->current_tz, TRUE);
	g_slist_free(wv->calendars);
	clear_all_events(wv);
	g_object_unref(wv->unsaved_events);
}

static void week_view_class_init(WeekViewClass* klass)
{
	GObjectClass* goc = (GObjectClass*) klass;

	goc->set_property = set_property;
	goc->get_property = get_property;
	goc->dispose = week_view_dispose;
	goc->finalize = week_view_finalize;
	g_object_class_override_property(goc, PROP_HADJUSTMENT, "hadjustment");
	g_object_class_override_property(goc, PROP_VADJUSTMENT, "vadjustment");
	g_object_class_override_property(goc, PROP_HSCROLL_POLICY, "hscroll-policy");
	g_object_class_override_property(goc, PROP_VSCROLL_POLICY, "vscroll-policy");

	week_view_signals[SIGNAL_EVENT_SELECTED] = g_signal_new("event-selected", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);
	week_view_signals[SIGNAL_DATE_RANGE_CHANGED] = g_signal_new("date-range-changed", G_TYPE_FROM_CLASS(goc), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 3, G_TYPE_INT, G_TYPE_INT64, G_TYPE_INT64);
}

static void on_size_allocate(GtkWidget* widget, GdkRectangle* allocation, gpointer user_data)
{
	WeekView* wv = FOCAL_WEEK_VIEW(widget);
	g_assert_nonnull(wv->adj);

	wv->width = allocation->width;
	wv->height = allocation->height;

	gtk_adjustment_configure(wv->adj,
							 wv->scroll_pos,
							 0,
							 24 * 2 * HALFHOUR_HEIGHT + (has_all_day(wv) ? ALLDAY_HEIGHT : 0),
							 0.1 * wv->height,
							 0.9 * wv->height,
							 wv->height);
}

static void on_realize(GtkWidget* widget)
{
	WeekView* wv = FOCAL_WEEK_VIEW(widget);

	GtkStyleContext* sc = gtk_widget_get_style_context(widget);
	GdkRGBA color;
	gtk_style_context_get_color(sc, GTK_STATE_FLAG_NORMAL, &color);
	// TODO make fully generic: retrieve available colors from style context, calculate inbetween values
	if (color.red > 0.5 && color.blue > 0.5 && color.green > 0.5) {
		// dark
		gdk_rgba_parse(&wv->colors.bg, "#444444");
		gdk_rgba_parse(&wv->colors.bg_title_cells, "#333333");
		gdk_rgba_parse(&wv->colors.header_divider, "#666666");
		gdk_rgba_parse(&wv->colors.fg, "#aaaaaa");
		gdk_rgba_parse(&wv->colors.fg_50, "#808080");
		gdk_rgba_parse(&wv->colors.marker_current_time, "#ff8f7e");
		gdk_rgba_parse(&wv->colors.fg_current_day, "#79a8cc");
	} else {
		// light
		gdk_rgba_parse(&wv->colors.bg, "#fafbfc");
		gdk_rgba_parse(&wv->colors.bg_title_cells, "#dadada");
		gdk_rgba_parse(&wv->colors.header_divider, "#b6b6b6");
		gdk_rgba_parse(&wv->colors.fg, "#303030");
		gdk_rgba_parse(&wv->colors.fg_50, "#a6a6a6");
		gdk_rgba_parse(&wv->colors.marker_current_time, "#ff0000");
		// TODO TBD: add bg_current_day(?) to allow e.g. invert or vary fg/bg in current day label cell (not needed in dark display)
		gdk_rgba_parse(&wv->colors.fg_current_day, "#356797");
	}

	wv->resize_cursor = gdk_cursor_new_from_name(gdk_window_get_display(gtk_widget_get_window(widget)), "ns-resize");
}

static void week_view_init(WeekView* wv)
{
	wv->scroll_pos = 410;

	gtk_widget_add_events((GtkWidget*) wv, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

	g_signal_connect(G_OBJECT(wv), "size-allocate", G_CALLBACK(on_size_allocate), NULL);
	g_signal_connect(G_OBJECT(wv), "realize", G_CALLBACK(on_realize), NULL);
	g_signal_connect(G_OBJECT(wv), "draw", G_CALLBACK(on_draw_event), NULL);
	g_signal_connect(G_OBJECT(wv), "button-press-event", G_CALLBACK(on_press_event), NULL);
	g_signal_connect(G_OBJECT(wv), "button-release-event", G_CALLBACK(on_release_event), NULL);
	g_signal_connect(G_OBJECT(wv), "motion-notify-event", G_CALLBACK(on_motion_event), NULL);
}

// Returns the number of weeks per year according to ISO8601
// See https://en.wikipedia.org/wiki/ISO_week_date#Weeks_per_year
static int weeks_in_year_iso8601(int year)
{
	int jan1_dow = icaltime_day_of_week(icaltime_from_day_of_year(1, year));
	if (jan1_dow == ICAL_THURSDAY_WEEKDAY || (jan1_dow == ICAL_WEDNESDAY_WEEKDAY && icaltime_is_leap_year(year)))
		return 53;
	else
		return 52;
}

// Returns the ISO 8601 week number for the given icaltimetype.
// Note that this function is necessary because icaltime_week_number
// does not return the correct values (see #61).
// See https://en.wikipedia.org/wiki/ISO_week_date#Calculating_the_week_number_of_a_given_date
// A complete implementation should check that the returned value is not
// greater than the number of weeks in the given year and wrap it if so, but
// we don't do that because we also need to wrap the year in that case.
static int week_number_iso8601(icaltimetype tt)
{
	int doy = icaltime_day_of_year(tt);
	int dow = (icaltime_day_of_week(tt) - ICAL_MONDAY_WEEKDAY + 7) % 7 + 1;
	int week = (doy - dow + 10) / 7;
	return week;
}

// Returns the ISO 8601 week number we would like to display for the given icaltimetype.
// Or put another way, the ISO 8601 week number of the "week" to which this date "belongs".
// This is NOT necessarily the ISO 8601 week number of the given date. According to the
// standard, week numbers are counted from Monday, but if the widget is configured to display
// Sunday-Saturday and today is Sunday, it is more useful to display the number corresponding
// to Monday-Sunday (actually beginning tomorrow).
static void display_week_year(WeekView* wv, icaltimetype tt, int* out_week, int* out_year)
{
	int week = week_number_iso8601(tt);
	int year = tt.year;

	if (wv->weekday_start == 0 && icaltime_day_of_week(tt) == ICAL_SUNDAY_WEEKDAY)
		week++;

	if (week > weeks_in_year_iso8601(tt.year)) {
		year++;
		week = 1;
	}

	*out_week = week;
	*out_year = year;
}

static void update_current_time(WeekView* wv)
{
	struct icaltimetype today = icaltime_current_time_with_zone(wv->current_tz);

	wv->now.day = today.day;
	wv->now.minutes = 60 * today.hour + today.minute;
	wv->now.weekday = icaltime_day_of_week(today) - ICAL_SUNDAY_WEEKDAY;
	display_week_year(wv, today, &wv->now.week, &wv->now.year);
	//if(wv->now.week > weeks_in_year_iso8601(wv->now.year)) {
	//	wv->now.week = 1;
	//	wv->now.year++;
	//}
}

static gboolean timer_update_current_time(gpointer user_data)
{
	update_current_time(FOCAL_WEEK_VIEW(user_data));
	gtk_widget_queue_draw(GTK_WIDGET(user_data));
	return TRUE;
}

// update the displayed date range based on current week and year
void update_view_span(WeekView* wv)
{
	// based on algorithm from https://en.wikipedia.org/wiki/ISO_week_date
	int span_year_begin = wv->shown_year;
	int wd_4jan = icaltime_day_of_week(icaltime_from_day_of_year(4, span_year_begin));
	int tmp = wv->shown_week * 7 + wv->weekday_start - (wd_4jan + 2); // First day of week
	if (tmp < 1) {
		tmp += icaltime_days_in_year(--span_year_begin);
	} else if (tmp > icaltime_days_in_year(wv->shown_year)) {
		// Should be impossible to arrive here?
		tmp -= icaltime_days_in_year(span_year_begin++);
	}
	icaltimetype start = icaltime_from_day_of_year(tmp, span_year_begin);
	start.hour = 0;
	start.minute = 0;
	start.second = 0;
	start.is_date = FALSE;
	start.zone = wv->current_tz;
	icaltimetype until = start;
	icaltime_adjust(&until, wv->weekday_end - wv->weekday_start + 1, 0, 0, 0);
	wv->current_view = icaltime_span_new(start, until, 0);
}

static void week_view_notify_date_range_changed(WeekView* wv)
{
	g_signal_emit(wv, week_view_signals[SIGNAL_DATE_RANGE_CHANGED], 0, wv->shown_week, wv->current_view.start, wv->current_view.end);
}

GtkWidget* week_view_new(void)
{
	WeekView* cw = g_object_new(FOCAL_TYPE_WEEK_VIEW, NULL);

	char* zoneinfo_link = realpath("/etc/localtime", NULL);
	cw->current_tz = icaltimezone_get_builtin_timezone(zoneinfo_link + strlen("/usr/share/zoneinfo/"));
	free(zoneinfo_link);

	update_current_time(cw);

	// initially show current week of current year
	cw->shown_week = cw->now.week;
	cw->shown_year = cw->now.year;
	update_view_span(cw);

	cw->now.within_shown_range = TRUE;
	g_timeout_add_seconds(120, &timer_update_current_time, cw);

	cw->current_selection = NULL;

	cw->unsaved_events = memory_calendar_new();
	week_view_add_calendar(cw, cw->unsaved_events);

	return (GtkWidget*) cw;
}

static void event_widget_set_extents(EventWidget* w, icaltimetype start, struct icaldurationtype dur)
{
	w->minutes_from = start.hour * 60 + start.minute;
	w->minutes_to = (start.hour + dur.hours) * 60 + start.minute + dur.minutes;
}

static void add_event_occurrence(Event* ev, icaltimetype next, struct icaldurationtype duration, gpointer user)
{
	WeekView* wv = FOCAL_WEEK_VIEW(user);

	dayindex di = dayindex_from_icaltime(wv, next);
	EventWidget* w = (EventWidget*) malloc(sizeof(EventWidget));
	w->ev = ev;
	if (next.is_date) {
		w->next = wv->events_allday[di];
		w->new_dayindex = di;
		wv->events_allday[di] = w;
	} else {
		event_widget_set_extents(w, next, duration);
		w->next = wv->events_week[di];
		w->new_dayindex = di;
		wv->events_week[di] = w;
	}
}

static void add_event_from_calendar(gpointer user_data, Event* ev)
{
	WeekView* cw = FOCAL_WEEK_VIEW(user_data);
	event_each_recurrence(ev, cw->current_tz, cw->current_view, add_event_occurrence, cw);
}

void week_view_add_event(WeekView* wv, Event* vevent)
{
	// TODO check if there is no owning calendar and make it unsaved if so
	add_event_from_calendar(wv, vevent);
	gtk_widget_queue_draw((GtkWidget*) wv);
}

void week_view_remove_event(WeekView* wv, Event* ev)
{
	icaltimetype dtstart = event_get_dtstart(ev);
	const icaltimezone* tz = icaltime_get_timezone(dtstart);
	// convert to local time
	icaltimezone_convert_time(&dtstart, (icaltimezone*) tz, wv->current_tz);
	dayindex di = dayindex_from_icaltime(wv, dtstart);

	if (wv->current_selection == ev) {
		wv->current_selection = NULL;
		g_signal_emit(wv, week_view_signals[SIGNAL_EVENT_SELECTED], 0, NULL);
	}

	EventWidget** ll = dtstart.is_date ? &wv->events_allday[di] : &wv->events_week[di];
	for (EventWidget** ew = ll; *ew; ew = &(*ew)->next) {
		if ((*ew)->ev == ev) {
			EventWidget* next = (*ew)->next;
			if (wv->hover_event == *ew)
				wv->hover_event = NULL;
			free(*ew);
			*ew = next;
			break;
		}
	}

	gtk_widget_queue_draw((GtkWidget*) wv);
}

static void calendar_event_updated(WeekView* wv, Event* old_event, Event* new_event, Calendar* cal)
{
	// all references to old_event are about to become invalid
	if (old_event) {
		week_view_remove_event(wv, old_event);
	}
	if (new_event) {
		week_view_add_event(wv, new_event);
	}
}

int week_view_get_week(WeekView* wv)
{
	return wv->shown_week;
}

icaltime_span week_view_get_current_view(WeekView* wv)
{
	return wv->current_view;
}

static void week_view_populate_view(WeekView* wv)
{
	clear_all_events(wv);
	update_view_span(wv);

	time_t now = time(NULL);
	icaltime_span icalnow = {now, now, FALSE};
	wv->now.within_shown_range = icaltime_span_contains(&icalnow, &wv->current_view);

	for (GSList* p = wv->calendars; p; p = p->next)
		calendar_each_event(FOCAL_CALENDAR(p->data), add_event_from_calendar, wv);

	gtk_widget_queue_draw((GtkWidget*) wv);
}

void week_view_add_calendar(WeekView* wv, Calendar* cal)
{
	wv->calendars = g_slist_append(wv->calendars, cal);
	g_signal_connect_swapped(cal, "event-updated", G_CALLBACK(calendar_event_updated), wv);
	calendar_each_event(cal, add_event_from_calendar, wv);
	//week_view_populate_view(wv);
	gtk_widget_queue_draw((GtkWidget*) wv);
	calendar_sync_date_range(cal, wv->current_view);
}

void week_view_remove_calendar(WeekView* wv, Calendar* cal)
{
	g_signal_handlers_disconnect_by_data(cal, wv);
	wv->calendars = g_slist_remove(wv->calendars, cal);
	week_view_populate_view(wv);
}

void week_view_goto_previous(WeekView* wv)
{
	if (--wv->shown_week == 0)
		wv->shown_week = weeks_in_year_iso8601(--wv->shown_year);
	week_view_populate_view(wv);
	for (GSList* p = wv->calendars; p; p = p->next)
		calendar_sync_date_range(FOCAL_CALENDAR(p->data), wv->current_view);
	week_view_notify_date_range_changed(wv);
}

void week_view_goto_current(WeekView* wv)
{
	wv->shown_week = wv->now.week;
	wv->shown_year = wv->now.year;
	week_view_populate_view(wv);
	for (GSList* p = wv->calendars; p; p = p->next)
		calendar_sync_date_range(FOCAL_CALENDAR(p->data), wv->current_view);
	week_view_notify_date_range_changed(wv);
}

void week_view_goto_next(WeekView* wv)
{
	wv->shown_week = wv->shown_week % weeks_in_year_iso8601(wv->shown_year) + 1;
	if (wv->shown_week == 1)
		wv->shown_year++;
	week_view_populate_view(wv);
	for (GSList* p = wv->calendars; p; p = p->next)
		calendar_sync_date_range(FOCAL_CALENDAR(p->data), wv->current_view);
	week_view_notify_date_range_changed(wv);
}

void week_view_refresh(WeekView* wv, Event* ev)
{
	// TODO: this implementation is a bit brittle. It doesn't handle events changing from
	// normal events to all-day events or vice versa. Use with caution.

	// find corresponding EventWidget(s), there may be many if it's a recurring event
	EventWidget** ll = event_get_dtstart(ev).is_date ? wv->events_allday : wv->events_week;
	for (int i = 0; i < 7; ++i) {
		for (EventWidget** ew = &ll[i]; *ew; ew = &(*ew)->next) {
			if ((*ew)->ev == ev) {
				// although dtstart may refer to a completely different day in the case
				// of a recurring event, here we assume the hour/minute is consistent and
				// so no need to go through all the recurrence rules again
				icaltimetype dtstart = event_get_dtstart(ev);
				struct icaldurationtype duration = event_get_duration(ev);
				event_widget_set_extents(*ew, dtstart, duration);
			}
		}
	}
	gtk_widget_queue_draw((GtkWidget*) wv);
}

void week_view_focus_event(WeekView* wv, Event* event)
{
	// The event might not be in the current view
	icaltimetype dt = event_get_dtstart(event);
	icaltimetype et = event_get_dtend(event);
	display_week_year(wv, dt, &wv->shown_week, &wv->shown_year);
	week_view_populate_view(wv);
	week_view_notify_date_range_changed(wv);

	GdkRectangle rect;
	rect.width = (wv->width - SIDEBAR_WIDTH) / (wv->weekday_end - wv->weekday_start + 1);
	rect.x = (icaltime_day_of_week(dt) - ICAL_SUNDAY_WEEKDAY - wv->weekday_start) * rect.width + SIDEBAR_WIDTH;
	rect.y = HEADER_HEIGHT + (dt.hour * 60 + dt.minute - wv->scroll_pos) * HALFHOUR_HEIGHT / 30;
	rect.height = (et.hour * 60 + et.minute - dt.hour * 60 - dt.minute) * HALFHOUR_HEIGHT / 30;
	g_signal_emit(wv, week_view_signals[SIGNAL_EVENT_SELECTED], 0, event, &rect);
}

void week_view_set_day_span(WeekView* wv, int weekday_start, int weekday_end)
{
	wv->weekday_start = weekday_start;
	wv->weekday_end = weekday_end;
	update_view_span(wv);
	week_view_notify_date_range_changed(wv);
}
