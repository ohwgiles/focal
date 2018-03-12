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

struct _RemoteCalendar {
	Calendar parent;
	CaldavClient* caldav;
	GSList* events;
};
G_DEFINE_TYPE(RemoteCalendar, remote_calendar, TYPE_CALENDAR)

static void add_event(Calendar* c, icalcomponent* event)
{
	RemoteCalendar* rc = FOCAL_REMOTE_CALENDAR(c);
	caldav_client_put(rc->caldav, event, NULL);
}

static void delete_event(Calendar* c, icalcomponent* event)
{
	RemoteCalendar* rc = FOCAL_REMOTE_CALENDAR(c);
	caldav_client_delete(rc->caldav, event, NULL);
}

static void each_event(Calendar* c, CalendarEachEventCallback callback, void* user)
{
	RemoteCalendar* rc = FOCAL_REMOTE_CALENDAR(c);
	for (GSList* p = rc->events; p; p = p->next) {
		callback(user, p->data);
	}
}

static void free_events(RemoteCalendar* rc)
{
	for (GSList* p = rc->events; p; p = p->next) {
		if (p->data)
			icalcomponent_free(icalcomponent_get_parent((icalcomponent*) p->data));
	}
	g_slist_free(rc->events);
}

void remote_calendar_init(RemoteCalendar* rc)
{
}

void remote_calendar_class_init(RemoteCalendarClass* klass)
{
	FOCAL_CALENDAR_CLASS(klass)->add_event = add_event;
	FOCAL_CALENDAR_CLASS(klass)->delete_event = delete_event;
	FOCAL_CALENDAR_CLASS(klass)->each_event = each_event;
}

void remote_calendar_sync(RemoteCalendar* rc)
{
	free_events(rc);
	rc->events = caldav_client_sync(rc->caldav);
}

Calendar* remote_calendar_new(const char* url, const char* username, const char* password)
{
	RemoteCalendar* rc = g_object_new(REMOTE_CALENDAR_TYPE, NULL);
	// TODO verify cert
	rc->caldav = caldav_client_new(url, username, password, FALSE);
	rc->events = g_slist_alloc();
	caldav_client_init(rc->caldav);
	return (Calendar*) rc;
}

void remote_calendar_free(RemoteCalendar* rc)
{
	caldav_client_free(rc->caldav);
	free_events(rc);
	g_free(rc);
}
