/*
 * ics-calendar.c
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
#include <libical/ical.h>
#include <string.h>

#include "ics-calendar.h"

struct _IcsCalendar {
	Calendar parent;
	char* path;
	GFile* file;
	icalcomponent* ical;
	GHashTable* events;
};
G_DEFINE_TYPE(IcsCalendar, ics_calendar, TYPE_CALENDAR)

#define READ_CHUNK_SIZE 4096

static void copy_to_vcalendar(gpointer key, gpointer value, gpointer user_data)
{
	icalcomponent_add_component((icalcomponent*) user_data, icalcomponent_new_clone(event_get_component((Event*) value)));
}

static void write_ical_to_disk(IcsCalendar* ic)
{
	g_hash_table_foreach(ic->events, copy_to_vcalendar, ic->ical);

	char* ics = icalcomponent_as_ical_string(ic->ical);
	GError* err = NULL;
	// TODO: asynchronously
	g_file_replace_contents(ic->file, ics, strlen(ics), NULL, TRUE, G_FILE_CREATE_NONE, NULL, NULL, &err);
	g_free(ics);

	for (icalcomponent* e = icalcomponent_get_first_component(ic->ical, ICAL_VEVENT_COMPONENT); (e = icalcomponent_get_current_component(ic->ical));) {
		if (icalcomponent_isa(e) == ICAL_VEVENT_COMPONENT) {
			icalcomponent_remove_component(ic->ical, e);
			icalcomponent_free(e);
		}
	}

	if (err) {
		g_critical("Failed to save to %s: %s", ic->path, err->message);
		g_error_free(err);
	} else {
		g_signal_emit_by_name(ic, "sync-done", TRUE, 0);
	}
}

static void save_event(Calendar* c, Event* event)
{
	IcsCalendar* lc = FOCAL_ICS_CALENDAR(c);
	Event* old_event = g_hash_table_lookup(lc->events, event_get_uid(event));

	if (!old_event)
		g_hash_table_insert(lc->events, g_strdup(event_get_uid(event)), event);

	g_signal_emit_by_name(lc, "event-updated", old_event, event);

	write_ical_to_disk(lc);
}

static void delete_event(Calendar* c, Event* event)
{
	IcsCalendar* lc = FOCAL_ICS_CALENDAR(c);
	g_signal_emit_by_name(lc, "event-updated", event, NULL);

	g_hash_table_remove(lc->events, event_get_uid(event)); // calls event_free
	write_ical_to_disk(lc);
}

struct EachEventContext {
	CalendarEachEventCallback callback;
	gpointer user;
};

static void on_each_event(gpointer key, gpointer value, gpointer user_data)
{
	struct EachEventContext* ctx = (struct EachEventContext*) user_data;
	ctx->callback(ctx->user, value);
}

static void each_event(Calendar* c, CalendarEachEventCallback callback, void* user)
{
	IcsCalendar* lc = FOCAL_ICS_CALENDAR(c);
	struct EachEventContext ctx = {
		.callback = callback,
		.user = user};
	g_hash_table_foreach(lc->events, on_each_event, &ctx);
}

static void finalize(GObject* gobject)
{
	IcsCalendar* lc = FOCAL_ICS_CALENDAR(gobject);
	g_hash_table_destroy(lc->events);
	icalcomponent_free(lc->ical);
	g_free(lc->path);
	g_object_unref(lc->file);
	G_OBJECT_CLASS(ics_calendar_parent_class)->finalize(gobject);
}

void ics_calendar_init(IcsCalendar* lc)
{
}

static void sync_done(IcsCalendar* ic, const GString* data)
{
	if (ic->ical)
		icalcomponent_free(ic->ical);
	ic->ical = icalcomponent_new_from_string(data->str);
	g_assert_nonnull(ic->ical);

	// TODO how do we know if items were removed spontaneously?

	for (icalcomponent* e = icalcomponent_get_first_component(ic->ical, ICAL_VEVENT_COMPONENT); (e = icalcomponent_get_current_component(ic->ical));) {
		if (icalcomponent_isa(e) == ICAL_VEVENT_COMPONENT) {
			// try not to invalidate already known events
			const char* uid = icalcomponent_get_uid(e);
			g_assert_nonnull(uid);
			Event* existing = (Event*) g_hash_table_lookup(ic->events, uid);
			if (existing) {
				event_replace_component(existing, icalcomponent_new_clone(e));
				// TODO only if actually modified?
				g_signal_emit_by_name(ic, "event-updated", existing, existing);
			} else {
				Event* ev = event_new_from_icalcomponent(icalcomponent_new_clone(e));
				event_set_calendar(ev, FOCAL_CALENDAR(ic));
				g_hash_table_insert(ic->events, g_strdup(uid), ev);
				g_signal_emit_by_name(ic, "event-updated", NULL, ev);
			}
			icalcomponent_remove_component(ic->ical, e);
			icalcomponent_free(e);
		} else {
			icalcomponent_get_next_component(ic->ical, ICAL_VEVENT_COMPONENT);
		}
	}

	g_signal_emit_by_name(ic, "sync-done", TRUE, 0);
}

typedef struct {
	GString* str;
	IcsCalendar* ic;
} SyncContext;

static void stream_read_done(GObject* source, GAsyncResult* res, gpointer user_data)
{
	GError* err = NULL;
	long bytes_read = g_input_stream_read_finish(G_INPUT_STREAM(source), res, &err);
	if (bytes_read < 0) {
		// error
		g_critical("%s", err->message);
		// TODO: cleanup leaks
		return;
	}
	SyncContext* sc = (SyncContext*) user_data;
	sc->str->len += bytes_read;

	if (bytes_read > 0) {
		// maybe more to read
		if (sc->str->allocated_len - sc->str->len < READ_CHUNK_SIZE) {
			sc->str->str = g_realloc(sc->str->str, sc->str->allocated_len * 2);
			sc->str->allocated_len *= 2;
		}
		g_input_stream_read_async(G_INPUT_STREAM(source), sc->str->str + sc->str->len, READ_CHUNK_SIZE, G_PRIORITY_DEFAULT, NULL, stream_read_done, sc);
	} else {
		// finished
		sc->str->str[sc->str->len] = '\0';
		sync_done(sc->ic, sc->str);
		g_string_free(sc->str, TRUE);
		g_free(sc);
		g_object_unref(source);
	}
}

static void file_read_done(GObject* source, GAsyncResult* res, gpointer user_data)
{
	GError* err = NULL;
	GFileInputStream* stream = g_file_read_finish(G_FILE(source), res, &err);
	if (err) {
		g_critical("%s", err->message);
		return;
	}

	SyncContext* sc = g_new0(SyncContext, 1);
	sc->ic = FOCAL_ICS_CALENDAR(user_data);
	sc->str = g_string_sized_new(READ_CHUNK_SIZE);
	g_input_stream_read_async(G_INPUT_STREAM(stream), sc->str->str, READ_CHUNK_SIZE, G_PRIORITY_DEFAULT, NULL, stream_read_done, sc);
}

static void ics_calendar_sync(Calendar* c)
{
	IcsCalendar* ic = FOCAL_ICS_CALENDAR(c);
	GError* err = NULL;

	g_file_read_async(ic->file, G_PRIORITY_DEFAULT, NULL, file_read_done, ic);
	if (err) {
		g_object_unref(ic->file);
		_calendar_error(c, "Could not open %s for reading: %s", ic->path, err->message);
		g_signal_emit_by_name(ic, "sync-done", FALSE, 0);
		return;
	}
}

static gboolean ics_calendar_is_read_only(Calendar* c)
{
	// TODO: not always accurate, but will do for now
	return !g_file_is_native(FOCAL_ICS_CALENDAR(c)->file);
}

void ics_calendar_class_init(IcsCalendarClass* klass)
{
	FOCAL_CALENDAR_CLASS(klass)->save_event = save_event;
	FOCAL_CALENDAR_CLASS(klass)->delete_event = delete_event;
	FOCAL_CALENDAR_CLASS(klass)->each_event = each_event;
	FOCAL_CALENDAR_CLASS(klass)->sync = ics_calendar_sync;
	FOCAL_CALENDAR_CLASS(klass)->read_only = ics_calendar_is_read_only;
	G_OBJECT_CLASS(klass)->finalize = finalize;
}

Calendar* ics_calendar_new(const char* path)
{
	g_assert_nonnull(path);
	IcsCalendar* lc = g_object_new(ICS_CALENDAR_TYPE, NULL);
	lc->path = g_strdup(path);
	lc->file = g_file_new_for_uri(lc->path);
	lc->events = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
	g_assert_nonnull(lc->events);
	return (Calendar*) lc;
}
