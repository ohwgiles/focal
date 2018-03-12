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

typedef struct {
	char* nsDav;
	char* nsCal;
	char* tagCalendarData;
	char* tagDisplayName;
	char* tagCTag;
} CaldavXml;

struct _CaldavClient {
	char* url;
	char* username;
	char* password;
	gboolean verify_cert;
	CaldavXml xml;
};

typedef struct {
	CaldavXml* props;
	GString chars;
	GSList* event_list;
} XmlParseCtx;

static void xmlparse_characters(void* ctx, const xmlChar* ch, int len)
{
	XmlParseCtx* xpc = (XmlParseCtx*) ctx;
	g_string_append_len(&xpc->chars, ch, len);
}

static void xmlparse_init_start(void* ctx, const xmlChar* fullname, const xmlChar** atts)
{
	CaldavXml* xpc = ((XmlParseCtx*) ctx)->props;

	// return if we already found the namespace names
	if (xpc->nsDav && xpc->nsCal)
		return;

	if (!strstr(fullname, ":multistatus"))
		// this is not the XML tag we're looking for
		return;

	// loop over attributes to find the namespace names
	for (const xmlChar** k = atts; *k; k += 2) {
		const xmlChar* v = k[1];
		if (strncmp(*k, "xmlns:", 6) == 0) {
			if (xpc->nsDav == NULL && strcmp(v, "DAV:") == 0) {
				xpc->nsDav = strdup(*k + 6);
				asprintf(&xpc->tagDisplayName, "%s:displayname", *k + 6);
			} else if (xpc->nsCal == NULL && strcmp(v, "http://calendarserver.org/ns/") == 0) {
				xpc->nsCal = strdup(*k + 6);
				asprintf(&xpc->tagCTag, "%s:getctag", *k + 6);
				asprintf(&xpc->tagCalendarData, "%s:calendar-data", *k + 6);
			}
		}
	}
}

static void xmlparse_init_end(void* ctx, const xmlChar* name)
{
	XmlParseCtx* xpc = (XmlParseCtx*) ctx;
	g_string_truncate(&xpc->chars, 0);
}

static void xmlparse_sync_end(void* ctx, const xmlChar* name)
{
	XmlParseCtx* xpc = (XmlParseCtx*) ctx;
	CaldavXml* props = xpc->props;
	if (props->tagCalendarData && strcmp(name, props->tagCalendarData) == 0) {
		icalcomponent* comp = icalparser_parse_string(xpc->chars.str);
		icalcomponent* vevent =
			icalcomponent_get_first_component(comp, ICAL_VEVENT_COMPONENT);
		if (vevent) {
			// printf("got event %s\n", icalcomponent_get_summary(vevent));
			xpc->event_list = g_slist_append(xpc->event_list, vevent);
		}
	}
	g_string_truncate(&xpc->chars, 0);
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
	curl_easy_setopt(curl, CURLOPT_USERNAME, cc->username);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, cc->password);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, cc->verify_cert);

	curl_easy_setopt(curl, CURLOPT_POSTFIELDS,
					 "<d:propfind xmlns:d=\"DAV:\" "
					 "xmlns:cs=\"http://calendarserver.org/ns/\">"
					 "  <d:prop>"
					 "     <d:displayname />"
					 "     <cs:getctag />"
					 "  </d:prop>"
					 "</d:propfind>");

	GString* str = g_string_new(NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, str);

	if (curl_easy_perform(curl) != CURLE_OK)
		return curl_easy_cleanup(curl), FALSE;

	XmlParseCtx ctx = {.props = &cc->xml};
	xmlSAXHandler my_handler = {.characters = xmlparse_characters,
								.startElement = xmlparse_init_start,
								.endElement = xmlparse_init_end};
	xmlSAXUserParseMemory(&my_handler, &ctx, str->str, str->len);

	curl_easy_cleanup(curl);

	return TRUE;
}

gboolean caldav_client_put(CaldavClient* cc, icalcomponent* event, const char* url)
{
	CURL* curl = curl_easy_init();
	if (!curl)
		return FALSE;

	char* purl;
	if (url) {
		purl = g_strdup(url);
	} else {
		asprintf(&purl, "%s/%s.ics", cc->url, icalcomponent_get_uid(event));
	}

	curl_easy_setopt(curl, CURLOPT_URL, purl);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "If-None-Match: *");
	headers = curl_slist_append(headers, "Content-Type: text/calendar; charset=utf-8");
	headers = curl_slist_append(headers, "Expect:");

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_USERNAME, cc->username);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, cc->password);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, cc->verify_cert);

	char* caldata =
		icalcomponent_as_ical_string(icalcomponent_get_parent(event));
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
		purl = g_strdup(url);
	} else {
		asprintf(&purl, "%s/%s.ics", cc->url, icalcomponent_get_uid(event));
	}

	curl_easy_setopt(curl, CURLOPT_URL, purl);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

	struct curl_slist* headers = NULL;
	// TODO use caldav etag
	// headers = curl_slist_append(headers, "If-Match: ETAG_HERE");

	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
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
	curl_easy_setopt(curl, CURLOPT_USERNAME, cc->username);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, cc->password);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, cc->verify_cert);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "<propfind xmlns='DAV:'><prop><calendar-data xmlns='urn:ietf:params:xml:ns:caldav'/></prop></propfind>");

	GString* str = g_string_new(NULL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, str);

	if (curl_easy_perform(curl) != CURLE_OK)
		return curl_easy_cleanup(curl), NULL;

	XmlParseCtx ctx = {.props = &cc->xml,
					   .event_list = g_slist_alloc()};

	xmlSAXHandler my_handler = {.characters = xmlparse_characters,
								.startElement = NULL,
								.endElement = xmlparse_sync_end};
	xmlSAXUserParseMemory(&my_handler, &ctx, str->str, str->len);

	curl_easy_cleanup(curl);

	return ctx.event_list;
}

void caldav_client_free(CaldavClient* c)
{
	g_free(c->url);
	g_free(c->username);
	g_free(c->password);
	g_free(c);
}
