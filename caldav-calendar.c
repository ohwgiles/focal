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
#include "event-private.h"

struct _CaldavCalendar {
	Calendar parent;
	char* url;
	GSList* events;
};
G_DEFINE_TYPE(CaldavCalendar, caldav_calendar, TYPE_CALENDAR)

typedef struct {
	int depth;
	char* name;
} XmlNs;

typedef struct {
	GString chars;
	int depth;
	GQueue* ns_defaults;
	GHashTable* ns_aliases;
	GSList* hrefs;
	GSList* event_list;
	char* current_href;
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
#if 0
// Callback when SAX parser finds a closing XML tag during calendar init
static void xmlparse_init_close(void* ctx, const xmlChar* name)
{
	XmlParseCtx* xpc = (XmlParseCtx*) ctx;
	// TODO save ctag and displayname
	xmlparse_ns_pop(xpc);
}
#endif
// Callback when SAX parser finds a closing XML tag during calendar-data REPORT
static void xmlparse_find_caldata(void* ctx, const xmlChar* name)
{
	XmlParseCtx* xpc = (XmlParseCtx*) ctx;
	if (xml_tag_matches(xpc, name, "DAV:", "href")) {
		// store the href for the current response element
		g_assert_null(xpc->current_href);
		xpc->current_href = g_strdup(xpc->chars.str);
	} else if (xml_tag_matches(xpc, name, "DAV:", "response")) {
		g_assert_nonnull(xpc->current_href);
		g_free(xpc->current_href);
		xpc->current_href = NULL;
	} else if (xml_tag_matches(xpc, name, "urn:ietf:params:xml:ns:caldav", "calendar-data")) {
		icalcomponent* comp = icalparser_parse_string(xpc->chars.str);
		icalcomponent* vevent = icalcomponent_get_first_component(comp, ICAL_VEVENT_COMPONENT);

		if (vevent) {
			EventPrivate* fp = icalcomponent_create_private(vevent);
			fp->url = g_strdup(xpc->current_href);
			xpc->event_list = g_slist_append(xpc->event_list, vevent);
		}
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

static size_t curl_write_to_gstring(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	g_string_append_len((GString*) userdata, ptr, size * nmemb);
	return size * nmemb;
}
#if 0
static gboolean query_displayname(RemoteCalendar* rc)
{
	CURLcode ret;
	CURL* curl = curl_easy_init();

	if (!curl)
		return FALSE;

	const CalendarConfig* cfg = calendar_get_config(FOCAL_CALENDAR(rc));

	curl_easy_setopt(curl, CURLOPT_URL, rc->url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Depth: 0");
	headers = curl_slist_append(headers, "Prefer: return-minimal");
	headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(curl, CURLOPT_USERNAME, cfg->d.caldav.user);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg->d.caldav.pass);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);

	curl_easy_setopt(curl, CURLOPT_POSTFIELDS,
	                 "<propfind xmlns=\"DAV:\">"
	                 "  <prop>"
	                 "     <displayname />"
	                 "     <getctag xmlns=\"http://calendarserver.org/ns/\"/>"
	                 "  </prop>"
	                 "</propfind>");

	GString* init_resp = g_string_new(NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, init_resp);

	ret = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (ret != CURLE_OK) {
		g_string_free(init_resp, TRUE);
		return FALSE;
	}

	XmlParseCtx ctx;
	xmlctx_init(&ctx);
	xmlSAXHandler my_handler = {.characters = xmlparse_characters,
	                            .startElement = xmlparse_tag_open,
	                            .endElement = xmlparse_init_close};
	xmlSAXUserParseMemory(&my_handler, &ctx, init_resp->str, init_resp->len);
	xmlctx_cleanup(&ctx);
	g_string_free(init_resp, TRUE);

	return TRUE;
}
#endif

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

/* Helper function for the common aspects of a CalDAV request */
static CURL* new_curl_request(const CalendarConfig* cfg, const char* url)
{
	CURL* curl = curl_easy_init();
	g_assert_nonnull(curl);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(curl, CURLOPT_USERNAME, cfg->d.caldav.user);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg->d.caldav.pass);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
	return curl;
}

typedef struct {
	CaldavCalendar* cal;
	char* url;
	struct curl_slist* hdrs;
	char* cal_postdata;
	icalcomponent* old_event;
	icalcomponent* new_event;
} ModifyContext;

static void caldav_modify_done(CURL* curl, CURLcode ret, void* user)
{
	ModifyContext* ac = (ModifyContext*) user;
	curl_slist_free_all(ac->hdrs);
	free(ac->cal_postdata);
	free(ac->url);

	if (ret == CURLE_OK) {
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

	g_free(ac);
}

static void caldav_put(CaldavCalendar* rc, icalcomponent* event, icalcomponent* replaces)
{
	EventPrivate* priv = icalcomponent_get_private(event);

	ModifyContext* ac = g_new0(ModifyContext, 1);
	ac->cal = rc;
	ac->old_event = replaces;
	ac->new_event = event;
	const char* url_path = strchrnul(strchr(rc->url, ':') + 3, '/');
	asprintf(&ac->url, "%.*s%s", (int) (url_path - rc->url), rc->url, priv->url);

	CURL* curl = new_curl_request(calendar_get_config(FOCAL_CALENDAR(rc)), ac->url);
	ac->hdrs = curl_slist_append(ac->hdrs, "Content-Type: text/calendar; charset=utf-8");
	ac->hdrs = curl_slist_append(ac->hdrs, "Expect:");
	/* TODO: use etag */
	if (!replaces)
		ac->hdrs = curl_slist_append(ac->hdrs, "If-None-Match: *");
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, ac->hdrs);
	ac->cal_postdata = icalcomponent_as_ical_string(icalcomponent_get_parent(event));
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ac->cal_postdata);

	async_curl_add_request(curl, caldav_modify_done, ac);
}

