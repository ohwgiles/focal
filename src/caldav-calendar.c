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
#include <libsecret/secret.h>
#include <libxml/SAX2.h>

#include "async-curl.h"
#include "caldav-calendar.h"
#include "event-private.h"

struct _CaldavCalendar {
	Calendar parent;
	const CalendarConfig* cfg;
	char* sync_token;
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

static size_t curl_write_to_gstring(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	g_string_append_len((GString*) userdata, ptr, size * nmemb);
	return size * nmemb;
}

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
static CURL* new_curl_request(const CalendarConfig* cfg, const char* url, const char* pass)
{
	CURL* curl = curl_easy_init();
	g_assert_nonnull(curl);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(curl, CURLOPT_USERNAME, cfg->d.caldav.user);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, pass);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
	return curl;
}

static const SecretSchema focal_secret_schema = {
	"net.ohwg.focal", SECRET_SCHEMA_NONE, {
											  {"url", SECRET_SCHEMA_ATTRIBUTE_STRING},
											  {"user", SECRET_SCHEMA_ATTRIBUTE_STRING},
											  {"NULL", 0},
										  }};

// Simple generic structure to pass a callback function and its arguments
// so that an action can be continued after an intermediate step. It should be
// allocated by the caller of caldav_secret_lookup, and will be freed after
// the callback is executed (see on_password_stored)
typedef struct {
	void (*func)();
	CaldavCalendar* cal;
	void* arg1;
	void* arg2;
} DelayedFunctionContext;

static void on_password_lookup(GObject* source, GAsyncResult* result, gpointer user);

static void caldav_secret_lookup(CaldavCalendar* rc, DelayedFunctionContext* dfc)
{
	secret_password_lookup(&focal_secret_schema, NULL, on_password_lookup, dfc,
						   "url", rc->cfg->d.caldav.url,
						   "user", rc->cfg->d.caldav.user,
						   NULL);
}

static void on_password_stored(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	DelayedFunctionContext* dfc = (DelayedFunctionContext*) user;
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(dfc->cal);

	secret_password_store_finish(result, &error);

	if (error != NULL) {
		fprintf(stderr, "error: %s\n", error->message);
		g_error_free(error);
		free(dfc);
	} else {
		// immediately look up the password again so we can continue the originally
		// requested async operation
		caldav_secret_lookup(rc, dfc);
	}
}

