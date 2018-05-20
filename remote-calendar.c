/*
 * remote-calendar.c
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
#include "remote-calendar.h"
#include "caldav-client.h"
#include "event-private.h"

struct _RemoteCalendar {
	Calendar parent;
	char* url;
	CaldavClient* caldav;
	GSList* events;
};
G_DEFINE_TYPE(RemoteCalendar, remote_calendar, TYPE_CALENDAR)

static char* generate_ical_uid()
{
	char* buffer;
	unsigned char uuid[16];
	for (int i = 0; i < 16; ++i)
		uuid[i] = (unsigned char) rand();
	// according to rfc4122 section 4.4
	uuid[8] = 0x80 | (uuid[8] & 0x5F);
	uuid[7] = 0x40 | (uuid[7] & 0x0F);
	asprintf(&buffer, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			 uuid[0], uuid[1], uuid[2], uuid[3],
			 uuid[4], uuid[5], uuid[6], uuid[7],
			 uuid[8], uuid[9], uuid[10], uuid[11],
			 uuid[12], uuid[13], uuid[14], uuid[15]);
	return buffer;
}

static void add_event(Calendar* c, icalcomponent* event)
{
	RemoteCalendar* rc = FOCAL_REMOTE_CALENDAR(c);

	EventPrivate* priv = icalcomponent_create_private(event);

	const char* uid = icalcomponent_get_uid(event);
	if (uid == NULL) {
		char* p = generate_ical_uid();
		icalcomponent_set_uid(event, p);
		free(p);
		uid = icalcomponent_get_uid(event);
	}

	char* url;
	asprintf(&url, "%s%s.ics", rc->url, icalcomponent_get_uid(event));
	priv->url = url;
	priv->cal = c;

	icalcomponent* parent = icalcomponent_get_parent(event);
	/* The event should have no parent if it was created here, or moved
	 * from another calendar, but it might have one if created from an
	 * invite file */
	if (parent == NULL) {
		parent = icalcomponent_new_vcalendar();
		icalcomponent_add_component(parent, event);
	}

	caldav_client_put_new(rc->caldav, event, url);
}

static void update_event(Calendar* c, icalcomponent* event)
{
	RemoteCalendar* rc = FOCAL_REMOTE_CALENDAR(c);
	EventPrivate* priv = icalcomponent_get_private(event);
	caldav_client_put_update(rc->caldav, event, priv->url);
}

static void delete_event(Calendar* c, icalcomponent* event)
{
	RemoteCalendar* rc = FOCAL_REMOTE_CALENDAR(c);
	EventPrivate* priv = icalcomponent_get_private(event);
	caldav_client_delete(rc->caldav, event, priv->url);
}

static void each_event(Calendar* c, CalendarEachEventCallback callback, void* user)
{
	RemoteCalendar* rc = FOCAL_REMOTE_CALENDAR(c);
	for (GSList* p = rc->events; p; p = p->next) {
		if (p->data)
			callback(user, (Calendar*) rc, (icalcomponent*) p->data);
	}
}

static void free_events(RemoteCalendar* rc)
{
	for (GSList* p = rc->events; p; p = p->next) {
		if (p->data) {
			icalcomponent* ev = (icalcomponent*) p->data;
			free(icalcomponent_get_private(ev)->url);
			icalcomponent_free_private(ev);
			icalcomponent_free(icalcomponent_get_parent(ev));
			free(ev);
		}
	}
	g_slist_free(rc->events);
}

void remote_calendar_init(RemoteCalendar* rc)
{
}

void remote_calendar_class_init(RemoteCalendarClass* klass)
{
	FOCAL_CALENDAR_CLASS(klass)->add_event = add_event;
	FOCAL_CALENDAR_CLASS(klass)->update_event = update_event;
	FOCAL_CALENDAR_CLASS(klass)->delete_event = delete_event;
	FOCAL_CALENDAR_CLASS(klass)->each_event = each_event;
}

void remote_calendar_sync(RemoteCalendar* rc)
{
	free_events(rc);
	rc->events = caldav_client_sync(rc->caldav);
	for (GSList* p = rc->events; p; p = p->next) {
		icalcomponent* ev = p->data;
		icalcomponent_get_private(ev)->cal = FOCAL_CALENDAR(rc);
	}
}

Calendar* remote_calendar_new(const char* url, const char* username, const char* password)
{
	RemoteCalendar* rc = g_object_new(REMOTE_CALENDAR_TYPE, NULL);
	// TODO verify cert
	rc->url = strdup(url);
	rc->caldav = caldav_client_new(url, username, password, FALSE);
	rc->events = NULL;
	caldav_client_init(rc->caldav);
	return (Calendar*) rc;
}

void remote_calendar_free(RemoteCalendar* rc)
{
	free(rc->url);
	caldav_client_free(rc->caldav);
	free_events(rc);
	g_free(rc);
}
