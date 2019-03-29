/*
 * outlook-calendar.c
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
	GSList* events;
	RemoteAuth* ba;
	gchar* tz;
	gchar* prefer_tz;
	icaltimezone* ical_tz;
	gboolean initial_sync;
	icaltime_span last_view_range;
};

G_DEFINE_TYPE(OutlookCalendar, outlook_calendar, TYPE_CALENDAR)

static void each_event(Calendar* c, CalendarEachEventCallback callback, void* user)
{
	OutlookCalendar* oc = FOCAL_OUTLOOK_CALENDAR(c);
	for (GSList* p = oc->events; p; p = p->next) {
		callback(user, (Event*) p->data);
	}
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
	ModifyContext* ac = (ModifyContext*) user;
	g_free(ac->url);
	printf("in on_delete_complete\n");
	if (ret == CURLE_OK) {
		long response_code;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		if (response_code == 204) {
			printf("success delete\n");
			ac->oc->events = g_slist_remove(ac->oc->events, ac->event);
			// reuse the sync-done event since for now the action is the same -> refresh the UI
			g_signal_emit_by_name(ac->oc, "sync-done", 0);
		}
	}
	g_free(ac);
}

static void do_delete_event(OutlookCalendar* oc, CURL* curl, struct curl_slist* headers, Event* event)
{
	ModifyContext* pc = g_new0(ModifyContext, 1);
	pc->oc = oc;
	pc->event = event;
	pc->url = g_strdup_printf("https://outlook.office.com/api/v2.0/me/events/%s", event_get_url(event));
	printf("try delete %s\n", pc->url);

	curl_easy_setopt(curl, CURLOPT_URL, pc->url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	async_curl_add_request(curl, headers, on_delete_complete, pc);
}

static void delete_event(Calendar* c, Event* event)
{
	OutlookCalendar* oc = FOCAL_OUTLOOK_CALENDAR(c);
	remote_auth_new_request(oc->ba, do_delete_event, oc, event);
}

struct icaltimetype icaltime_from_outlook_json(JsonReader* reader)
{
	struct icaltimetype r = {0};

	json_reader_read_member(reader, "DateTime");
	const char* str = json_reader_get_string_value(reader);
	// 2018-08-28T19:00:00.0000000
	sscanf(str, "%d-%d-%dT%d:%d:%d", &r.year, &r.month, &r.day, &r.hour, &r.minute, &r.second);
	json_reader_end_member(reader);
	json_reader_read_member(reader, "TimeZone");
	r.zone = icaltimezone_get_builtin_timezone(json_reader_get_string_value(reader));
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
	json_reader_read_member(reader, "Id");
	event_set_url(e, json_reader_get_string_value(reader));
	// debug
	//printf("---- id %s", event_get_url(e));
	json_reader_end_member(reader);

	icalcomponent* event = event_get_component(e);
	// Subject -> Summary
	json_reader_read_member(reader, "Subject");
	icalcomponent_set_summary(event, json_reader_get_string_value(reader));
	json_reader_end_member(reader);

	// Body -> Description
	json_reader_read_member(reader, "Body");
	json_reader_read_member(reader, "Content");
	icalcomponent_set_description(event, json_reader_get_string_value(reader));
	json_reader_end_member(reader);
	json_reader_end_member(reader);
	// Start -> DTSTART
	json_reader_read_member(reader, "Start");
	icalcomponent_set_dtstart(event, icaltime_from_outlook_json(reader));
	json_reader_end_member(reader);
	// End -> DTEND
	json_reader_read_member(reader, "End");
	icalcomponent_set_dtend(event, icaltime_from_outlook_json(reader));
	json_reader_end_member(reader);

	json_reader_read_member(reader, "Recurrence");
	if (json_reader_is_object(reader)) {
		struct icalrecurrencetype r = {0};
		icalrecurrencetype_clear(&r);
		json_reader_read_member(reader, "Pattern");
		json_reader_read_member(reader, "Interval");
		r.interval = json_reader_get_int_value(reader);
		json_reader_end_member(reader);
		json_reader_read_member(reader, "Type");
		const gchar* type = json_reader_get_string_value(reader);
		if (g_strcmp0(type, "Weekly") == 0) {
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

	if (json_reader_read_member(reader, "Attendees")) {
		for (int i = 0, n = json_reader_count_elements(reader); i < n; ++i) {
			json_reader_read_element(reader, i);
			json_reader_read_member(reader, "EmailAddress");
			json_reader_read_member(reader, "Name");
			const gchar* cn = json_reader_get_string_value(reader);
			json_reader_end_member(reader);
			json_reader_read_member(reader, "Address");
			gchar* mailto = g_strdup_printf("mailto:%s", json_reader_get_string_value(reader));
			json_reader_end_member(reader);
			json_reader_end_member(reader);
			json_reader_read_member(reader, "Status");
			json_reader_read_member(reader, "Response");
			const gchar* resp = json_reader_get_string_value(reader);
			json_reader_end_member(reader);
			json_reader_end_member(reader);
			icalparameter_partstat ps = ICAL_PARTSTAT_NONE;
			if (g_strcmp0(resp, "Accepted") == 0)
				ps = ICAL_PARTSTAT_ACCEPTED;
			else if (g_strcmp0(resp, "Declined") == 0)
				ps = ICAL_PARTSTAT_DECLINED;
			else if (g_strcmp0(resp, "TentativelyAccepted") == 0)
				ps = ICAL_PARTSTAT_TENTATIVE;
			icalcomponent_add_property(event, icalproperty_vanew_attendee(mailto, icalparameter_new_cn(cn), icalparameter_new_partstat(ps), 0));
			json_reader_end_element(reader);
			g_free(mailto);
		}
	}
	json_reader_end_member(reader);

	// TODO: many more fields
}

static void on_create_event_complete(CURL* curl, CURLcode ret, void* user)
{
	ModifyContext* ac = (ModifyContext*) user;
	printf("in on_create_event_complete\n");
	g_free(ac->url);
	g_free(ac->payload);
	if (ret == CURLE_OK) {
		long response_code;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		if ((response_code == 201 && ac->requires_add) || (response_code == 200 && !ac->requires_add)) {
			printf("got response: %s\n", ac->put_response->str);
			// update the event properties based on the response
			JsonParser* parser = json_parser_new();
			json_parser_load_from_data(parser, ac->put_response->str, ac->put_response->len, NULL);
			JsonReader* reader = json_reader_new(json_parser_get_root(parser));
			populate_event_from_json(ac->event, reader);
			g_object_unref(reader);
			g_object_unref(parser);

			if (ac->requires_add)
				ac->oc->events = g_slist_append(ac->oc->events, ac->event);

			// reuse the sync-done event since for now the action is the same -> refresh the UI
			g_signal_emit_by_name(ac->oc, "sync-done", 0);
		}
	}
	g_string_free(ac->put_response, TRUE);
	g_free(ac);
}

static void do_outlook_add_event(OutlookCalendar* oc, CURL* curl, struct curl_slist* headers, Event* event)
{
	JsonBuilder* builder = json_builder_new();

	//{
	//  "Subject": "Discuss the Calendar REST API",
	//  "Body": {
	//    "ContentType": "HTML",
	//    "Content": "I think it will meet our requirements!"
	//  },
	//  "Start": {
	//      "DateTime": "2014-02-02T18:00:00",
	//      "TimeZone": "Pacific Standard Time"
	//  },
	//  "End": {
	//      "DateTime": "2014-02-02T19:00:00",
	//      "TimeZone": "Pacific Standard Time"
	//  },
	//  "Attendees": [
	//    {
	//      "EmailAddress": {
	//        "Address": "janets@a830edad9050849NDA1.onmicrosoft.com",
	//        "Name": "Janet Schorr"
	//      },
	//      "Type": "Required"
	//    }
	//  ]
	//}
	char date_buffer[32];
	icaltimetype t;
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "Subject");
	json_builder_add_string_value(builder, event_get_summary(event));

	json_builder_set_member_name(builder, "Body");
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "ContentType");
	json_builder_add_string_value(builder, "text");
	json_builder_set_member_name(builder, "Content");
	json_builder_add_string_value(builder, event_get_description(event));
	json_builder_end_object(builder);

	json_builder_set_member_name(builder, "Start");
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "DateTime");
	t = event_get_dtstart(event);
	sprintf(date_buffer, "%04d-%02d-%02dT%02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
	json_builder_add_string_value(builder, date_buffer);
	json_builder_set_member_name(builder, "TimeZone");
	json_builder_add_string_value(builder, oc->tz);
	json_builder_end_object(builder);

	json_builder_set_member_name(builder, "End");
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "DateTime");
	t = event_get_dtend(event);
	sprintf(date_buffer, "%04d-%02d-%02dT%02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
	json_builder_add_string_value(builder, date_buffer);
	json_builder_set_member_name(builder, "TimeZone");
	json_builder_add_string_value(builder, oc->tz);
	json_builder_end_object(builder);

	json_builder_set_member_name(builder, "Attendees");
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
	sc->requires_add = (g_slist_find(oc->events, event) == NULL);

	json_node_free(root);
	g_object_unref(gen);
	g_object_unref(builder);

	headers = curl_slist_append(headers, "Content-Type: application/json");

	if (sc->requires_add) {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
		sc->url = g_strdup_printf("https://outlook.office.com/api/v2.0/me/events");
	} else {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
		sc->url = g_strdup_printf("https://outlook.office.com/api/v2.0/me/events/%s", event_get_url(event));
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
	remote_auth_new_request(oc->ba, do_outlook_add_event, oc, event);
}

typedef struct {
	OutlookCalendar* oc;
	GString* sync_resp;
} SyncContext;

static void do_outlook_sync(OutlookCalendar* oc, CURL* curl, struct curl_slist* headers);

static void on_sync_response(CURL* curl, CURLcode ret, void* user)
{
	SyncContext* sc = (SyncContext*) user;
	// debug
	//printf("received: [%s]\n", sc->sync_resp->str);
	OutlookCalendar* oc = sc->oc;

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code == 401) {
		g_warning("401 Unauthorized. Assuming auth token has expired and attempting refresh");
		g_string_free(sc->sync_resp, TRUE);
		g_free(sc);
		remote_auth_invalidate_credential(oc->ba, do_outlook_sync, oc, NULL);
		return;
	} else if (response_code != 200) {
		g_critical("unexpected response code %ld", response_code);
	}

	// free all events before repopulating
	g_slist_free_full(oc->events, (GDestroyNotify) event_free);
	oc->events = NULL;

	JsonParser* parser = json_parser_new();
	json_parser_load_from_data(parser, sc->sync_resp->str, sc->sync_resp->len, NULL);
	JsonReader* reader = json_reader_new(json_parser_get_root(parser));

	json_reader_read_member(reader, "value");
	for (int i = 0, n = json_reader_count_elements(reader); i < n; ++i) {
		Event* event = event_new_from_icalcomponent(icalcomponent_new_vevent());

		json_reader_read_element(reader, i);
		populate_event_from_json(event, reader);
		json_reader_end_element(reader);

		event_set_calendar(event, FOCAL_CALENDAR(oc));
		oc->events = g_slist_append(oc->events, event);
	}
	json_reader_end_member(reader);

	if (json_reader_read_member(reader, "@odata.nextLink"))
		g_warning("Unimplemented following of nextLink %s, truncated collection", json_reader_get_string_value(reader));
	json_reader_end_member(reader);

	g_object_unref(reader);
	g_object_unref(parser);

	g_string_free(sc->sync_resp, TRUE);
	g_free(sc);

	if (oc->last_view_range.start) {
		// now fill out the missing recurrence information for the last requested range.
		// sync-done will be emitted when complete
		oc->initial_sync = TRUE;
		calendar_load_additional_for_date_range(FOCAL_CALENDAR(oc), oc->last_view_range);
	} else {
		g_signal_emit_by_name(oc, "sync-done", 0);
	}
}

static void do_outlook_sync(OutlookCalendar* oc, CURL* curl, struct curl_slist* headers)
{
	SyncContext* sc = g_new0(SyncContext, 1);
	sc->oc = oc;
	sc->sync_resp = g_string_new(NULL);
	headers = curl_slist_append(headers, "client-request-id: abcd1234");
	headers = curl_slist_append(headers, "return-client-request-id: true");
	headers = curl_slist_append(headers, "Prefer: outlook.body-content-type=\"text\"");
	headers = curl_slist_append(headers, oc->prefer_tz);
	curl_easy_setopt(curl, CURLOPT_URL, "https://outlook.office.com/api/v2.0/me/events?%24top=1000");
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, sc->sync_resp);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);
	// debug
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	async_curl_add_request(curl, headers, on_sync_response, sc);
}

static void outlook_sync(Calendar* c)
{
	OutlookCalendar* oc = FOCAL_OUTLOOK_CALENDAR(c);
	remote_auth_new_request(oc->ba, do_outlook_sync, oc, NULL);
}

static gboolean outlook_is_read_only(Calendar* c)
{
	return FALSE;
}

typedef struct {
	OutlookCalendar* oc;
	icaltime_span range;
	gchar* range_string;
	int num_reqs_remaining;
	CURL* curl;
	struct curl_slist* headers;
} LoadOccurrencesContext;

typedef struct {
	LoadOccurrencesContext* load_ctx;
	GString* resp;
	Event* event;
	gchar* url;
} LoadSingleOccurrenceContext;

static void add_unexpected_recurrence(gpointer key, gpointer value, gpointer user_data)
{
	icalcomponent* cmp = (icalcomponent*) user_data;
	struct icaldatetimeperiodtype at = {
		.time = icaltime_from_timet_with_zone((time_t) key, 0, NULL)};
	icalcomponent_add_property(cmp, icalproperty_new_rdate(at));
}

static void on_recurrence_response(CURL* curl, CURLcode ret, void* user)
{
	LoadSingleOccurrenceContext* soc = (LoadSingleOccurrenceContext*) user;
	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code == 401) {
		// In other circumstances we would get a new auth token and try the request again. In this case,
		// we might have a very large number of asynchronous requests in parallel which would all fail
		// the same way. Requesting a new auth token in each of them would be a quick way to get blocked
		// from the API, so don't even try.
		// Unfortunately, this is not an unreasonable branch to land in - if the auth token expires while
		// a view is open, and the user navigates to a new week, we will end up here. A future improvement
		// might be to fetch just the first recurring event (acquiring a new auth token if necessary), and
		// once successful fire off requests for all the others.
		g_warning("401 Unauthorized. Not attempting auth token refresh, view may be inconsistent!");
		goto on_recurrence_response_cleanup;
	} else if (response_code != 200) {
		g_critical("unexpected response code %ld", response_code);
	}

	// debug
	// printf("response: %.*s\n", (int) soc->resp->len, soc->resp->str);

	JsonParser* parser = json_parser_new();
	json_parser_load_from_data(parser, soc->resp->str, soc->resp->len, NULL);
	JsonReader* reader = json_reader_new(json_parser_get_root(parser));

	GHashTable* recurrences_in_range = g_hash_table_new(g_direct_hash, g_direct_equal);
	json_reader_read_member(reader, "value");
	for (int i = 0, n = json_reader_count_elements(reader); i < n; ++i) {
		json_reader_read_element(reader, i);
		json_reader_read_member(reader, "Start");
		json_reader_read_member(reader, "DateTime");
		time_t at = time_t_from_outlook_datetime(json_reader_get_string_value(reader));

		g_hash_table_add(recurrences_in_range, GINT_TO_POINTER(at));
		json_reader_end_member(reader);
		json_reader_end_member(reader);
		json_reader_end_element(reader);
	}
	json_reader_end_member(reader);
	g_object_unref(reader);
	g_object_unref(parser);

	icalcomponent* cmp = event_get_component(soc->event);
	icalproperty* rrule = icalcomponent_get_first_property(cmp, ICAL_RRULE_PROPERTY);
	icaltimetype dtstart = icalcomponent_get_dtstart(cmp);
	icaltimetype dtfrom = icaltime_from_timet_with_zone(soc->load_ctx->range.start, 0, NULL);
	icaltimetype dtuntil = icaltime_from_timet_with_zone(soc->load_ctx->range.end, 0, NULL);
	struct icalrecurrencetype recur = icalproperty_get_rrule(rrule);
	icalrecur_iterator* ritr = icalrecur_iterator_new(recur, dtstart);
	icaltimetype next;
	// TODO: optimize
	do
		next = icalrecur_iterator_next(ritr);
	while (icaltime_compare(next, dtfrom) == -1);
	while (!icaltime_is_null_time(next) && icaltime_compare(next, dtuntil) == -1) {
		time_t at = icaltime_as_timet_with_zone(next, next.zone);

		if (g_hash_table_contains(recurrences_in_range, GINT_TO_POINTER(at))) {
			g_hash_table_remove(recurrences_in_range, GINT_TO_POINTER(at));
			//printf("debug: [%s] known occurrence at %s\n", icalcomponent_get_summary(cmp), icaltime_as_ical_string(next));
		} else if (!icalproperty_recurrence_is_excluded(cmp, &dtstart, &next)) {
			// this occurrence should not exist, and we didn't already know that
			//printf("debug: [%s] adding exclusion at %s\n", icalcomponent_get_summary(cmp), icaltime_as_ical_string(next));
			icalcomponent_add_property(cmp, icalproperty_new_exdate(next));
		}
		next = icalrecur_iterator_next(ritr);
	}
	icalrecur_iterator_free(ritr);
	// everything remaining in recurrences_in_range should be additional ocurrences of this event
	g_hash_table_foreach(recurrences_in_range, add_unexpected_recurrence, cmp);
	g_hash_table_destroy(recurrences_in_range);

on_recurrence_response_cleanup:
	g_free(soc->url);
	g_string_free(soc->resp, TRUE);

	if (--soc->load_ctx->num_reqs_remaining == 0) {
		//printf("debug: all recurrence requests finished\n");
		// Finished retrieving all extra recurrence info. Clean up the parent structure and
		// emit sync-done so that the view will be updated
		LoadOccurrencesContext* loc = soc->load_ctx;
		// CURL handle was never used (only clones of it), so we have to clean it ourselves
		curl_slist_free_all(loc->headers);
		curl_easy_cleanup(loc->curl);

		g_free(loc->range_string);

		g_signal_emit_by_name(loc->oc, "sync-done", 0);
		g_free(loc);
	}

	g_free(soc);
}

static void do_request_recurrences(void* user, Event* event)
{
	LoadOccurrencesContext* loc = (LoadOccurrencesContext*) user;
	if (event_is_recurring(event)) {
		loc->num_reqs_remaining++;

		LoadSingleOccurrenceContext* soc = g_new0(LoadSingleOccurrenceContext, 1);
		soc->load_ctx = loc;
		soc->event = event;
		soc->resp = g_string_new(NULL);
		CURL* curl = curl_easy_duphandle(loc->curl);
		soc->url = g_strdup_printf("https://outlook.office.com/api/v2.0/me/events/%s/instances?%s", event_get_url(event), soc->load_ctx->range_string);

		struct curl_slist* headers = NULL;
		for (struct curl_slist* it = loc->headers; it; it = it->next)
			headers = curl_slist_append(headers, it->data);
		curl_easy_setopt(curl, CURLOPT_URL, soc->url);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, soc->resp);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);

		async_curl_add_request(curl, headers, on_recurrence_response, soc);
	}
}

static void request_event_recurrences(LoadOccurrencesContext* loc, CURL* curl, struct curl_slist* headers)
{
	headers = curl_slist_append(headers, "client-request-id: abcd1234");
	headers = curl_slist_append(headers, "return-client-request-id: true");
	headers = curl_slist_append(headers, "Prefer: outlook.body-content-type=\"text\"");

	// remote_auth_new_request just gives us a single CURL handle. Store it, then
	// clone it for each request, so they can be executed in parallel.
	loc->curl = curl;
	loc->headers = headers;

	each_event(FOCAL_CALENDAR(loc->oc), do_request_recurrences, loc);
}

static void outlook_load_additional_for_date_range(Calendar* c, icaltime_span range)
{
	OutlookCalendar* oc = FOCAL_OUTLOOK_CALENDAR(c);
	oc->last_view_range = range;
	if (!oc->initial_sync) {
		// If we haven't synced yet, there's no sense in fetching recurrence information.
		// Recurrence information will be fetched after sync.
		return;
	}
	LoadOccurrencesContext* loc = g_new0(LoadOccurrencesContext, 1);
	loc->oc = oc;
	loc->range = range;
	char buf_from[32], buf_to[32];
	struct tm tm_from, tm_to;
	localtime_r(&range.start, &tm_from);
	localtime_r(&range.end, &tm_to);
	strftime(buf_from, 24, "%FT00:00:00", &tm_from);
	strftime(buf_to, 24, "%FT00:00:00", &tm_to);
	loc->range_string = g_strdup_printf("startDateTime=%s&endDateTime=%s", buf_from, buf_to);
	remote_auth_new_request(loc->oc->ba, request_event_recurrences, loc, NULL);
}

static void finalize(GObject* gobject)
{
	OutlookCalendar* oc = FOCAL_OUTLOOK_CALENDAR(gobject);
	g_object_unref(oc->ba);
	g_slist_free_full(oc->events, (GDestroyNotify) event_free);
	g_free(oc->tz);
	g_free(oc->prefer_tz);
	G_OBJECT_CLASS(outlook_calendar_parent_class)->finalize(gobject);
}

void outlook_calendar_init(OutlookCalendar* rc)
{
}

static void attach_authenticator(Calendar* c, RemoteAuth* auth)
{
	FOCAL_OUTLOOK_CALENDAR(c)->ba = auth;
}

void outlook_calendar_class_init(OutlookCalendarClass* klass)
{
	FOCAL_CALENDAR_CLASS(klass)->save_event = add_event;
	FOCAL_CALENDAR_CLASS(klass)->delete_event = delete_event;
	FOCAL_CALENDAR_CLASS(klass)->each_event = each_event;
	FOCAL_CALENDAR_CLASS(klass)->sync = outlook_sync;
	FOCAL_CALENDAR_CLASS(klass)->read_only = outlook_is_read_only;
	FOCAL_CALENDAR_CLASS(klass)->load_additional_for_date_range = outlook_load_additional_for_date_range;
	FOCAL_CALENDAR_CLASS(klass)->attach_authenticator = attach_authenticator;
	G_OBJECT_CLASS(klass)->finalize = finalize;
}

Calendar* outlook_calendar_new(CalendarConfig* cfg)
{
	OutlookCalendar* oc = g_object_new(OUTLOOK_CALENDAR_TYPE, "auth", g_object_new(REMOTE_AUTH_OAUTH2_TYPE, "cfg", cfg, "provider", g_object_new(TYPE_OAUTH2_PROVIDER_OUTLOOK, NULL), NULL), NULL);
	oc->cfg = cfg;
	oc->events = NULL;
	// TODO: error handling
	char* localtime_link = realpath("/etc/localtime", NULL);
	oc->tz = g_strdup(localtime_link + strlen("/usr/share/zoneinfo/"));
	free(localtime_link);
	oc->ical_tz = icaltimezone_get_builtin_timezone(oc->tz);
	oc->prefer_tz = g_strdup_printf("Prefer: outlook.timezone=\"%s\"", oc->tz);

	return FOCAL_CALENDAR(oc);
}