static void on_password_lookup(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	gchar* password = secret_password_lookup_finish(result, &error);
	DelayedFunctionContext* dfc = (DelayedFunctionContext*) user;
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(dfc->cal);

	if (error != NULL) {
		fprintf(stderr, "error: %s\n", error->message);
		g_error_free(error);
	} else if (password == NULL) {
		// no matching password found, prompt the user to create one
		char* pass = NULL;
		g_signal_emit_by_name(rc, "request-password", rc->cfg->name, rc->cfg->d.caldav.user, &pass);
		if (pass) {
			// We could just use the password here, but there's a good chance that it
			// would be immediately required again, and the next lookup would pop up
			// another dialog. Instead, wait until the password is stored, then trigger
			// a lookup again with the original callback data
			secret_password_store(&focal_secret_schema, SECRET_COLLECTION_DEFAULT, "Focal Remote Calendar password",
								  pass, NULL, on_password_stored, dfc,
								  "url", rc->cfg->d.caldav.url,
								  "user", rc->cfg->d.caldav.user,
								  NULL);
			g_free(pass);
			// skip free of dfc, it will be cleaned up in on_password_stored callback
			return;
		} else {
			// user declined to enter password. Allow a later attempt...
			rc->op_pending = FALSE;
		}
	} else {
		rc->op_pending = FALSE;
		(*dfc->func)(dfc->cal, password, dfc->arg1, dfc->arg2);
		secret_password_free(password);
	}
	free(dfc);
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

static size_t caldav_put_response_get_etag(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	const int header_len = strlen("ETag: ");
	if (strncmp(ptr, "ETag: ", header_len) == 0) {
		char* etag = strdup(ptr + header_len);
		// truncate CRLF
		*strchrnul(etag, '\r') = 0;
		icalcomponent_get_private((icalcomponent*) userdata)->etag = etag;
	}
	return size * nmemb;
}

static void do_caldav_put(CaldavCalendar* rc, char* password, icalcomponent* event, icalcomponent* replaces)
{
	EventPrivate* priv = icalcomponent_get_private(event);

	ModifyContext* ac = g_new0(ModifyContext, 1);
	ac->cal = rc;
	ac->old_event = replaces;
	ac->new_event = event;
	const char* root_url = rc->cfg->d.caldav.url;

	const char* url_path = strchrnul(strchr(root_url, ':') + 3, '/');
	asprintf(&ac->url, "%.*s%s", (int) (url_path - root_url), root_url, priv->url);

	CURL* curl = new_curl_request(calendar_get_config(FOCAL_CALENDAR(rc)), ac->url, password);
	ac->hdrs = curl_slist_append(ac->hdrs, "Content-Type: text/calendar; charset=utf-8");
	ac->hdrs = curl_slist_append(ac->hdrs, "Expect:");

	if (ac->old_event) {
		char* match;
		asprintf(&match, "If-Match: %s", icalcomponent_get_private(ac->old_event)->etag);
		ac->hdrs = curl_slist_append(ac->hdrs, match);
		free(match);
	} else {
		ac->hdrs = curl_slist_append(ac->hdrs, "If-None-Match: *");
	}

	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, ac->hdrs);
	ac->cal_postdata = icalcomponent_as_ical_string(icalcomponent_get_parent(ac->new_event));
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ac->cal_postdata);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(ac->cal_postdata));

	curl_easy_setopt(curl, CURLOPT_HEADERDATA, ac->new_event);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, caldav_put_response_get_etag);

	async_curl_add_request(curl, caldav_modify_done, ac);
}

#define ENSURE_EXCLUSIVE(rc)                                                                   \
	do {                                                                                       \
		if (rc->op_pending) {                                                                  \
			g_warning("Operation already pending, early return from %s", __PRETTY_FUNCTION__); \
			return;                                                                            \
		}                                                                                      \
		rc->op_pending = TRUE;                                                                 \
	} while (0)

static void add_event(Calendar* c, icalcomponent* event)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	ENSURE_EXCLUSIVE(rc);

	EventPrivate* priv = icalcomponent_create_private(event);
	const char* uid = icalcomponent_get_uid(event);
	if (uid == NULL) {
		char* p = generate_ical_uid();
		icalcomponent_set_uid(event, p);
		free(p);
		uid = icalcomponent_get_uid(event);
	}
	const char* url_path = strchrnul(strchr(rc->cfg->d.caldav.url, ':') + 3, '/');
	asprintf(&priv->url, "%s%s.ics", url_path, icalcomponent_get_uid(event));
	priv->cal = c;

	icalcomponent* parent = icalcomponent_get_parent(event);
	/* The event should have no parent if it was created here, or moved
	 * from another calendar, but it might have one if created from an
	 * invite file */
	if (parent == NULL) {
		parent = icalcomponent_new_vcalendar();
		icalcomponent_add_property(parent, icalproperty_new_version("2.0"));
		icalcomponent_add_property(parent, icalproperty_new_prodid("-//OHWG//FOCAL"));
		icalcomponent_add_component(parent, event);
	}

	DelayedFunctionContext* dfc = g_new0(DelayedFunctionContext, 1);
	dfc->cal = rc;
	dfc->func = do_caldav_put;
	dfc->arg1 = event;

	caldav_secret_lookup(rc, dfc);
}

static void update_event(Calendar* c, icalcomponent* event)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	ENSURE_EXCLUSIVE(rc);

	DelayedFunctionContext* dfc = g_new0(DelayedFunctionContext, 1);
	dfc->cal = rc;
	dfc->func = do_caldav_put;
	dfc->arg1 = event;
	dfc->arg2 = event;

	caldav_secret_lookup(rc, dfc);
}

