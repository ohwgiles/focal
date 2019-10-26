/*
 * outlook-calendar.c
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
#include "outlook-calendar.h"
#include "async-curl.h"
#include "oauth2-provider-outlook.h"
#include "remote-auth-oauth2.h"
#include <curl/curl.h>
#include <json-glib/json-glib.h>
#include <libsecret/secret.h>
#include <stdlib.h>
#include <string.h>

struct _OutlookCalendar {
	Calendar parent;
	const CalendarConfig* cfg;
	GHashTable* events;
	RemoteAuth* auth;
	gchar* tz;
	gchar* prefer_tz;
	icaltimezone* ical_tz;
	gchar* sync_url;
	icaltime_span sync_range;
};

G_DEFINE_TYPE(OutlookCalendar, outlook_calendar, TYPE_CALENDAR)

// defined in windows-tz-map.gperf
extern const char* outlook_timezone_to_tzid(const char* windows_name);

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
	OutlookCalendar* oc = FOCAL_OUTLOOK_CALENDAR(c);
	struct EachEventContext ctx = {
		.callback = callback,
		.user = user};
	g_hash_table_foreach(oc->events, on_each_event, &ctx);
}

typedef struct {
	OutlookCalendar* oc;
	char* url;
	gchar* payload;
	GString* put_response;
	gboolean requires_add;
	Event* event;
} ModifyContext;

static void on_delete_complete(CURL* curl, CURLcode ret, void* user)
{
	ModifyContext* mc = (ModifyContext*) user;
	g_free(mc->url);

	if (ret != CURLE_OK) {
		// TODO report error via UI
		fprintf(stderr, "curl error: %s\n", curl_easy_strerror(ret));
		g_string_free(mc->put_response, TRUE);
		free(mc);
		return;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code == 204) {
		g_hash_table_remove(mc->oc->events, event_get_url(mc->event)); // calls event_free
		// reuse the sync-done event since for now the action is the same -> refresh the UI
		g_signal_emit_by_name(mc->oc, "sync-done", 0);
	}

	g_free(mc);
}

static void do_delete_event(OutlookCalendar* oc, CURL* curl, struct curl_slist* headers, Event* event)
{
	ModifyContext* pc = g_new0(ModifyContext, 1);
	pc->oc = oc;
	pc->event = event;
	pc->url = g_strdup_printf("https://graph.microsoft.com/v1.0/me/events/%s", event_get_url(event));

	curl_easy_setopt(curl, CURLOPT_URL, pc->url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	async_curl_add_request(curl, headers, on_delete_complete, pc);
}

static void delete_event(Calendar* c, Event* event)
{
	OutlookCalendar* oc = FOCAL_OUTLOOK_CALENDAR(c);
	remote_auth_new_request(oc->auth, do_delete_event, oc, event);
}

struct icaltimetype icaltime_from_outlook_json(JsonReader* reader)
{
	struct icaltimetype r = {0};

	json_reader_read_member(reader, "dateTime");
	const char* str = json_reader_get_string_value(reader);
	// 2018-08-28T19:00:00.0000000
	sscanf(str, "%d-%d-%dT%d:%d:%d", &r.year, &r.month, &r.day, &r.hour, &r.minute, &r.second);
	json_reader_end_member(reader);

	json_reader_read_member(reader, "timeZone");
	const char* zone_str = json_reader_get_string_value(reader);
	r.zone = icaltimezone_get_builtin_timezone(zone_str);
	if (!r.zone) // try Windows timezone names
		r.zone = icaltimezone_get_builtin_timezone(outlook_timezone_to_tzid(zone_str));
	json_reader_end_member(reader);

	return r;
}

time_t time_t_from_outlook_datetime(const char* str)
{
	struct tm t = {0};
	// 2018-08-28T19:00:00.0000000
	sscanf(str, "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec);
	t.tm_year -= 1900;
	t.tm_mon -= 1;
	return timegm(&t);
}

static void populate_event_from_json(Event* e, JsonReader* reader)
{
	json_reader_read_member(reader, "id");
	event_set_url(e, json_reader_get_string_value(reader));
	json_reader_end_member(reader);
	icaltimetype tt;
	const char* tz_str;

	icalcomponent* event = event_get_component(e);
	// Subject -> Summary
	json_reader_read_member(reader, "subject");
	icalcomponent_set_summary(event, json_reader_get_string_value(reader));
	json_reader_end_member(reader);

	// Body -> Description
	json_reader_read_member(reader, "body");
	json_reader_read_member(reader, "content");
	icalcomponent_set_description(event, json_reader_get_string_value(reader));
	json_reader_end_member(reader);
	json_reader_end_member(reader);
	// Start -> DTSTART
	json_reader_read_member(reader, "start");
	tt = icaltime_from_outlook_json(reader);
	json_reader_end_member(reader);
	// TODO: Usefully apply originalStartTimeZone
	json_reader_read_member(reader, "originalStartTimeZone");
	tz_str = json_reader_get_string_value(reader);
	(void) tz_str;
	json_reader_end_member(reader);
	icalcomponent_set_dtstart(event, tt);
	// End -> DTEND
	json_reader_read_member(reader, "end");
	icalcomponent_set_dtend(event, icaltime_from_outlook_json(reader));
	json_reader_end_member(reader);

	json_reader_read_member(reader, "recurrence");
	if (json_reader_is_object(reader)) {
		struct icalrecurrencetype r = {0};
		icalrecurrencetype_clear(&r);
		json_reader_read_member(reader, "pattern");
		json_reader_read_member(reader, "interval");
		r.interval = json_reader_get_int_value(reader);
		json_reader_end_member(reader);
		json_reader_read_member(reader, "type");
		const gchar* type = json_reader_get_string_value(reader);
		if (g_strcmp0(type, "weekly") == 0) {
			r.freq = ICAL_WEEKLY_RECURRENCE;
		} else {
			fprintf(stderr, "Unhandled recurrence type %s\n", type);
			r.freq = ICAL_NO_RECURRENCE;
		}
		json_reader_end_member(reader);
		json_reader_end_member(reader);
		// TODO: more complicated recurrence
		icalcomponent_add_property(event, icalproperty_new_rrule(r));
	}
	json_reader_end_member(reader);

	if (json_reader_read_member(reader, "attendees")) {
		for (int i = 0, n = json_reader_count_elements(reader); i < n; ++i) {
			json_reader_read_element(reader, i);
			json_reader_read_member(reader, "emailAddress");
			json_reader_read_member(reader, "name");
			const gchar* cn = json_reader_get_string_value(reader);
			json_reader_end_member(reader);
			json_reader_read_member(reader, "address");
			gchar* mailto = g_strdup_printf("mailto:%s", json_reader_get_string_value(reader));
			json_reader_end_member(reader);
			json_reader_end_member(reader);
			json_reader_read_member(reader, "status");
			json_reader_read_member(reader, "response");
			const gchar* resp = json_reader_get_string_value(reader);
			json_reader_end_member(reader);
			json_reader_end_member(reader);
			icalparameter_partstat ps = ICAL_PARTSTAT_NONE;
			if (g_strcmp0(resp, "accepted") == 0)
				ps = ICAL_PARTSTAT_ACCEPTED;
			else if (g_strcmp0(resp, "declined") == 0)
				ps = ICAL_PARTSTAT_DECLINED;
			else if (g_strcmp0(resp, "tentativelyAccepted") == 0)
				ps = ICAL_PARTSTAT_TENTATIVE;
			icalcomponent_add_property(event, icalproperty_vanew_attendee(mailto, icalparameter_new_cn(cn), icalparameter_new_partstat(ps), 0));
			json_reader_end_element(reader);
			g_free(mailto);
		}
	}
	json_reader_end_member(reader);

	// TODO: many more fields
}

static void do_outlook_add_event(OutlookCalendar* oc, CURL* curl, struct curl_slist* headers, Event* event);

static void on_create_event_complete(CURL* curl, CURLcode ret, void* user)
{
	ModifyContext* mc = (ModifyContext*) user;
	OutlookCalendar* oc = mc->oc;
	g_free(mc->url);
	g_free(mc->payload);

	if (ret != CURLE_OK) {
		// TODO report error via UI
		fprintf(stderr, "curl error: %s\n", curl_easy_strerror(ret));
		g_string_free(mc->put_response, TRUE);
		free(mc);
		return;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	if (response_code == 401) {
		g_warning("401 Unauthorized. Assuming auth token has expired and attempting refresh");
		g_string_free(mc->put_response, TRUE);
		g_free(mc);
		remote_auth_invalidate_credential(oc->auth, do_outlook_add_event, oc, NULL);
		return;
	} else if (response_code != (mc->requires_add ? 201 : 200)) {
		g_critical("unexpected response code %ld", response_code);
	}

	if ((response_code == 201 && mc->requires_add) || (response_code == 200 && !mc->requires_add)) {
		// update the event properties based on the response
		JsonParser* parser = json_parser_new();
		json_parser_load_from_data(parser, mc->put_response->str, mc->put_response->len, NULL);
		JsonReader* reader = json_reader_new(json_parser_get_root(parser));
		populate_event_from_json(mc->event, reader);
		g_object_unref(reader);
		g_object_unref(parser);

		if (mc->requires_add)
			g_hash_table_insert(mc->oc->events, g_strdup(event_get_url(mc->event)), mc->event);

		// reuse the sync-done event since for now the action is the same -> refresh the UI
		g_signal_emit_by_name(mc->oc, "sync-done", 0);
	}

	g_string_free(mc->put_response, TRUE);
	g_free(mc);
}

static void do_outlook_add_event(OutlookCalendar* oc, CURL* curl, struct curl_slist* headers, Event* event)
{
	JsonBuilder* builder = json_builder_new();

	// For properties see https://docs.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-1.0
	char date_buffer[32];
	icaltimetype t;
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "subject");
	json_builder_add_string_value(builder, event_get_summary(event));

	json_builder_set_member_name(builder, "body");
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "contentType");
	json_builder_add_string_value(builder, "text");
	json_builder_set_member_name(builder, "content");
	json_builder_add_string_value(builder, event_get_description(event));
	json_builder_end_object(builder);

	json_builder_set_member_name(builder, "start");
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "dateTime");
	t = event_get_dtstart(event);
	sprintf(date_buffer, "%04d-%02d-%02dT%02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
	json_builder_add_string_value(builder, date_buffer);
	json_builder_set_member_name(builder, "timeZone");
	json_builder_add_string_value(builder, oc->tz);
	json_builder_end_object(builder);

	json_builder_set_member_name(builder, "end");
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "dateTime");
	t = event_get_dtend(event);
	sprintf(date_buffer, "%04d-%02d-%02dT%02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
	json_builder_add_string_value(builder, date_buffer);
	json_builder_set_member_name(builder, "timeZone");
	json_builder_add_string_value(builder, oc->tz);
	json_builder_end_object(builder);

	json_builder_set_member_name(builder, "attendees");
	json_builder_begin_array(builder);
	json_builder_end_array(builder);

	json_builder_end_object(builder);

	JsonGenerator* gen = json_generator_new();
	JsonNode* root = json_builder_get_root(builder);
	json_generator_set_root(gen, root);

	ModifyContext* sc = g_new0(ModifyContext, 1);
	sc->oc = oc;
	sc->payload = json_generator_to_data(gen, NULL);
	sc->put_response = g_string_new(NULL);
	g_assert_nonnull(sc->put_response);
	sc->event = event;
	// TODO: consider moving from URL to UID - the generation happens there automatically.
	// Question: API sometimes uses iCalUid etc - are they always the same as the ID in the URL?
	if (!event_get_url(event))
		event_set_url(event, event_get_uid(event));
	sc->requires_add = !g_hash_table_contains(oc->events, event_get_url(event));

	json_node_free(root);
	g_object_unref(gen);
	g_object_unref(builder);

	headers = curl_slist_append(headers, "Content-Type: application/json");

	if (sc->requires_add) {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
		sc->url = g_strdup_printf("https://graph.microsoft.com/v1.0/me/events");
	} else {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
		sc->url = g_strdup_printf("https://graph.microsoft.com/v1.0/me/events/%s", event_get_url(event));
	}
	curl_easy_setopt(curl, CURLOPT_URL, sc->url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, sc->put_response);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, sc->payload);

	// debug
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	async_curl_add_request(curl, headers, on_create_event_complete, sc);
}

static void add_event(Calendar* c, Event* event)
{
	OutlookCalendar* oc = FOCAL_OUTLOOK_CALENDAR(c);
	remote_auth_new_request(oc->auth, do_outlook_add_event, oc, event);
}

static void do_outlook_sync(OutlookCalendar* oc, CURL* curl, struct curl_slist* headers);

static void outlook_sync(Calendar* c)
{
	OutlookCalendar* oc = FOCAL_OUTLOOK_CALENDAR(c);
	// range has not been set yet with outlook_sync_date_range
	if (!oc->sync_url) {
		// Date range not yet set. Probably initial sync. Notify done so it will be added to the view. TODO cleaner way?
		g_signal_emit_by_name(oc, "sync-done", 0);
		return;
	}

	remote_auth_new_request(oc->auth, do_outlook_sync, oc, NULL);
}

static gboolean outlook_is_read_only(Calendar* c)
{
	return FALSE;
}

typedef struct {
	OutlookCalendar* oc;
	struct curl_slist* headers;
	GString* resp;
	GSList* recurrences;
} SyncContext;

typedef struct {
	gboolean exception;
	char* seriesMasterId;
	icaltimetype start, end;
} RecurrenceInfo;

static void process_event_exceptions(SyncContext* sc)
{
	for (GSList* s = sc->recurrences; s; s = s->next) {
		RecurrenceInfo* ri = (RecurrenceInfo*) s->data;
		Event* master = g_hash_table_lookup(sc->oc->events, ri->seriesMasterId);
		if (!master) {
			g_warning("Series master not found: %s", ri->seriesMasterId);
			continue;
		}

		icalcomponent* cmp = event_get_component(master);
		if (ri->exception)
			icalcomponent_add_property(cmp, icalproperty_new_exdate(ri->start));
		else {
			event_add_occurrence(master, ri->start, ri->end);
		}

		// TODO: would be cleaner to provide a descructor for RecurrenceInfo
		g_free(ri->seriesMasterId);
	}
}

static RecurrenceInfo* parse_recurrence_info_from_json(JsonReader* reader)
{
	RecurrenceInfo* ri = g_new0(RecurrenceInfo, 1);
	// Extract the seriesMasterId so we know to which event to attach the recurrence information
	json_reader_read_member(reader, "seriesMasterId");
	ri->seriesMasterId = g_strdup(json_reader_get_string_value(reader));
	json_reader_end_member(reader);
	// "type" field may be "exception" or "occurrence". Save this for later so we
	// know whether to create an RDATE or an EXDATE.
	// See https://docs.microsoft.com/en-us/graph/api/resources/event?view=graph-rest-1.0
	json_reader_read_member(reader, "type");
	ri->exception = g_strcmp0(json_reader_get_string_value(reader), "exception") == 0;
	json_reader_end_member(reader);
	// Extract actual time of recurrence/exception
	json_reader_read_member(reader, "start");
	ri->start = icaltime_from_outlook_json(reader);
	json_reader_end_member(reader);
	json_reader_read_member(reader, "end");
	ri->end = icaltime_from_outlook_json(reader);
	json_reader_end_member(reader);
	return ri;
}

static void on_sync_response(CURL* curl, CURLcode ret, void* user)
{
	SyncContext* sc = (SyncContext*) user;
	OutlookCalendar* oc = sc->oc;

	if (ret != CURLE_OK) {
		// TODO report error via UI
		fprintf(stderr, "curl error: %s\n", curl_easy_strerror(ret));
		g_string_free(sc->resp, TRUE);
		free(sc);
		return;
	}

	// debug
	//printf("received: [%s]\n", sc->resp->str);

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code == 401) {
		g_warning("401 Unauthorized. Assuming auth token has expired and attempting refresh");
		g_string_free(sc->resp, TRUE);
		g_free(sc);
		remote_auth_invalidate_credential(oc->auth, do_outlook_sync, oc, NULL);
		return;
	} else if (response_code != 200) {
		g_critical("Unexpected response code %ld", response_code);
	}

	JsonParser* parser = json_parser_new();
	json_parser_load_from_data(parser, sc->resp->str, sc->resp->len, NULL);
	JsonReader* reader = json_reader_new(json_parser_get_root(parser));

	json_reader_read_member(reader, "value");
	for (int i = 0, n = json_reader_count_elements(reader); i < n; ++i) {
		json_reader_read_element(reader, i);

		json_reader_read_member(reader, "type");
		// The API helpfully returns the seriesMaster for occurrences within the requested
		// range even if the seriesMaster itself is outside the range. We need this so we
		// can build a normal ical event structure, but we cannot discount the expanded
		// occurrences returned by the API since there may be extra occurrences (analagous
		// to RDATE). Later we check whether we already knew about this recurrence and
		// discard it if so.
		gboolean do_defer = g_strcmp0(json_reader_get_string_value(reader), "occurrence") == 0 || g_strcmp0(json_reader_get_string_value(reader), "exception") == 0;
		json_reader_end_member(reader);

		if (do_defer) {
			sc->recurrences = g_slist_append(sc->recurrences, parse_recurrence_info_from_json(reader));
		} else {
			// TODO deduplicate fetching id with populate_event_from_json
			json_reader_read_member(reader, "id");
			char* id = strdup(json_reader_get_string_value(reader));
			json_reader_end_member(reader);

			// Handle removed events
			gboolean delete = json_reader_read_member(reader, "@removed");
			json_reader_end_member(reader);

			Event* existing = g_hash_table_lookup(oc->events, id);

			if (delete) {
				g_signal_emit_by_name(oc, "event-updated", existing, NULL);
				g_hash_table_remove(oc->events, id);
			} else if (existing) {
				// Can't just call populate_event_from_json because currently it assumes
				// an empty event, i.e. it will *add* elements rather than checking and
				// updating existing ones. TODO improve this! For now we delete first RRULE
				icalcomponent* cmp = event_get_component(existing);
				icalcomponent_remove_property(cmp, icalcomponent_get_first_property(cmp, ICAL_RRULE_PROPERTY));
				// then repopulate...
				populate_event_from_json(existing, reader);
				g_signal_emit_by_name(oc, "event-updated", existing, existing);
			} else {
				Event* event = event_new_from_icalcomponent(icalcomponent_new_vevent());
				populate_event_from_json(event, reader);
				event_set_calendar(event, FOCAL_CALENDAR(oc));
				g_hash_table_insert(oc->events, g_strdup(event_get_url(event)), event);
				g_signal_emit_by_name(oc, "event-updated", NULL, event);
			}
			free(id);
		}
		json_reader_end_element(reader);
	}
	json_reader_end_member(reader);

	// Response will either contain @odata.nextLink (more events to fetch)...
	if (json_reader_read_member(reader, "@odata.nextLink")) {
		g_string_truncate(sc->resp, 0);

		// TODO try to reuse the handle instead of creating a new one?
		curl = curl_easy_duphandle(curl);
		struct curl_slist* headers = NULL;
		for (struct curl_slist* it = sc->headers; it; it = it->next)
			headers = curl_slist_append(headers, it->data);
		curl_easy_setopt(curl, CURLOPT_URL, json_reader_get_string_value(reader));
		async_curl_add_request(curl, headers, on_sync_response, sc);
	}
	json_reader_end_member(reader);

	// ...or @odata.deltaLink, which should be used to fetch incremental updates
	if (json_reader_read_member(reader, "@odata.deltaLink")) {
		g_free(oc->sync_url);
		oc->sync_url = g_strdup(json_reader_get_string_value(reader));

		// In this case syncing is done
		process_event_exceptions(sc);
		g_slist_free_full(sc->recurrences, g_free);
		g_string_free(sc->resp, TRUE);
		g_free(sc);
		g_signal_emit_by_name(oc, "sync-done", 0);
	}
	json_reader_end_member(reader);

	g_object_unref(reader);
	g_object_unref(parser);
}

static void do_outlook_sync(OutlookCalendar* oc, CURL* curl, struct curl_slist* headers)
{
	headers = curl_slist_append(headers, "client-request-id: abcd1234");
	headers = curl_slist_append(headers, "return-client-request-id: true");
	headers = curl_slist_append(headers, "Prefer: outlook.body-content-type=\"text\"");
	headers = curl_slist_append(headers, oc->prefer_tz);

	SyncContext* sc = g_new0(SyncContext, 1);
	sc->oc = oc;

	// Save a cheeky reference to the CURL headers so we can lazily duplicate them later
	// (before the on_sync_response callback exits) without having to request a new auth handle
	// TODO implement a cleaner API that obviates this?
	sc->headers = headers;

	sc->resp = g_string_new(NULL);

	curl_easy_setopt(curl, CURLOPT_URL, oc->sync_url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, sc->resp);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);

	async_curl_add_request(curl, headers, on_sync_response, sc);
}

static void outlook_sync_date_range(Calendar* c, icaltime_span range)
{
	OutlookCalendar* oc = FOCAL_OUTLOOK_CALENDAR(c);

	char buf_from[32], buf_to[32];
	struct tm tm_from, tm_to;
	localtime_r(&range.start, &tm_from);
	localtime_r(&range.end, &tm_to);
	strftime(buf_from, 24, "%FT00:00:00", &tm_from);
	strftime(buf_to, 24, "%FT00:00:00", &tm_to);

	g_free(oc->sync_url);
	oc->sync_url = g_strdup_printf("https://graph.microsoft.com/v1.0/me/calendarView/delta?startDateTime=%s&endDateTime=%s", buf_from, buf_to);
	oc->sync_range = range;

	remote_auth_new_request(oc->auth, do_outlook_sync, oc, NULL);
}

static void finalize(GObject* gobject)
{
	OutlookCalendar* oc = FOCAL_OUTLOOK_CALENDAR(gobject);
	g_object_unref(oc->auth);
	g_hash_table_destroy(oc->events);
	g_free(oc->sync_url);
	g_free(oc->tz);
	g_free(oc->prefer_tz);
	G_OBJECT_CLASS(outlook_calendar_parent_class)->finalize(gobject);
}

void outlook_calendar_init(OutlookCalendar* rc)
{
}

static void attach_authenticator(Calendar* c, RemoteAuth* auth)
{
	FOCAL_OUTLOOK_CALENDAR(c)->auth = auth;
}

void outlook_calendar_class_init(OutlookCalendarClass* klass)
{
	FOCAL_CALENDAR_CLASS(klass)->save_event = add_event;
	FOCAL_CALENDAR_CLASS(klass)->delete_event = delete_event;
	FOCAL_CALENDAR_CLASS(klass)->each_event = each_event;
	FOCAL_CALENDAR_CLASS(klass)->sync = outlook_sync;
	FOCAL_CALENDAR_CLASS(klass)->read_only = outlook_is_read_only;
	FOCAL_CALENDAR_CLASS(klass)->sync_date_range = outlook_sync_date_range;
	FOCAL_CALENDAR_CLASS(klass)->attach_authenticator = attach_authenticator;
	G_OBJECT_CLASS(klass)->finalize = finalize;
}

Calendar* outlook_calendar_new(CalendarConfig* cfg)
{
	OutlookCalendar* oc = g_object_new(OUTLOOK_CALENDAR_TYPE, "auth", g_object_new(REMOTE_AUTH_OAUTH2_TYPE, "cfg", cfg, "provider", g_object_new(TYPE_OAUTH2_PROVIDER_OUTLOOK, NULL), NULL), NULL);
	oc->cfg = cfg;
	oc->events = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) event_free);
	oc->sync_url = NULL;

	// TODO: error handling
	char* localtime_link = realpath("/etc/localtime", NULL);
	oc->tz = g_strdup(localtime_link + strlen("/usr/share/zoneinfo/"));
	free(localtime_link);
	oc->ical_tz = icaltimezone_get_builtin_timezone(oc->tz);
	oc->prefer_tz = g_strdup_printf("Prefer: outlook.timezone=\"%s\"", oc->tz);

	return FOCAL_CALENDAR(oc);
}