static void add_event(Calendar* c, icalcomponent* event)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);

	EventPrivate* priv = icalcomponent_create_private(event);
	const char* uid = icalcomponent_get_uid(event);
	if (uid == NULL) {
		char* p = generate_ical_uid();
		icalcomponent_set_uid(event, p);
		free(p);
		uid = icalcomponent_get_uid(event);
	}
	const char* url_path = strchrnul(strchr(rc->url, ':') + 3, '/');
	asprintf(&priv->url, "%s/%s.ics", url_path, icalcomponent_get_uid(event));
	priv->cal = c;

	icalcomponent* parent = icalcomponent_get_parent(event);
	/* The event should have no parent if it was created here, or moved
	 * from another calendar, but it might have one if created from an
	 * invite file */
	if (parent == NULL) {
		parent = icalcomponent_new_vcalendar();
		icalcomponent_add_component(parent, event);
	}

	caldav_put(rc, event, NULL);
}

static void update_event(Calendar* c, icalcomponent* event)
{
	caldav_put(FOCAL_CALDAV_CALENDAR(c), event, event);
}

static void delete_event(Calendar* c, icalcomponent* event)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	EventPrivate* priv = icalcomponent_get_private(event);

	ModifyContext* pc = g_new0(ModifyContext, 1);
	pc->cal = rc;
	pc->old_event = event;
	// TODO is this if stmt really needed?
	if (priv->url) {
		char* url_path = strchrnul(strchr(rc->url, ':') + 3, '/');
		asprintf(&pc->url, "%.*s%s", (int) (url_path - rc->url), rc->url, priv->url);
	} else {
		asprintf(&pc->url, "%s/%s.ics", rc->url, icalcomponent_get_uid(event));
	}

	CURL* curl = new_curl_request(calendar_get_config(c), pc->url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	// TODO use caldav etag
	// headers = curl_slist_append(headers, "If-Match: ETAG_HERE");
	async_curl_add_request(curl, caldav_modify_done, pc);
}

static void each_event(Calendar* c, CalendarEachEventCallback callback, void* user)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	for (GSList* p = rc->events; p; p = p->next) {
		callback(user, (Calendar*) rc, (icalcomponent*) p->data);
	}
}