static void do_delete_event(CaldavCalendar* rc, const char* password, icalcomponent* event)
{
	EventPrivate* priv = icalcomponent_get_private(event);

	ModifyContext* pc = g_new0(ModifyContext, 1);
	pc->cal = rc;
	pc->old_event = event;
	// TODO is this if stmt really needed?
	const char* root_url = rc->cfg->d.caldav.url;
	if (priv->url) {
		char* url_path = strchrnul(strchr(root_url, ':') + 3, '/');
		asprintf(&pc->url, "%.*s%s", (int) (url_path - root_url), root_url, priv->url);
	} else {
		asprintf(&pc->url, "%s%s.ics", root_url, icalcomponent_get_uid(event));
	}

	CURL* curl = new_curl_request(rc->cfg, pc->url, password);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

	// set the If-Match header
	char* match;
	asprintf(&match, "If-Match: %s", priv->etag);
	pc->hdrs = curl_slist_append(pc->hdrs, match);
	free(match);

	async_curl_add_request(curl, caldav_modify_done, pc);
}

static void delete_event(Calendar* c, icalcomponent* event)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	ENSURE_EXCLUSIVE(rc);

	// if the component has no private data, it has never been added to this calendar
	if (!icalcomponent_has_private(event))
		return;

	DelayedFunctionContext* dfc = g_new0(DelayedFunctionContext, 1);
	dfc->cal = rc;
	dfc->func = do_delete_event;
	dfc->arg1 = event;
	dfc->arg2 = event;

	caldav_secret_lookup(rc, dfc);
}

static void each_event(Calendar* c, CalendarEachEventCallback callback, void* user)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	for (GSList* p = rc->events; p; p = p->next) {
		callback(user, (Calendar*) rc, (icalcomponent*) p->data);
	}
}

static void free_event(icalcomponent* ev)
{
	EventPrivate* priv = icalcomponent_get_private(ev);
	free(priv->url);
	free(priv->etag);
	icalcomponent_free_private(ev);
	icalcomponent_free(icalcomponent_get_parent(ev));
}

static void free_events(CaldavCalendar* rc)
{
	g_slist_free_full(rc->events, (GDestroyNotify) free_event);
}

static icalcomponent* create_event_from_parsed_xml(CaldavCalendar* cal, CaldavEntry* cde)
{
	// event updated
	icalcomponent* comp = icalparser_parse_string(cde->caldata);
	icalcomponent* event = icalcomponent_get_first_component(comp, ICAL_VEVENT_COMPONENT);
	g_assert_nonnull(event);
	EventPrivate* fp = icalcomponent_create_private(event);
	fp->cal = FOCAL_CALENDAR(cal);
	// transfer ownership
	fp->url = cde->href;
	fp->etag = cde->etag;
	free(cde->caldata);
	free(cde);
	return event;
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
	struct curl_slist* hdrs;
	GString* report_req;
	GString* report_resp;
} SyncContext;

