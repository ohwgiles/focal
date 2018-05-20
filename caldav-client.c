/*
 * caldav-client.c
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
#include <string.h>

#include "caldav-client.h"
#include "calendar.h"
#include "event-private.h"

struct _CaldavClient {
	char* url;
	char* username;
	char* password;
	gboolean verify_cert;
};

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

// Callback when SAX parser finds a closing XML tag during calendar init
static void xmlparse_init_close(void* ctx, const xmlChar* name)
{
	XmlParseCtx* xpc = (XmlParseCtx*) ctx;
	// TODO save ctag and displayname
	xmlparse_ns_pop(xpc);
}

// Callback when SAX parser finds a closing XML tag during search for hrefs
static void xmlparse_find_hrefs(void* ctx, const xmlChar* name)
{
	XmlParseCtx* xpc = (XmlParseCtx*) ctx;
	if (xml_tag_matches(xpc, name, "DAV:", "href")) {
		xpc->hrefs = g_slist_append(xpc->hrefs, g_strdup(xpc->chars.str));
	}
	xmlparse_ns_pop(xpc);
}

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

CaldavClient* caldav_client_new(const char* url, const char* user, const char* pass, gboolean verify_cert)
{
	CaldavClient* cc = g_new0(CaldavClient, 1);
	cc->url = g_strdup(url);
	cc->username = g_strdup(user);
	cc->password = g_strdup(pass);
	cc->verify_cert = verify_cert;
	return cc;
}

gboolean caldav_client_init(CaldavClient* cc)
{
	CURLcode ret;
	CURL* curl = curl_easy_init();

	if (!curl)
		return FALSE;

	curl_easy_setopt(curl, CURLOPT_URL, cc->url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Depth: 0");
	headers = curl_slist_append(headers, "Prefer: return-minimal");
	headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(curl, CURLOPT_USERNAME, cc->username);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, cc->password);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, cc->verify_cert);

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

gboolean caldav_client_put(CaldavClient* cc, icalcomponent* event, const char* url)
{
	CURL* curl = curl_easy_init();
	if (!curl)
		return FALSE;

	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: text/calendar; charset=utf-8");
	headers = curl_slist_append(headers, "Expect:");

	char* purl;
	if (url) {
		char* host_delim = strchrnul(strchr(cc->url, ':') + 3, '/');
		asprintf(&purl, "%.*s%s", (int) (host_delim - cc->url), cc->url, url);
	} else {
		asprintf(&purl, "%s/%s.ics", cc->url, icalcomponent_get_uid(event));
		headers = curl_slist_append(headers, "If-None-Match: *");
	}

	curl_easy_setopt(curl, CURLOPT_URL, purl);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(curl, CURLOPT_USERNAME, cc->username);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, cc->password);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, cc->verify_cert);

	char* caldata = icalcomponent_as_ical_string(icalcomponent_get_parent(event));
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, caldata);

	CURLcode res = curl_easy_perform(curl);

	free(caldata);
	free(purl);
	curl_easy_cleanup(curl);

	return res == CURLE_OK;
}

gboolean caldav_client_delete(CaldavClient* cc, icalcomponent* event, const char* url)
{
	CURL* curl = curl_easy_init();
	if (!curl)
		return FALSE;

	char* purl;
	if (url) {
		char* host_delim = strchrnul(strchr(cc->url, ':') + 3, '/');
		asprintf(&purl, "%.*s%s", (int) (host_delim - cc->url), cc->url, url);
	} else {
		asprintf(&purl, "%s/%s.ics", cc->url, icalcomponent_get_uid(event));
	}

	curl_easy_setopt(curl, CURLOPT_URL, purl);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

	struct curl_slist* headers = NULL;
	// TODO use caldav etag
	// headers = curl_slist_append(headers, "If-Match: ETAG_HERE");

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(curl, CURLOPT_USERNAME, cc->username);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, cc->password);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, cc->verify_cert);

	CURLcode res = curl_easy_perform(curl);

	free(purl);
	curl_easy_cleanup(curl);

	return res == CURLE_OK;
}

GSList* caldav_client_sync(CaldavClient* cc)
{
	CURLcode ret;
	CURL* curl = curl_easy_init();

	if (!curl)
		return NULL;

	curl_easy_setopt(curl, CURLOPT_URL, cc->url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Depth: 1");
	headers = curl_slist_append(headers, "Prefer: return-minimal");
	headers = curl_slist_append(headers, "Content-Type: application/xml; charset=utf-8");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(curl, CURLOPT_USERNAME, cc->username);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, cc->password);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, cc->verify_cert);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "<propfind xmlns=\"DAV:\"><prop><href/></prop></propfind>");

	GString* propfind_resp = g_string_new(NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, propfind_resp);

	// First, a PROPFIND request...
	ret = curl_easy_perform(curl);

	if (ret != CURLE_OK) {
		g_string_free(propfind_resp, TRUE);
		curl_easy_cleanup(curl);
		return NULL;
	}

	XmlParseCtx ctx;
	xmlctx_init(&ctx);
	// list containing all <href> URLs
	ctx.hrefs = g_slist_alloc();
	xmlSAXHandler my_handler = {.characters = xmlparse_characters,
								.startElement = xmlparse_tag_open,
								.endElement = xmlparse_find_hrefs};
	xmlSAXUserParseMemory(&my_handler, &ctx, propfind_resp->str, propfind_resp->len);
	xmlctx_cleanup(&ctx);
	g_string_free(propfind_resp, TRUE);

	GString* report_req = g_string_new(
		"<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
		"  <d:prop>"
		"    <d:getetag/>"
		"    <c:calendar-data/>"
		"  </d:prop>");
	for (GSList* p = ctx.hrefs; p; p = p->next) {
		g_string_append_printf(report_req, "<d:href>%s</d:href>", (char*) p->data);
	}
	g_string_append(report_req, "</c:calendar-multiget>");
	g_slist_free_full(ctx.hrefs, g_free);

	// ... and then REPORT, reusing the same CURL*
	GString* report_resp = g_string_new(NULL);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "REPORT");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, report_req->str);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, report_resp);

	ret = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	g_string_free(report_req, TRUE);

	if (ret != CURLE_OK) {
		g_string_free(report_resp, TRUE);
		return NULL;
	}

	xmlctx_init(&ctx);
	ctx.event_list = g_slist_alloc();
	xmlSAXHandler my_handler2 = {.characters = xmlparse_characters,
								 .startElement = xmlparse_tag_open,
								 .endElement = xmlparse_find_caldata};
	xmlSAXUserParseMemory(&my_handler2, &ctx, report_resp->str, report_resp->len);
	xmlctx_cleanup(&ctx);
	g_string_free(report_resp, TRUE);

	return ctx.event_list;
}

void caldav_client_free(CaldavClient* c)
{
	g_free(c->url);
	g_free(c->username);
	g_free(c->password);
	g_free(c);
}
