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
#ifndef EVENT_H
#define EVENT_H

#include <glib-object.h>
#include <libical/ical.h>

struct _Calendar;
typedef struct _Calendar Calendar;

struct _GdkRGBA;
typedef struct _GdkRGBA GdkRGBA;

// The Event type is an abstraction layer around an icalcomponent
// of VEVENT type. It adds caldav-specific data members such as etag.
#define FOCAL_TYPE_EVENT event_get_type()
G_DECLARE_FINAL_TYPE(Event, event, FOCAL, EVENT, GObject)

// Getters for data members. No data transfer.
Calendar* event_get_calendar(Event* ev);
GdkRGBA* event_get_color(Event* ev);
gboolean event_get_dirty(Event* ev);
icalcomponent* event_get_component(Event* ev);
const char* event_get_summary(Event* ev);
const char* event_get_description(Event* ev);
const char* event_get_location(Event* ev);
icaltimetype event_get_dtstart(Event* ev);
icaltimetype event_get_dtend(Event* ev);
struct icaldurationtype event_get_duration(Event* ev);
const char* event_get_etag(Event* ev);
const char* event_get_uid(Event* ev);
const char* event_get_url(Event* ev);
const char* event_get_alarm_trigger(Event* ev);
icaltimetype event_get_alarm_time(Event* ev);

// Calendar object must outlive the Event. This is usually safe since
// the Event is added to the calendar at the same time and the destruction
// of a Calendar causes destruction of all the Events attached to it.
void event_set_calendar(Event* ev, Calendar* cal);

// Simple data setters
void event_set_dtstart(Event* ev, icaltimetype dt);
void event_set_dtend(Event* ev, icaltimetype dt);
void event_set_alarm_trigger(Event* ev, const char* trigger_string);
// Sets the participation status of the event by comparing the attendee
// list with the email address of the Calendar attached to the event.
gboolean event_set_participation_status(Event* ev, icalparameter_partstat status);

// Data setters make a copy of the passed string
void event_set_summary(Event* ev, const char* summary);
void event_set_description(Event* ev, const char* description);
void event_set_location(Event* ev, const char* location);
void event_set_url(Event* ev, const char* url);

// Updates the etag associated with this event and takes ownership of
// the passed pointer. Should be used when synchronizing the the server.
void event_update_etag(Event* ev, char* etag);

// Methods for iterating the list of attendees mentioned in the VEVENT.
void event_add_attendee(Event* ev, const char* name);
void event_each_attendee(Event*, void (*callback)(), void* user);
void event_remove_attendee(Event* ev, icalproperty* attendee);

// Iterates over each recurrence of an event, or calls the callback once
// if the event is not recurring. The callback will be passed a dtstart
// adjusted to the local timezone passed as tz.
// TODO: is this intuitive? What's wrong with passing the original dtstart?
typedef void (*EventRecurrenceCallback)(Event* event, icaltimetype dtstart, struct icaldurationtype duration, gpointer user);
void event_each_recurrence(Event* ev, const icaltimezone* tz, icaltime_span range, EventRecurrenceCallback callback, gpointer user);
gboolean event_is_recurring(Event* ev);

// Creates a new Event object by reading the contents of the file at the
// given path. Returns NULL if the file could not be read or parsed.
Event* event_new_from_ics_file(const char* path);

// Creates a new Event from an already-created icalcomponent.
// TODO: try to deprecate
Event* event_new_from_icalcomponent(icalcomponent* component);
// Replace the internal component with the passed one
void event_replace_component(Event* ev, icalcomponent* component);

// Creates a new Event with the given parameters
Event* event_new(const char* summary, icaltimetype dtstart, icaltimetype dtend, const icaltimezone* tz);

// Returns an ical VCALENDAR string containing this event. This might have
// been defined by a remote server and contain other objects, or it will be
// otherwise empty if it has been created by focal. Caller should free the
// string when finished.
char* event_as_ical_string(Event* ev);

// Saves the event to the stored calendar
// TODO: does this mean calendar_save_event should be called ONLY from here?
void event_save(Event* ev);

// Frees the event and all its owned data members
void event_free(Event* ev);

#endif // EVENT_H
