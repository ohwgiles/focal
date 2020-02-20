/*
 * memory-calendar.c
 * This file is part of focal, a calendar application for Linux
 * Copyright 2018-2020 Oliver Giles and focal contributors.
 *
 * Focal is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Focal is distributed without any explicit or implied warranty.
 * You should have received a copy of the GNU General Public License
 * version 3 with focal. If not, see <http://www.gnu.org/licenses/>.
 */
#include "memory-calendar.h"

struct _MemoryCalendar {
	Calendar parent;
	GHashTable* events;
};
G_DEFINE_TYPE(MemoryCalendar, memory_calendar, TYPE_CALENDAR)

static void save_event(Calendar* c, Event* event)
{
	MemoryCalendar* mc = FOCAL_MEMORY_CALENDAR(c);
	Event* old_event = g_hash_table_lookup(mc->events, event_get_uid(event));

	if (!old_event)
		g_hash_table_insert(mc->events, g_strdup(event_get_uid(event)), event);

	g_signal_emit_by_name(mc, "event-updated", old_event, event);
}

static void delete_event(Calendar* c, Event* event)
{
	MemoryCalendar* mc = FOCAL_MEMORY_CALENDAR(c);
	g_signal_emit_by_name(mc, "event-updated", event, NULL);
	g_hash_table_remove(mc->events, event_get_uid(event)); // calls event_free
}

static void each_event(Calendar* c, CalendarEachEventCallback callback, void* user)
{
	MemoryCalendar* mc = FOCAL_MEMORY_CALENDAR(c);
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init(&iter, mc->events);
	while (g_hash_table_iter_next(&iter, &key, &value))
		callback(user, value);
}

static void sync_noop(Calendar* c)
{
	g_signal_emit_by_name(c, "sync-done", TRUE, 0);
}

static gboolean is_read_only(Calendar* c)
{
	return FALSE;
}

static void finalize(GObject* gobject)
{
	MemoryCalendar* mc = FOCAL_MEMORY_CALENDAR(gobject);
	g_hash_table_destroy(mc->events);
	G_OBJECT_CLASS(memory_calendar_parent_class)->finalize(gobject);
}

void memory_calendar_init(MemoryCalendar* mc)
{
}

void memory_calendar_class_init(MemoryCalendarClass* klass)
{
	FOCAL_CALENDAR_CLASS(klass)->save_event = save_event;
	FOCAL_CALENDAR_CLASS(klass)->delete_event = delete_event;
	FOCAL_CALENDAR_CLASS(klass)->each_event = each_event;
	FOCAL_CALENDAR_CLASS(klass)->sync = sync_noop;
	FOCAL_CALENDAR_CLASS(klass)->read_only = is_read_only;
	G_OBJECT_CLASS(klass)->finalize = finalize;
}

Calendar* memory_calendar_new()
{
	MemoryCalendar* mc = g_object_new(MEMORY_CALENDAR_TYPE, NULL);
	mc->events = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
	g_assert_nonnull(mc->events);

	return (Calendar*) mc;
}