static void sync_multiget_report_done(CURL* curl, CURLcode ret, void* user)
{
	SyncContext* sc = (SyncContext*) user;
	CaldavCalendar* rc = sc->cal;

	// No matter what happened, these are no longer needed
	curl_slist_free_all(sc->hdrs);
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
			EventPrivate* pp = icalcomponent_get_private((*p)->data);
			if (strcmp(cde->href, pp->url) == 0) {
				if (cde->caldata) {
					// event updated
					if (strcmp(cde->etag, pp->etag) == 0) {
						// we already knew about this update (we probably did it ourselves). Just ignore it.
						caldav_entry_free(cde);
					} else {
						// replace the old event with the new one
						free_event((*p)->data);
						(*p)->data = create_event_from_parsed_xml(rc, cde);
						nUpdated++;
					}
				} else {
					// If the calendar-data is NULL, assume the event is deleted. This can
					// happen if an event is removed after the sync-collection request but
					// before the response to this multiget.
					free_event((*p)->data);
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
			icalcomponent* event = create_event_from_parsed_xml(rc, cde);
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

static void do_multiget_events(CaldavCalendar* rc, const char* password, GSList* hrefs)
{
	SyncContext* sc = g_new0(SyncContext, 1);
	sc->cal = rc;

	// According to RFC6578 Appendix B, the next step is to send GET requests
	// for each resource returned in the earlier sync-collection REPORT method.
	// Here we instead use a calendar-multiget REPORT for efficiency.
	// See https://tools.ietf.org/html/rfc6578#appendix-B

	CURL* curl = new_curl_request(rc->cfg, rc->cfg->d.caldav.url, password);

	sc->hdrs = curl_slist_append(NULL, "Depth: 1");
	sc->hdrs = curl_slist_append(sc->hdrs, "Prefer: return-minimal");
	sc->hdrs = curl_slist_append(sc->hdrs, "Content-Type: application/xml; charset=utf-8");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, sc->hdrs);
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

	async_curl_add_request(curl, sync_multiget_report_done, sc);
}

static void sync_collection_report_done(CURL* curl, CURLcode ret, void* user)
{
	SyncContext* sc = (SyncContext*) user;
	CaldavCalendar* rc = sc->cal;

	// No matter what happened, these are no longer needed
	curl_slist_free_all(sc->hdrs);
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
				EventPrivate* pp = icalcomponent_get_private((*p)->data);
				if (strcmp(se->href, pp->url) == 0) {
					free_event((*p)->data);
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

	DelayedFunctionContext* dfc = g_new0(DelayedFunctionContext, 1);
	dfc->func = do_multiget_events;
	dfc->cal = rc;
	dfc->arg1 = hrefs;

	caldav_secret_lookup(rc, dfc);
}

static void do_caldav_sync(CaldavCalendar* rc, const char* password)
{
	// Begin sync operation. According to RFC6578, the first step is to send
	// a sync-collection REPORT to retrieve a list of hrefs that have been
	// updated since the last call to the API (identified by the sync-token)
	// See https://tools.ietf.org/html/rfc6578#appendix-B

	CURL* curl = new_curl_request(rc->cfg, rc->cfg->d.caldav.url, password);

	// Userdata for the sync operation
	SyncContext* sc = g_new0(SyncContext, 1);
	sc->cal = rc;

	sc->hdrs = curl_slist_append(sc->hdrs, "Depth: 1");
	sc->hdrs = curl_slist_append(sc->hdrs, "Prefer: return-minimal");
	sc->hdrs = curl_slist_append(sc->hdrs, "Content-Type: application/xml; charset=utf-8");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, sc->hdrs);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "REPORT");

	sc->report_req = g_string_new("");
	g_string_append_printf(sc->report_req,
						   "<d:sync-collection xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
						   "  <d:sync-token>%s</d:sync-token>"
						   "  <d:sync-level>infinite</d:sync-level>"
						   "  <d:prop/>" // dav:prop always required by sabredav
						   "</d:sync-collection>",
						   rc->sync_token);

	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, sc->report_req->str);

	sc->report_resp = g_string_new(NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, sc->report_resp);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);

	async_curl_add_request(curl, sync_collection_report_done, sc);
}

static void caldav_sync(Calendar* c)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(c);
	ENSURE_EXCLUSIVE(rc);

	DelayedFunctionContext* dfc = g_new0(DelayedFunctionContext, 1);
	dfc->func = do_caldav_sync;
	dfc->cal = rc;

	caldav_secret_lookup(rc, dfc);
}

static void finalize(GObject* gobject)
{
	CaldavCalendar* rc = FOCAL_CALDAV_CALENDAR(gobject);
	free(rc->sync_token);
	free_events(rc);
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

Calendar* caldav_calendar_new(const CalendarConfig* cfg)
{
	CaldavCalendar* rc = g_object_new(CALDAV_CALENDAR_TYPE, NULL);
	// url must end with a slash. TODO don't assert
	g_assert_true(strrchr(cfg->d.caldav.url, '/')[1] == 0);
	rc->cfg = cfg;
	rc->events = NULL;
	rc->sync_token = g_strdup("");
	return (Calendar*) rc;
}

void caldav_calendar_free(CaldavCalendar* rc)
{
	free(rc->sync_token);
	free_events(rc);
	g_free(rc);
}