static void free_events(CaldavCalendar* rc)
{
	for (GSList* p = rc->events; p; p = p->next) {
		if (p->data) {
			icalcomponent* ev = (icalcomponent*) p->data;
			free(icalcomponent_get_private(ev)->url);
			icalcomponent_free_private(ev);
			icalcomponent_free(icalcomponent_get_parent(ev));
		}
	}
	g_slist_free(rc->events);
}

typedef struct {
	CaldavCalendar* cal;
	struct curl_slist* hdrs;
	GString* report_req;
	GString* report_resp;
} SyncContext;

static void sync_done(CURL* curl, CURLcode ret, void* user)
{
	SyncContext* sc = (SyncContext*) user;

	curl_slist_free_all(sc->hdrs);
	g_string_free(sc->report_req, TRUE);

	if (ret == CURLE_OK) {
		XmlParseCtx ctx;
		xmlctx_init(&ctx);
		xmlSAXHandler my_handler = {.characters = xmlparse_characters,
									.startElement = xmlparse_tag_open,
									.endElement = xmlparse_find_caldata};
		xmlSAXUserParseMemory(&my_handler, &ctx, sc->report_resp->str, sc->report_resp->len);
		xmlctx_cleanup(&ctx);

		sc->cal->events = ctx.event_list;
		for (GSList* p = sc->cal->events; p; p = p->next) {
			icalcomponent* ev = p->data;
			icalcomponent_get_private(ev)->cal = FOCAL_CALENDAR(sc->cal);
		}
		g_signal_emit_by_name(sc->cal, "sync-done", 0);
	} else {
		// TODO report error via UI
		fprintf(stderr, "curl error: %s\n", curl_easy_strerror(ret));
	}

	g_string_free(sc->report_resp, TRUE);
	g_free(sc);
}

static void caldav_sync(Calendar* c)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	free_events(rc);

	CURL* curl = new_curl_request(calendar_get_config(c), rc->url);

	SyncContext* sc = g_new0(SyncContext, 1);
	sc->cal = rc;

	sc->hdrs = curl_slist_append(sc->hdrs, "Depth: 1");
	sc->hdrs = curl_slist_append(sc->hdrs, "Prefer: return-minimal");
	sc->hdrs = curl_slist_append(sc->hdrs, "Content-Type: application/xml; charset=utf-8");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, sc->hdrs);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "REPORT");

	sc->report_req = g_string_new(
		"<d:sync-collection xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
		"  <d:sync-token></d:sync-token>"
		"  <d:sync-level>1</d:sync-level>"
		"  <d:prop>"
		"    <c:calendar-data/>"
		"    <d:getetag/>"
		"  </d:prop>"
		"</d:sync-collection>");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, sc->report_req->str);

	sc->report_resp = g_string_new(NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, sc->report_resp);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);

	async_curl_add_request(curl, sync_done, sc);
}

static void finalize(GObject* gobject)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(gobject);
	free_events(rc);
	free(rc->url);
	G_OBJECT_CLASS(caldav_calendar_parent_class)->finalize(gobject);
}

void caldav_calendar_init(CaldavCalendar* rc)
{
}

void caldav_calendar_class_init(CaldavCalendarClass* klass)
{
	FOCAL_CALENDAR_CLASS(klass)->add_event = add_event;
	FOCAL_CALENDAR_CLASS(klass)->update_event = update_event;
	FOCAL_CALENDAR_CLASS(klass)->delete_event = delete_event;
	FOCAL_CALENDAR_CLASS(klass)->each_event = each_event;
	FOCAL_CALENDAR_CLASS(klass)->sync = caldav_sync;
	G_OBJECT_CLASS(klass)->finalize = finalize;
}

Calendar* caldav_calendar_new(const char* url, const char* username, const char* password)
{
	CaldavCalendar* rc = g_object_new(CALDAV_CALENDAR_TYPE, NULL);
	rc->url = strdup(url);
	rc->events = NULL;
	// TODO cfg is not set yet, but this is not yet useful
	//query_displayname(rc);
	return (Calendar*) rc;
}

void caldav_calendar_free(CaldavCalendar* rc)
{
	free(rc->url);
	free_events(rc);
	g_free(rc);
}
