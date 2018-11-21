/*
 * caldav-calendar.c
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
#include <curl/curl.h>
#include <libxml/SAX2.h>

#include "async-curl.h"
#include "caldav-calendar.h"
#include "remote-auth.h"

struct _CaldavCalendar {
	Calendar parent;
	char* sync_token;
	RemoteAuth* auth;
	gboolean op_pending;
	GSList* events;
};
G_DEFINE_TYPE(CaldavCalendar, caldav_calendar, TYPE_CALENDAR)

typedef struct {
	int depth;
	char* name;
} XmlNs;

typedef struct {
	// internal members used in raw xml parsing
	GString chars;
	int depth;
	GQueue* ns_defaults;
	GHashTable* ns_aliases;
	// fields of interest parsed from the xml
	int status;
	char* current_href;
	char* current_etag;
	char* current_caldata;
	// final results to be consumed by the caller
	GSList* result_list;
	char* sync_token;
} XmlParseCtx;

static void xmlns_free(XmlNs* ns)
{
	g_free(ns->name);
	g_free(ns);
}

// Checks the list of passed XML attributes for default namespaces
// and namespace aliases and updates the context accordingly. Call when
// entering a new XML tag pair.
static void xmlparse_ns_push(XmlParseCtx* xpc, const xmlChar** atts)
{
	xpc->depth++;
	if (!atts)
		return;

	// loop over attributes to find the namespace names
	for (const xmlChar** k = atts; *k; k += 2) {
		const xmlChar* v = k[1];
		if (strcmp(*k, "xmlns") == 0) {
			XmlNs* ns = g_new(XmlNs, 1);
			ns->depth = xpc->depth;
			ns->name = g_strdup(v);
			g_queue_push_head(xpc->ns_defaults, ns);
		} else if (strncmp(*k, "xmlns:", 6) == 0) {
			g_hash_table_insert(xpc->ns_aliases, g_strdup(v), g_strdup(*k + 6));
		}
	}
}

// If leaving an XML tag pair, check if the current default namespace
// scope has ended and pop it from the stack if so. Call when leaving
// an XML tag pair.
static void xmlparse_ns_pop(XmlParseCtx* xpc)
{
	XmlNs* ns = g_queue_peek_head(xpc->ns_defaults);
	if (ns && ns->depth == xpc->depth) {
		xmlns_free(ns);
		g_queue_pop_head(xpc->ns_defaults);
	}
}

// Helper function to match a found XML tag, taking all namespace aliases
// and default namespaces into account
static gboolean xml_tag_matches(XmlParseCtx* xpc, const char* tag, const char* expectedNs, const char* expectedTag)
{
	const char* nsc = strchr(tag, ':');
	if (nsc) {
		char* nsAbbr = g_hash_table_lookup(xpc->ns_aliases, expectedNs);
		if (nsAbbr)
			return nsc - tag == strlen(nsAbbr) && memcmp(nsAbbr, tag, nsc - tag) == 0 && strcmp(expectedTag, nsc + 1) == 0;
	} else {
		XmlNs* topns = g_queue_peek_head(xpc->ns_defaults);
		if (topns)
			return strcmp(topns->name, expectedNs) == 0 && strcmp(expectedTag, tag) == 0;
	}
	return FALSE;
}

// Callback when SAX parser ends a CDATA section. Store the content, it
// will probably be needed (checked in the endElement callback)
static void xmlparse_characters(void* ctx, const xmlChar* ch, int len)
{
	XmlParseCtx* xpc = (XmlParseCtx*) ctx;
	g_string_append_len(&xpc->chars, ch, len);
}

// Callback when SAX parser finds an open XML tag (all-purpose)
static void xmlparse_tag_open(void* ctx, const xmlChar* fullname, const xmlChar** atts)
{
	XmlParseCtx* xpc = (XmlParseCtx*) ctx;
	xmlparse_ns_push(xpc, atts);
	g_string_truncate(&xpc->chars, 0);
}

typedef struct {
	char* href;
	char* etag;
	char* caldata;
} CaldavEntry;

// Callback when SAX parser finds a closing XML tag during calendar-data REPORT
static void xmlparse_find_caldata(void* ctx, const xmlChar* name)
{
	XmlParseCtx* xpc = (XmlParseCtx*) ctx;
	if (xml_tag_matches(xpc, name, "DAV:", "href")) {
		// store the href for the current response element
		g_assert_null(xpc->current_href);
		xpc->current_href = g_strdup(xpc->chars.str);
	} else if (xml_tag_matches(xpc, name, "DAV:", "getetag")) {
		g_assert_null(xpc->current_etag);
		xpc->current_etag = g_strdup(xpc->chars.str);
	} else if (xml_tag_matches(xpc, name, "DAV:", "response")) {
		g_assert_nonnull(xpc->current_href);
		// cannot assert that other fields are non-null, because if the response is not 200, they won't be supplied
		CaldavEntry* e = malloc(sizeof(CaldavEntry));
		// transfer ownership
		e->href = xpc->current_href;
		e->etag = xpc->current_etag;
		e->caldata = xpc->current_caldata;
		xpc->result_list = g_slist_append(xpc->result_list, e);

		xpc->current_href = NULL;
		xpc->current_etag = NULL;
		xpc->current_caldata = NULL;
	} else if (xml_tag_matches(xpc, name, "urn:ietf:params:xml:ns:caldav", "calendar-data")) {
		g_assert_null(xpc->current_caldata);
		xpc->current_caldata = g_strdup(xpc->chars.str);
	}

	xmlparse_ns_pop(xpc);
}

typedef struct {
	char* href;
	int status;
} SyncEntry;

// Callback when SAX parser finds a closing XML tag during sync-collection REPORT
static void xmlparse_report_sync_collection(void* ctx, const xmlChar* name)
{
	XmlParseCtx* xpc = (XmlParseCtx*) ctx;
	if (xml_tag_matches(xpc, name, "DAV:", "href")) {
		// store the href for the current response element
		g_assert_null(xpc->current_href);
		xpc->current_href = g_strdup(xpc->chars.str);
	} else if (xml_tag_matches(xpc, name, "DAV:", "status")) {
		// store the status for the current response element
		sscanf(xpc->chars.str, "HTTP/1.1 %d ", &xpc->status);
	} else if (xml_tag_matches(xpc, name, "DAV:", "response")) {
		g_assert_nonnull(xpc->current_href);
		SyncEntry* e = malloc(sizeof(SyncEntry));
		e->href = xpc->current_href;
		e->status = xpc->status;
		xpc->result_list = g_slist_append(xpc->result_list, e);
		xpc->current_href = NULL;
		xpc->status = 0;
	} else if (xml_tag_matches(xpc, name, "DAV:", "sync-token")) {
		xpc->sync_token = strdup(xpc->chars.str);
	}
	xmlparse_ns_pop(xpc);
}
// Initializes an XmlParseCtx. Those members which are only used in specific operations
// must be allocated manually after calling this function
static void xmlctx_init(XmlParseCtx* ctx)
{
	memset(ctx, 0, sizeof(XmlParseCtx));
	ctx->ns_defaults = g_queue_new();
	ctx->ns_aliases = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	g_string_set_size(&ctx->chars, 0);
}

// Cleans up an XmlParseCtx. Any members allocated manually after xmlctx_init must also
// be cleaned up manually
static void xmlctx_cleanup(XmlParseCtx* ctx)
{
	g_queue_free_full(ctx->ns_defaults, (GDestroyNotify) xmlns_free);
	g_hash_table_remove_all(ctx->ns_aliases);
	g_free(ctx->chars.str);
	g_hash_table_destroy(ctx->ns_aliases);
}

typedef struct {
	CaldavCalendar* cal;
	char* url;
	char* cal_postdata;
	Event* old_event;
	Event* new_event;
} ModifyContext;

static void caldav_modify_done(CURL* curl, CURLcode ret, void* user)
{
	ModifyContext* ac = (ModifyContext*) user;
	free(ac->cal_postdata);
	free(ac->url);

	if (ret == CURLE_OK) {
		// RFC 4791 5.3.4 states that if the object stored on the server side is not
		// octet-identical to the one in the PUT request, an ETag won't be supplied
		// and we need to retrieve the object afresh.
		if (ac->new_event && !event_get_etag(ac->new_event)) {
			g_warning("No ETag in PUT response, triggering sync. Event LEAKS!");
			CaldavCalendar* cc = ac->cal;
			// Cannot call event_free because it's still being used in the display, and
			// won't be refreshed until the sync completes. For now we accept the small
			// leak, later it would be better to perhaps remove it from the display
			// before triggering the sync
			// event_free(ac->new_event);
			g_free(ac);
			cc->op_pending = FALSE;
			calendar_sync(FOCAL_CALENDAR(cc));
			return;
		}

		// "officially" append the event to the collection
		if (ac->old_event)
			ac->cal->events = g_slist_remove(ac->cal->events, ac->old_event);
		if (ac->new_event)
			ac->cal->events = g_slist_append(ac->cal->events, ac->new_event);

		// reuse the sync-done event since for now the action is the same -> refresh the UI
		g_signal_emit_by_name(ac->cal, "sync-done", 0);
	} else {
		// TODO report error via UI
		fprintf(stderr, "curl error: %s\n", curl_easy_strerror(ret));
	}

	ac->cal->op_pending = FALSE;
	g_free(ac);
}

static size_t caldav_put_response_get_etag(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	const int header_len = strlen("ETag: ");
	if (strncmp(ptr, "ETag: ", header_len) == 0) {
		char* etag = strdup(ptr + header_len);
		// truncate CRLF
		*strchrnul(etag, '\r') = 0;
		event_update_etag((Event*) userdata, etag);
	}
	return size * nmemb;
}

static void do_caldav_put(CaldavCalendar* rc, CURL* curl, struct curl_slist* headers, Event* event)
{
	ModifyContext* ac = g_new0(ModifyContext, 1);
	ac->cal = rc;
	ac->new_event = event;

	const char* event_url = event_get_url(event);
	if (event_url == NULL) {
		char* u = g_strdup_printf("%s%s.ics", strchrnul(strchr(calendar_get_location(FOCAL_CALENDAR(rc)), ':') + 3, '/'), event_get_uid(event));
		event_set_url(event, u);
		g_free(u);
		event_url = event_get_url(event);
	}

	// TODO cleaner?
	const char* root_url = calendar_get_location(FOCAL_CALENDAR(rc));
	const char* url_path = strchrnul(strchr(root_url, ':') + 3, '/');
	asprintf(&ac->url, "%.*s%s", (int) (url_path - root_url), root_url, event_get_url(event));

	curl_easy_setopt(curl, CURLOPT_URL, ac->url);

	headers = curl_slist_append(headers, "Content-Type: text/calendar; charset=utf-8");
	headers = curl_slist_append(headers, "Expect:");

	GSList* f = g_slist_find(rc->events, event);
	if (f)
		ac->old_event = f->data;

	if (ac->old_event) {
		char* match;
		asprintf(&match, "If-Match: %s", event_get_etag(ac->old_event));
		headers = curl_slist_append(headers, match);
		free(match);
	} else {
		headers = curl_slist_append(headers, "If-None-Match: *");
	}

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	ac->cal_postdata = event_as_ical_string(ac->new_event);

	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ac->cal_postdata);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(ac->cal_postdata));

	curl_easy_setopt(curl, CURLOPT_HEADERDATA, ac->new_event);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, caldav_put_response_get_etag);

	async_curl_add_request(curl, headers, caldav_modify_done, ac);
}

#define ENSURE_EXCLUSIVE(rc)                                                                   \
	do {                                                                                       \
		if (rc->op_pending) {                                                                  \
			g_warning("Operation already pending, early return from %s", __PRETTY_FUNCTION__); \
			return;                                                                            \
		}                                                                                      \
		rc->op_pending = TRUE;                                                                 \
	} while (0)

static void save_event(Calendar* c, Event* event)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	ENSURE_EXCLUSIVE(rc);
	remote_auth_new_request(rc->auth, do_caldav_put, rc, event);
}

static void do_delete_event(CaldavCalendar* rc, CURL* curl, struct curl_slist* headers, Event* event)
{
	ModifyContext* pc = g_new0(ModifyContext, 1);
	pc->cal = rc;
	pc->old_event = event;
	// TODO is this if stmt really needed?
	const char* event_url = event_get_url(event);
	g_assert(event_url);
	if (event_url == NULL) {
		event_set_url(event, g_strdup_printf("%s%s.ics", strchrnul(strchr(calendar_get_location(FOCAL_CALENDAR(rc)), ':') + 3, '/'), event_get_uid(event)));
		event_url = event_get_url(event);
	}

	const char* root_url = calendar_get_location(FOCAL_CALENDAR(rc));
	char* url_path = strchrnul(strchr(root_url, ':') + 3, '/');
	asprintf(&pc->url, "%.*s%s", (int) (url_path - root_url), root_url, event_url);

	curl_easy_setopt(curl, CURLOPT_URL, pc->url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

	// set the If-Match header
	char* match;
	asprintf(&match, "If-Match: %s", event_get_etag(event));
	headers = curl_slist_append(headers, match);
	free(match);

	async_curl_add_request(curl, headers, caldav_modify_done, pc);
}

static void delete_event(Calendar* c, Event* event)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	ENSURE_EXCLUSIVE(rc);

	// if the event has no etag, it has never been added to this calendar
	if (!event_get_etag(event))
		return;

	remote_auth_new_request(rc->auth, do_delete_event, rc, event);
}

static void each_event(Calendar* c, CalendarEachEventCallback callback, void* user)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	for (GSList* p = rc->events; p; p = p->next) {
		callback(user, (Event*) p->data);
	}
}

static void free_events(CaldavCalendar* rc)
{
	g_slist_free_full(rc->events, (GDestroyNotify) event_free);
}

static Event* create_event_from_parsed_xml(CaldavCalendar* cal, CaldavEntry* cde)
{
	// event updated
	icalcomponent* comp = icalparser_parse_string(cde->caldata);
	icalcomponent* vev = icalcomponent_get_first_component(comp, ICAL_VEVENT_COMPONENT);
	g_assert_nonnull(vev);
	Event* ev = event_new_from_icalcomponent(vev);
	event_set_calendar(ev, FOCAL_CALENDAR(cal));
	event_set_url(ev, cde->href);
	free(cde->href);
	event_update_etag(ev, cde->etag);
	free(cde->caldata);
	free(cde);
	return ev;
}

static void caldav_entry_free(CaldavEntry* cde)
{
	free(cde->href);
	free(cde->etag);
	free(cde->caldata);
	free(cde);
}

typedef struct {
	CaldavCalendar* cal;
	GString* report_req;
	GString* report_resp;
} SyncContext;

static void sync_multiget_report_done(CURL* curl, CURLcode ret, void* user)
{
	SyncContext* sc = (SyncContext*) user;
	CaldavCalendar* rc = sc->cal;

	g_string_free(sc->report_req, TRUE);

	// Debug
	//printf("sync done: [%s]\n", sc->report_resp->str);

	// Handle the case where the http request failed
	if (ret != CURLE_OK) {
		// TODO report error via UI
		fprintf(stderr, "curl error: %s\n", curl_easy_strerror(ret));
		sc->cal->op_pending = FALSE;
		g_string_free(sc->report_resp, TRUE);
		free(sc);
		return;
	}

	// Parse the response
	XmlParseCtx ctx;
	xmlctx_init(&ctx);
	xmlSAXHandler my_handler = {.characters = xmlparse_characters,
								.startElement = xmlparse_tag_open,
								.endElement = xmlparse_find_caldata};
	xmlSAXUserParseMemory(&my_handler, &ctx, sc->report_resp->str, sc->report_resp->len);
	xmlctx_cleanup(&ctx);
	g_string_free(sc->report_resp, TRUE);
	// ctx.result_list now contains a GSList of CaldavEntry structs

	// Debug counters
	int nUpdated = 0, nNew = 0;

	// Merge the results from the sync with the existing events
	GSList** n = &ctx.result_list;
	while (*n) {
		CaldavEntry* cde = (*n)->data;
		// Look through the existing list of events for matching resources
		for (GSList** p = &sc->cal->events; *p; p = &(*p)->next) {
			Event* ee = (Event*) (*p)->data;
			if (strcmp(cde->href, event_get_url(ee)) == 0) {
				if (cde->caldata) {
					// event updated
					if (strcmp(cde->etag, event_get_etag(ee)) == 0) {
						// we already knew about this update (we probably did it ourselves). Just ignore it.
						caldav_entry_free(cde);
					} else {
						// replace the old event with the new one
						event_free((*p)->data);
						(*p)->data = create_event_from_parsed_xml(rc, cde);
						nUpdated++;
					}
				} else {
					// If the calendar-data is NULL, assume the event is deleted. This can
					// happen if an event is removed after the sync-collection request but
					// before the response to this multiget.
					event_free((*p)->data);
					*p = (*p)->next;
					caldav_entry_free(cde);
				}
				// Since a match was found and dealt with, remove this entry from the list
				gpointer next = (*n)->next;
				g_slist_free_1(*n);
				*n = next;
				// break & continue,
				goto sync_populate_local_list_event_matched;
			}
		}
		// if we get here, there was no matching resource in the existing list.
		// so move onto the next entry. If it was matched we have already advanced
		n = &(*n)->next;
		// empty statement for "break & continue"
	sync_populate_local_list_event_matched:
		(void) 0;
	}

	// Everything remaining in ctx.event_list should be new events
	for (GSList* e = ctx.result_list; e; e = e->next) {
		CaldavEntry* cde = e->data;
		// A removal or permission denied that was not matched in the local list above will still be here,
		// but its caldata member will be empty. So it can be ignored.
		if (cde->caldata) {
			Event* event = create_event_from_parsed_xml(rc, cde);
			sc->cal->events = g_slist_append(sc->cal->events, event);
			nNew++;
		} else {
			caldav_entry_free(cde);
		}
	}
	g_slist_free(ctx.result_list);

	// print debug counters
	printf("sync: %d updated, %d new\n", nUpdated, nNew);

	g_free(sc);

	// All done, notify
	rc->op_pending = FALSE;
	g_signal_emit_by_name(rc, "sync-done", 0);
}

static void do_multiget_events(CaldavCalendar* rc, CURL* curl, struct curl_slist* headers, GSList* hrefs)
{
	SyncContext* sc = g_new0(SyncContext, 1);
	sc->cal = rc;

	// According to RFC6578 Appendix B, the next step is to send GET requests
	// for each resource returned in the earlier sync-collection REPORT method.
	// Here we instead use a calendar-multiget REPORT for efficiency.
	// See https://tools.ietf.org/html/rfc6578#appendix-B

	curl_easy_setopt(curl, CURLOPT_URL, calendar_get_location(FOCAL_CALENDAR(rc)));

	headers = curl_slist_append(headers, "Depth: 1");
	headers = curl_slist_append(headers, "Prefer: return-minimal");
	headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "REPORT");

	// Build the query
	sc->report_req = g_string_new(
		"<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
		"<C:calendar-multiget xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
		"  <D:prop>"
		"    <D:getetag/>"
		"    <C:calendar-data/>"
		"  </D:prop>");
	for (GSList* p = hrefs; p; p = p->next)
		g_string_append_printf(sc->report_req, "<D:href>%s</D:href>", (const char*) p->data);
	g_string_append(sc->report_req, "</C:calendar-multiget>");

	// Free the list of hrefs
	g_slist_free_full(hrefs, free);

	// Finalise and fire the multiget request
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, sc->report_req->str);

	sc->report_resp = g_string_new(NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, sc->report_resp);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);

	async_curl_add_request(curl, headers, sync_multiget_report_done, sc);
}

static void do_caldav_sync(CaldavCalendar* rc, CURL* curl, struct curl_slist* headers);

static void sync_collection_report_done(CURL* curl, CURLcode ret, void* user)
{
	SyncContext* sc = (SyncContext*) user;
	CaldavCalendar* rc = sc->cal;

	g_string_free(sc->report_req, TRUE);

	// Debug
	//printf("sync done: [%s]\n", sc->report_resp->str);

	// Handle the case where the http request failed
	if (ret != CURLE_OK) {
		// TODO report error via UI
		fprintf(stderr, "curl error: %s\n", curl_easy_strerror(ret));
		sc->cal->op_pending = FALSE;
		g_string_free(sc->report_resp, TRUE);
		free(sc);
		return;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code == 401) {
		g_warning("401 Unauthorized. Assuming auth token has expired and attempting refresh");
		remote_auth_invalidate_credential(rc->auth, do_caldav_sync, rc, NULL);
		return;
	} else if (response_code != 207) {
		g_critical("unexpected response code %ld", response_code);
	}

	// Parse the response
	XmlParseCtx ctx;
	xmlctx_init(&ctx);
	xmlSAXHandler my_handler = {.characters = xmlparse_characters,
								.startElement = xmlparse_tag_open,
								.endElement = xmlparse_report_sync_collection};
	xmlSAXUserParseMemory(&my_handler, &ctx, sc->report_resp->str, sc->report_resp->len);
	xmlctx_cleanup(&ctx);
	g_string_free(sc->report_resp, TRUE);

	// Store the new sync-token for subsequent sync operations
	free(sc->cal->sync_token);
	sc->cal->sync_token = ctx.sync_token; // (xfer ownership)

	// Any resource that returned a 404 shall be deleted from the local collection.
	// All others will be queried. Servers cannot be relied on to deliver status 200 here.
	int nDeleted = 0;
	GSList* hrefs = NULL;
	for (GSList* s = ctx.result_list; s; s = s->next) {
		SyncEntry* se = s->data;
		if (se->status == 404) {
			for (GSList** p = &sc->cal->events; *p; p = &(*p)->next) {
				Event* ee = (Event*) (*p)->data;
				if (strcmp(se->href, event_get_url(ee)) == 0) {
					event_free((*p)->data);
					*p = (*p)->next;
					nDeleted++;
					break;
				}
			}
			free(se->href);
		} else {
			hrefs = g_slist_append(hrefs, se->href);
		}
	}
	g_slist_free_full(ctx.result_list, free);

	free(sc);

	// early return in the case where there are no new or updated events
	if (hrefs == NULL) {
		if (nDeleted) {
			printf("sync: %d deleted\n", nDeleted);
		} else {
			printf("sync: no changes\n");
		}
		// sync-done here is necessary if items were deleted OR it's the initial sync.
		// We know whether we deleted something but don't know if this is an initial sync.
		g_signal_emit_by_name(rc, "sync-done", 0);
		rc->op_pending = FALSE;
		return;
	}

	remote_auth_new_request(rc->auth, do_multiget_events, rc, hrefs);
}

static void do_caldav_sync(CaldavCalendar* rc, CURL* curl, struct curl_slist* headers)
{
	// Begin sync operation. According to RFC6578, the first step is to send
	// a sync-collection REPORT to retrieve a list of hrefs that have been
	// updated since the last call to the API (identified by the sync-token)
	// See https://tools.ietf.org/html/rfc6578#appendix-B
	curl_easy_setopt(curl, CURLOPT_URL, calendar_get_location(FOCAL_CALENDAR(rc)));

	// Userdata for the sync operation
	SyncContext* sc = g_new0(SyncContext, 1);
	sc->cal = rc;

	headers = curl_slist_append(headers, "Depth: 1");
	headers = curl_slist_append(headers, "Prefer: return-minimal");
	headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "REPORT");

	sc->report_req = g_string_new("");
	g_string_append_printf(sc->report_req,
						   "<d:sync-collection xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
						   "  <d:sync-token>%s</d:sync-token>"
						   "  <d:sync-level>infinite</d:sync-level>"
						   "  <d:prop><d:getetag/></d:prop>" // dav:prop always required by sabredav, getetag required by google
						   "</d:sync-collection>",
						   rc->sync_token);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, sc->report_req->str);

	sc->report_resp = g_string_new(NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, sc->report_resp);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);

	async_curl_add_request(curl, headers, sync_collection_report_done, sc);
}

static void caldav_sync(Calendar* c)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	ENSURE_EXCLUSIVE(rc);

	remote_auth_new_request(rc->auth, do_caldav_sync, rc, NULL);
}

static gboolean caldav_is_read_only(Calendar* c)
{
	// TODO
	return FALSE;
}

static void caldav_auth_cancelled(CaldavCalendar* rc)
{
	rc->op_pending = FALSE;
}

static void constructed(GObject* gobject)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(gobject);
	// url must end with a slash. TODO don't assert
	g_assert_true(strrchr(calendar_get_location(FOCAL_CALENDAR(rc)), '/')[1] == 0);
	// auth member must have been supplied by attach_authenticator
	g_assert_nonnull(rc->auth);
	rc->events = NULL;
	rc->sync_token = g_strdup("");
	g_signal_connect_swapped(rc->auth, "cancelled", G_CALLBACK(caldav_auth_cancelled), rc);
}

static void finalize(GObject* gobject)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(gobject);
	g_object_unref(rc->auth);
	free(rc->sync_token);
	free_events(rc);
	G_OBJECT_CLASS(caldav_calendar_parent_class)->finalize(gobject);
}

void caldav_calendar_init(CaldavCalendar* rc)
{
}

static void attach_authenticator(Calendar* c, RemoteAuth* auth)
{
	FOCAL_CALDAV_CALENDAR(c)->auth = auth;
}

void caldav_calendar_class_init(CaldavCalendarClass* klass)
{
	FOCAL_CALENDAR_CLASS(klass)->save_event = save_event;
	FOCAL_CALENDAR_CLASS(klass)->delete_event = delete_event;
	FOCAL_CALENDAR_CLASS(klass)->each_event = each_event;
	FOCAL_CALENDAR_CLASS(klass)->sync = caldav_sync;
	FOCAL_CALENDAR_CLASS(klass)->read_only = caldav_is_read_only;

	FOCAL_CALENDAR_CLASS(klass)->attach_authenticator = attach_authenticator;
	G_OBJECT_CLASS(klass)->constructed = constructed;
	G_OBJECT_CLASS(klass)->finalize = finalize;
}
