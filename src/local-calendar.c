/*
 * local-calendar.c
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
#include <libical/ical.h>

#include "local-calendar.h"

struct _LocalCalendar {
	Calendar parent;
	char* path;
	icalcomponent* ical;
	GSList* events;
};
G_DEFINE_TYPE(LocalCalendar, local_calendar, TYPE_CALENDAR)

static void write_ical_to_disk(LocalCalendar* lc)
{
	for (GSList* p = lc->events; p; p = p->next)
		icalcomponent_add_component(lc->ical, icalcomponent_new_clone(event_get_component((Event*) p->data)));

	g_file_set_contents(lc->path, icalcomponent_as_ical_string(lc->ical), -1, NULL);
	for (icalcomponent* e = icalcomponent_get_first_component(lc->ical, ICAL_VEVENT_COMPONENT); (e = icalcomponent_get_current_component(lc->ical));) {
		if (icalcomponent_isa(e) == ICAL_VEVENT_COMPONENT) {
			icalcomponent_remove_component(lc->ical, e);
			icalcomponent_free(e);
		}
	}

	g_signal_emit_by_name(lc, "sync-done", 0);
}

static void save_event(Calendar* c, Event* event)
{
	LocalCalendar* lc = FOCAL_LOCAL_CALENDAR(c);

	if (!g_slist_find(lc->events, event))
		lc->events = g_slist_append(lc->events, event);
	write_ical_to_disk(lc);
}

static void delete_event(Calendar* c, Event* event)
{
	LocalCalendar* lc = FOCAL_LOCAL_CALENDAR(c);
	lc->events = g_slist_remove(lc->events, event);
	event_free(event);
	write_ical_to_disk(lc);
}

static void each_event(Calendar* c, CalendarEachEventCallback callback, void* user)
{
	LocalCalendar* lc = FOCAL_LOCAL_CALENDAR(c);
	for (GSList* p = lc->events; p; p = p->next) {
		callback(user, (Event*) p->data);
	}
}

static void free_events(LocalCalendar* lc)
{
	g_slist_free_full(lc->events, (GDestroyNotify) event_free);
	icalcomponent_free(lc->ical);
	lc->events = NULL;
	lc->ical = NULL;
}

static void finalize(GObject* gobject)
{
	LocalCalendar* lc = FOCAL_LOCAL_CALENDAR(gobject);
	free_events(lc);
	g_free(lc->path);
	G_OBJECT_CLASS(local_calendar_parent_class)->finalize(gobject);
}

void local_calendar_init(LocalCalendar* lc)
{
}

static void local_calendar_sync(Calendar* c)
{
	LocalCalendar* lc = FOCAL_LOCAL_CALENDAR(c);
	free_events(lc);
	gchar* contents = NULL;
	GError* err = NULL;

	g_file_get_contents(lc->path, &contents, NULL, &err);
	if (err) {
		fprintf(stderr, "g_file_get_contents: %s\n", err->message);
		g_error_free(err);
		return;
	}

	lc->ical = icalcomponent_new_from_string(contents);
	g_assert_nonnull(lc->ical);
	g_free(contents);
	lc->events = NULL;

	for (icalcomponent* e = icalcomponent_get_first_component(lc->ical, ICAL_VEVENT_COMPONENT); (e = icalcomponent_get_current_component(lc->ical));) {
		if (icalcomponent_isa(e) == ICAL_VEVENT_COMPONENT) {
			Event* ev = event_new_from_icalcomponent(icalcomponent_new_clone(e));
			icalcomponent_remove_component(lc->ical, e);
			icalcomponent_free(e);
			event_set_calendar(ev, c);
			lc->events = g_slist_append(lc->events, ev);
		} else {
			icalcomponent_get_next_component(lc->ical, ICAL_VEVENT_COMPONENT);
		}
	}

	g_signal_emit_by_name(c, "sync-done", 0);
}

void local_calendar_class_init(LocalCalendarClass* klass)
{
	FOCAL_CALENDAR_CLASS(klass)->save_event = save_event;
	FOCAL_CALENDAR_CLASS(klass)->delete_event = delete_event;
	FOCAL_CALENDAR_CLASS(klass)->each_event = each_event;
	FOCAL_CALENDAR_CLASS(klass)->sync = local_calendar_sync;
	G_OBJECT_CLASS(klass)->finalize = finalize;
}

Calendar* local_calendar_new(const char* path)
{
	g_assert_nonnull(path);
	LocalCalendar* lc = g_object_new(LOCAL_CALENDAR_TYPE, NULL);
	lc->path = g_strdup(path);
	return (Calendar*) lc;
}

void local_calendar_free(LocalCalendar* lc)
{
	free_events(lc);
	g_free(lc->path);
	g_free(lc);
}
