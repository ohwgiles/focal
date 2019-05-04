/*
 * reminder.c
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
#include "reminder.h"
#include "calendar.h"

#include <stdlib.h>
#include <string.h>

static icaltimezone* current_tz;
static icaltimetype now;
static icaltime_span notify_range;
static GHashTable* reminders;

typedef struct {
	time_t at;
	Event* event;
	guint source_id;
	gboolean known;
	gboolean seen;
} Reminder;

static const char* time_until_start(time_t dtstart)
{
	static char buffer[128];
	int n_secs = (int) (dtstart - time(NULL));
	if (n_secs < 60)
		sprintf(buffer, "%d seconds", n_secs);
	else if (n_secs < 3600)
		sprintf(buffer, "%d minutes", (n_secs / 60));
	else
		sprintf(buffer, "%d hours, %d minutes", (n_secs / 3600), (int) (n_secs / 60));
	return buffer;
}

static gboolean reminder_display(Reminder* rem)
{
	static char buffer[128];
	GNotification* notification = g_notification_new(event_get_summary(rem->event));
	time_t dtstart = icaltime_as_timet_with_zone(event_get_dtstart(rem->event), current_tz);
	sprintf(buffer, "%sin %s", ctime(&dtstart), time_until_start(dtstart));
	g_notification_set_body(notification, buffer);
	g_application_send_notification(g_application_get_default(), "event-reminder", notification);
	g_object_unref(notification);

	rem->seen = TRUE;
	return FALSE; // prevent callback from repeating
}

static void event_deleted(gpointer data, GObject* where_the_object_was)
{
	g_hash_table_remove(reminders, where_the_object_was);
}

static void check_occurrence_add_notification(Event* ev, icaltimetype next, struct icaldurationtype duration, gpointer user)
{
	// no notifications for all-day events
	if (next.is_date)
		return;

	time_t at = icaltime_as_timet(next);
	// TODO remove? This should be guaranteed by event_each_recurrence
	g_assert_true(notify_range.start < at && at < notify_range.end);

	Reminder* rem = (Reminder*) g_hash_table_lookup(reminders, ev);
	if (rem) {
		if (rem->at != at)
			g_warning("%s", "Limited support for notifications for closely recurring events");
		rem->known = TRUE;
	} else {
		rem = g_new0(Reminder, 1);
		rem->known = TRUE;
		rem->seen = FALSE;
		rem->at = at;
		rem->event = ev;
		g_object_weak_ref(G_OBJECT(ev), event_deleted, rem);
		time_t alarm = icaltime_as_timet_with_zone(event_get_alarm_time(ev), current_tz);
		time_t now = time(NULL); // TODO avoid call for each event/ocurrence?
		if (alarm > now)
			rem->source_id = g_timeout_add_seconds(alarm - now, (GSourceFunc) reminder_display, rem);
		else
			rem->seen = TRUE;
		g_hash_table_insert(reminders, ev, rem);
	}
}

static void check_event_add_notification(gpointer user_data, Event* ev)
{
	event_each_recurrence(ev, current_tz, notify_range, check_occurrence_add_notification, user_data);
}

static void update_current_time()
{
	now = icaltime_current_time_with_zone(current_tz);
	now.zone = NULL;
	notify_range.start = icaltime_as_timet(now);
	// only check events 6 hours in the future. Should be enough for notifications
	notify_range.end = notify_range.start + 6 * 3600;
}

static void notification_mark_unknown(gpointer key, gpointer value, gpointer user_data)
{
	((Reminder*) value)->known = FALSE;
}

static gboolean notification_is_unknown(gpointer key, gpointer value, gpointer user_data)
{
	return !((Reminder*) value)->known;
}

static void notification_free(Reminder* rem)
{
	// cancel pending popup
	if (!rem->seen)
		g_source_remove(rem->source_id);
	g_free(rem);
}

void reminder_init(void)
{
	g_assert_null(current_tz);
	g_assert_null(reminders);

	char* zoneinfo_link = realpath("/etc/localtime", NULL);
	current_tz = icaltimezone_get_builtin_timezone(zoneinfo_link + strlen("/usr/share/zoneinfo/"));
	free(zoneinfo_link);

	update_current_time();

	reminders = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify) notification_free);
}

void reminder_sync_notifications(GSList* calendars)
{
	g_assert_nonnull(reminders);
	update_current_time();

	g_hash_table_foreach(reminders, notification_mark_unknown, NULL);
	for (GSList* p = calendars; p; p = p->next) {
		calendar_each_event(p->data, check_event_add_notification, NULL);
	}
	g_hash_table_foreach_remove(reminders, notification_is_unknown, NULL);
}

void reminder_cleanup(void)
{
	g_assert_nonnull(reminders);
	g_hash_table_destroy(reminders);
	reminders = NULL;
}
