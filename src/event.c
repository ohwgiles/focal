/*
 * event.c
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
#include "event.h"
#include "calendar.h"

struct _Event {
	GObject parent;
	icalcomponent* cmp;
	Calendar* cal;
	char* url;
	char* etag;
	gboolean dirty;
};

G_DEFINE_TYPE(Event, event, G_TYPE_OBJECT)

Calendar* event_get_calendar(Event* ev)
{
	return ev->cal;
}

GdkRGBA* event_get_color(Event* ev)
{
	static GdkRGBA grey = {0.7, 0.7, 0.7, 0.85};
	if (ev->cal)
		return calendar_get_color(ev->cal);
	else
		return &grey;
}

icalcomponent* event_get_component(Event* ev)
{
	return ev->cmp;
}

gboolean event_get_dirty(Event* ev)
{
	return ev->dirty;
}

const char* event_get_summary(Event* ev)
{
	return icalcomponent_get_summary(ev->cmp);
}

const char* event_get_description(Event* ev)
{
	return icalcomponent_get_description(ev->cmp);
}

const char* event_get_location(Event* ev)
{
	return icalcomponent_get_location(ev->cmp);
}

icaltimetype event_get_dtstart(Event* ev)
{
	return icalcomponent_get_dtstart(ev->cmp);
}

icaltimetype event_get_dtend(Event* ev)
{
	return icalcomponent_get_dtend(ev->cmp);
}

struct icaldurationtype event_get_duration(Event* ev)
{
	return icalcomponent_get_duration(ev->cmp);
}

const char* event_get_etag(Event* ev)
{
	return ev->etag;
}

static char* generate_ical_uid()
{
	unsigned char uuid[16];
	for (int i = 0; i < 16; ++i)
		uuid[i] = (unsigned char) g_random_int();
	// according to rfc4122 section 4.4
	uuid[8] = 0x80 | (uuid[8] & 0x5F);
	uuid[7] = 0x40 | (uuid[7] & 0x0F);
	return g_strdup_printf("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
						   uuid[0], uuid[1], uuid[2], uuid[3],
						   uuid[4], uuid[5], uuid[6], uuid[7],
						   uuid[8], uuid[9], uuid[10], uuid[11],
						   uuid[12], uuid[13], uuid[14], uuid[15]);
}

const char* event_get_uid(Event* ev)
{
	const char* uid = icalcomponent_get_uid(ev->cmp);
	if (uid == NULL) {
		char* p = generate_ical_uid();
		icalcomponent_set_uid(ev->cmp, p);
		uid = icalcomponent_get_uid(ev->cmp);
		g_assert_cmpstr(p, ==, uid);
		g_free(p);
	}
	return uid;
}

const char* event_get_url(Event* ev)
{
	return ev->url;
}

const char* event_get_alarm_trigger(Event* ev)
{
	icalcomponent* valarm = icalcomponent_get_first_component(ev->cmp, ICAL_VALARM_COMPONENT);
	if (!valarm)
		return NULL;

	icalproperty* prop = icalcomponent_get_first_property(valarm, ICAL_TRIGGER_PROPERTY);
	if (!prop)
		return NULL;

	return icalproperty_get_value_as_string(prop);
}

icaltimetype event_get_alarm_time(Event* ev)
{
	icalcomponent* valarm = icalcomponent_get_first_component(ev->cmp, ICAL_VALARM_COMPONENT);
	if (!valarm)
		return icaltime_null_time();

	icalproperty* prop = icalcomponent_get_first_property(valarm, ICAL_TRIGGER_PROPERTY);
	if (!prop)
		return icaltime_null_time();

	struct icaltriggertype trigger = icalproperty_get_trigger(prop);
	if (!icaltime_is_null_time(trigger.time))
		return trigger.time;
	else
		return icaltime_add(icalcomponent_get_dtstart(ev->cmp), trigger.duration);
}

void event_set_calendar(Event* ev, Calendar* cal)
{
	g_assert_null(ev->cal);
	ev->cal = cal;
}

void event_set_dtstart(Event* ev, icaltimetype dt)
{
	icalcomponent_set_dtstart(ev->cmp, dt);
	ev->dirty = TRUE;
}

void event_set_dtend(Event* ev, icaltimetype dt)
{
	// an icalcomponent may have DTEND or DURATION, but not both. focal prefers DTEND,
	// but libical will error out if set_dtend is called when the event event already has
	// a DURATION. So unconditionally remove any DURATION property before calling set_dtend.
	icalcomponent_remove_property(ev->cmp, icalcomponent_get_first_property(ev->cmp, ICAL_DURATION_PROPERTY));
	icalcomponent_set_dtend(ev->cmp, dt);
	ev->dirty = TRUE;
}

void event_set_alarm_trigger(Event* ev, const char* trigger_string)
{
	struct icaltriggertype trigger = {
		.time = icaltime_from_string(trigger_string),
		.duration = icaldurationtype_from_string(trigger_string)};

	icalcomponent* valarm = icalcomponent_get_first_component(ev->cmp, ICAL_VALARM_COMPONENT);
	if (!valarm) {
		valarm = icalcomponent_new_valarm();
		icalcomponent_add_component(ev->cmp, valarm);
	}

	icalproperty* prop = icalcomponent_get_first_property(valarm, ICAL_TRIGGER_PROPERTY);
	if (prop) {
		icalproperty_set_trigger(prop, trigger);
	} else {
		icalcomponent_add_property(valarm, icalproperty_new_trigger(trigger));
	}

	ev->dirty = TRUE;
}

gboolean event_set_participation_status(Event* ev, icalparameter_partstat status)
{
	g_assert_nonnull(ev->cal);
	const char* participant_email = calendar_get_email(ev->cal);
	if (!participant_email)
		return FALSE;
	for (icalproperty* attendee = icalcomponent_get_first_property(ev->cmp, ICAL_ATTENDEE_PROPERTY); attendee; attendee = icalcomponent_get_next_property(ev->cmp, ICAL_ATTENDEE_PROPERTY)) {
		const char* cal_addr = icalproperty_get_attendee(attendee);
		if (g_ascii_strncasecmp(cal_addr, "mailto:", 7) == 0 && g_ascii_strcasecmp(&cal_addr[7], participant_email) == 0) {
			icalparameter* partstat = icalproperty_get_first_parameter(attendee, ICAL_PARTSTAT_PARAMETER);
			if (!partstat) {
				partstat = icalparameter_new(ICAL_PARTSTAT_PARAMETER);
				icalproperty_add_parameter(attendee, partstat);
			}
			icalparameter_set_partstat(partstat, status);
			ev->dirty = TRUE;
			return TRUE;
		}
	}
	return FALSE;
}

void event_set_summary(Event* ev, const char* summary)
{
	icalcomponent_set_summary(ev->cmp, summary);
	ev->dirty = TRUE;
}

void event_set_description(Event* ev, const char* description)
{
	icalcomponent_set_description(ev->cmp, description);
	ev->dirty = TRUE;
}

void event_set_location(Event* ev, const char* location)
{
	icalcomponent_set_location(ev->cmp, location);
	ev->dirty = TRUE;
}

void event_set_url(Event* ev, const char* url)
{
	ev->url = g_strdup(url);
}

void event_update_etag(Event* ev, char* etag)
{
	// takes ownership
	ev->etag = etag;
}

void event_add_attendee(Event* ev, const char* name)
{
	icalproperty* attendee = icalproperty_new_attendee(name);
	icalcomponent_add_property(ev->cmp, attendee);
	ev->dirty = TRUE;
}

void event_each_attendee(Event* ev, void (*callback)(), void* user)
{
	for (icalproperty* attendee = icalcomponent_get_first_property(ev->cmp, ICAL_ATTENDEE_PROPERTY); attendee; attendee = icalcomponent_get_next_property(ev->cmp, ICAL_ATTENDEE_PROPERTY)) {
		callback(ev, attendee, user);
	}
}

void event_remove_attendee(Event* ev, icalproperty* attendee)
{
	icalcomponent_remove_property(ev->cmp, attendee);
	ev->dirty = TRUE;
}

void event_each_recurrence(Event* ev, const icaltimezone* user_tz, EventRecurrenceCallback callback, gpointer user)
{
	icaltimetype dtstart = event_get_dtstart(ev);
	//icaltimetype dtend = event_get_dtend(ev);
	const icaltimezone* tz = icaltime_get_timezone(dtstart);
	// convert to local time
	icaltimezone_convert_time(&dtstart, (icaltimezone*) tz, (icaltimezone*) user_tz);

	struct icaldurationtype duration = event_get_duration(ev);

	icalproperty* rrule = icalcomponent_get_first_property(ev->cmp, ICAL_RRULE_PROPERTY);
	if (rrule) {
		// recurring event
		struct icalrecurrencetype recur = icalproperty_get_rrule(rrule);
		icalrecur_iterator* ritr = icalrecur_iterator_new(recur, dtstart);
		icaltimetype next;
		while (next = icalrecur_iterator_next(ritr), !icaltime_is_null_time(next)) {
			(*callback)(ev, next, duration, user);
		}
		icalrecur_iterator_free(ritr);
	} else {
		// non-recurring event
		(*callback)(ev, dtstart, duration, user);
	}
}

static char* icalparser_read_fstream(char* s, size_t sz, void* ud)
{
	return fgets(s, sz, (FILE*) ud);
}

static void event_init(Event* e)
{
}

static void event_class_init(EventClass* e)
{
}

Event* event_new_from_ics_file(const char* path)
{
	FILE* stream = fopen(path, "r");
	if (!stream)
		return NULL;

	icalcomponent* c;
	char* line;
	icalparser* parser = icalparser_new();
	icalparser_set_gen_data(parser, stream);
	do {
		line = icalparser_get_line(parser, &icalparser_read_fstream);
		c = icalparser_add_line(parser, line);
		if (c) {
			Event* ev = g_object_new(FOCAL_TYPE_EVENT, NULL);
			ev->cmp = c;
			// Not exactly dirty, but has never been saved to a calendar
			ev->dirty = TRUE;
			return ev;
		}
	} while (line);
	return NULL;
}

Event* event_new_from_icalcomponent(icalcomponent* component)
{
	Event* ev = g_object_new(FOCAL_TYPE_EVENT, NULL);
	ev->cmp = component;
	return ev;
}

void event_replace_component(Event* ev, icalcomponent* component)
{
	icalcomponent_free(ev->cmp);
	ev->cmp = component;
}

Event* event_new(const char* summary, icaltimetype dtstart, icaltimetype dtend, const icaltimezone* tz)
{
	Event* e = g_object_new(FOCAL_TYPE_EVENT, NULL);
	icalcomponent* ev = icalcomponent_new_vevent();
	icalcomponent_set_dtstamp(ev, icaltime_from_timet_with_zone(time(NULL), 0, tz)); // required by ccs-calendarserver
	icalcomponent_set_dtstart(ev, dtstart);
	icalcomponent_set_dtend(ev, dtend);
	icalcomponent_set_summary(ev, summary);
	// default reminder 5m before
	icalcomponent* valarm = icalcomponent_new_valarm();
	struct icaltriggertype minus_5_minutes = {
		.duration = icaldurationtype_from_string("-PT5M")};
	icalcomponent_add_property(valarm, icalproperty_new_trigger(minus_5_minutes));
	icalcomponent_add_component(ev, valarm);
	e->cmp = ev;
	event_get_uid(e); // force generation of uid
	// Not exactly dirty, but has never been saved to a calendar
	e->dirty = TRUE;
	return e;
}

char* event_as_ical_string(Event* ev)
{
	icalcomponent* parent = icalcomponent_get_parent(ev->cmp);
	/* The event should have no parent if it was created here, or moved
	 * from another calendar, but it might have one if created from an
	 * invite file */
	if (parent == NULL) {
		parent = icalcomponent_new_vcalendar();
		icalcomponent_add_property(parent, icalproperty_new_version("2.0"));
		icalcomponent_add_property(parent, icalproperty_new_prodid("-//OHWG//FOCAL"));
		icaltimetype dtstart = icalcomponent_get_dtstart(ev->cmp);
		icalcomponent_add_component(parent, icalcomponent_new_clone(icaltimezone_get_component((icaltimezone*) dtstart.zone)));
		icalcomponent_add_component(parent, ev->cmp);
	}

	return icalcomponent_as_ical_string(parent);
}

void event_save(Event* ev)
{
	calendar_save_event(ev->cal, ev);
	ev->dirty = FALSE;
}

void event_free(Event* ev)
{
	icalcomponent* parent = icalcomponent_get_parent(ev->cmp);
	if (parent)
		icalcomponent_free(parent);
	else
		icalcomponent_free(ev->cmp);
	g_free(ev->etag);
	g_free(ev->url);
	g_object_unref(ev);
}
