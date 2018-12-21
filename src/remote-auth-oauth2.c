/*
 * remote-auth-oauth2.c
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
#include "remote-auth-oauth2.h"
#include "async-curl.h"
#include "calendar-config.h"
#include "oauth2-provider.h"
#include <json-glib/json-glib.h>
#include <libsecret/secret.h>

typedef struct {
	void (*callback)();
	void* user;
	void* arg;
} DeferredFunc;

struct _RemoteAuthOAuth2 {
	RemoteAuth parent;
	CalendarConfig* cfg;
	DeferredFunc* ctx;
	GString* auth_resp;
	OAuth2Provider* provider;
};
G_DEFINE_TYPE(RemoteAuthOAuth2, remote_auth_oauth2, TYPE_REMOTE_AUTH)

// libsecret schema for OAuth2
static const SecretSchema focal_oauth2_schema = {
	"net.ohwg.focal", SECRET_SCHEMA_NONE, {
											  {"type", SECRET_SCHEMA_ATTRIBUTE_STRING},
											  {"cookie", SECRET_SCHEMA_ATTRIBUTE_STRING},
											  {"NULL", 0},
										  }};

static void on_request_auth_complete(CURL* curl, CURLcode ret, void* user);

// Callback when focal is invoked via OAuth2 redirect custom URL scheme, e.g.
//  /path/to/focal net.ohwg.focal:/auth/google?code=...
// This will typically happen via xdg-open if focal.desktop is registered as the correct x-scheme-handler.
static void on_external_browser_response(void* user_data, const gchar* cookie, const char* code)
{
	RemoteAuthOAuth2* ba = (RemoteAuthOAuth2*) user_data;
	CalendarConfig* cfg = ba->cfg;

	// Every instance of RemoteAuth receives this callback. Check that this event was really
	// intended for us by comparing the cookie attached to the event
	if (cfg->cookie && strcmp(cfg->cookie, cookie) == 0) {
		CURL* curl = curl_easy_init();
		g_assert_nonnull(curl);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
		g_string_truncate(ba->auth_resp, 0);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, ba->auth_resp);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);
		curl_easy_setopt(curl, CURLOPT_URL, oauth2_provider_token_url(ba->provider));
		gchar* query = oauth2_provider_auth_code_query(ba->provider, code, cookie);
		g_assert_nonnull(query);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, query);
		async_curl_add_request(curl, NULL, on_request_auth_complete, ba);
	}
}

// Called to launch external OAuth2 authentication in a browser window
static void launch_external_authentication(RemoteAuthOAuth2* oa)
{
	gchar* url = NULL;
	GError* error = NULL;
	CalendarConfig* cfg = oa->cfg;

	g_assert_nonnull(cfg->cookie);
	url = oauth2_provider_ext_auth_url(oa->provider, cfg->cookie);

	fprintf(stderr, "Launching browser for %s", url);
	gboolean ret = g_app_info_launch_default_for_uri(url, NULL, &error);
	free(url);
	if (!ret)
		g_error("Could not launch web browser: %s", error->message);
}

static void on_auth_token_lookup(GObject* source, GAsyncResult* result, gpointer user);

static void auth_token_lookup(RemoteAuthOAuth2* oa)
{
	CalendarConfig* cfg = oa->cfg;
	g_assert_nonnull(cfg->cookie);
	secret_password_lookup(&focal_oauth2_schema, NULL, on_auth_token_lookup, oa,
						   "type", "auth",
						   "cookie", cfg->cookie,
						   NULL);
}

static void on_auth_token_stored(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	secret_password_store_finish(result, &error);
	if (error != NULL) {
		g_critical(error->message, G_LOG_LEVEL_CRITICAL);
		g_error_free(error);
	} else {
		// fetch the auth token again so we can continue with the original async request
		auth_token_lookup(FOCAL_REMOTE_AUTH_OAUTH2(user));
	}
}

static void on_refresh_token_stored(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	secret_password_store_finish(result, &error);
	if (error != NULL) {
		g_critical(error->message, G_LOG_LEVEL_CRITICAL);
		g_error_free(error);
	}
}

static void on_request_auth_complete(CURL* curl, CURLcode ret, void* user)
{
	if (ret != CURLE_OK) {
		// TODO better handling
		g_error("CURL NOK");
		return;
	}

	RemoteAuthOAuth2* oa = (RemoteAuthOAuth2*) user;
	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code != 200) {
		g_critical("unhandled response code %ld, response %s\n", response_code, oa->auth_resp->str);
		return;
	}

	JsonParser* parser = json_parser_new();
	json_parser_load_from_data(parser, oa->auth_resp->str, -1, NULL);

	JsonReader* reader = json_reader_new(json_parser_get_root(parser));
	json_reader_read_member(reader, "access_token");
	const char* access_token = json_reader_get_string_value(reader);
	json_reader_end_member(reader);

	json_reader_read_member(reader, "refresh_token");
	const char* refresh_token = json_reader_get_string_value(reader);
	json_reader_end_member(reader);

	CalendarConfig* cfg = oa->cfg;

	if (refresh_token) {
		secret_password_store(&focal_oauth2_schema, SECRET_COLLECTION_DEFAULT, "Focal OAuth Refresh Token",
							  refresh_token, NULL, on_refresh_token_stored, oa,
							  "type", "refresh",
							  "cookie", cfg->cookie,
							  NULL);
	} else {
		g_warning("OAuth2 response did not contain new refresh token");
	}

	secret_password_store(&focal_oauth2_schema, SECRET_COLLECTION_DEFAULT, "Focal OAuth Auth Token",
						  access_token, NULL, on_auth_token_stored, oa,
						  "type", "auth",
						  "cookie", cfg->cookie,
						  NULL);

	g_object_unref(reader);
	g_object_unref(parser);
}

static void request_new_auth_token(RemoteAuthOAuth2* oa, const char* refresh_token)
{
	CURL* curl = curl_easy_init();
	g_assert_nonnull(curl);

	curl_easy_setopt(curl, CURLOPT_URL, oauth2_provider_token_url(oa->provider));
	char* postdata = oauth2_provider_refresh_token_query(oa->provider, refresh_token);
	g_string_truncate(oa->auth_resp, 0);

	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, oa->auth_resp);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);

	async_curl_add_request(curl, NULL, on_request_auth_complete, oa);
}

static void on_refresh_token_lookup(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	gchar* token = secret_password_lookup_finish(result, &error);
	RemoteAuthOAuth2* oa = (RemoteAuthOAuth2*) user;

	if (error != NULL) {
		g_critical(error->message, G_LOG_LEVEL_CRITICAL);
		g_error_free(error);
		free(oa->ctx);
		oa->ctx = NULL;
	} else if (token == NULL) {
		// no refresh token in password store, we need to run authentication again!
		g_warning("no refresh token, rerun authentication");
		launch_external_authentication(oa);
	} else {
		request_new_auth_token(oa, token);
		secret_password_free(token);
	}
}

static void refresh_token_lookup(RemoteAuthOAuth2* ra)
{
	CalendarConfig* cfg = ra->cfg;
	secret_password_lookup(&focal_oauth2_schema, NULL, on_refresh_token_lookup, ra,
						   "type", "refresh",
						   "cookie", cfg->cookie,
						   NULL);
}

static void on_auth_token_lookup(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	gchar* token = secret_password_lookup_finish(result, &error);
	RemoteAuthOAuth2* ba = (RemoteAuthOAuth2*) user;

	if (error != NULL) {
		g_critical(error->message, G_LOG_LEVEL_CRITICAL);
		g_error_free(error);
	} else if (token == NULL) {
		// no auth token in password store, try to acquire another using the refresh token
		refresh_token_lookup(ba);
		return;
	} else {
		CURL* curl = curl_easy_init();
		g_assert_nonnull(curl);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
		struct curl_slist* hdrs = curl_slist_append(NULL, "User-Agent: Focal/0.1");
		char* auth = g_strdup_printf("Authorization: Bearer %s", token);
		hdrs = curl_slist_append(hdrs, auth);
		g_free(auth);
		// invoke original handler
		(*ba->ctx->callback)(ba->ctx->user, curl, hdrs, ba->ctx->arg);
		secret_password_free(token);
	}
	free(ba->ctx);
	ba->ctx = NULL;
}

static void remote_auth_oauth2_new_request(RemoteAuth* ra, void (*callback)(), void* user, void* arg)
{
	RemoteAuthOAuth2* oa = FOCAL_REMOTE_AUTH_OAUTH2(ra);
	CalendarConfig* cfg = oa->cfg;
	if (cfg->cookie == NULL) {
		g_warning("cookie is unset, generating new");
		cfg->cookie = g_uuid_string_random();
	}

	g_assert_nonnull(oa->provider);
	g_assert_null(oa->ctx);
	oa->ctx = g_new0(DeferredFunc, 1);
	oa->ctx->callback = callback;
	oa->ctx->user = user;
	oa->ctx->arg = arg;

	auth_token_lookup(oa);
}

static void remote_auth_oauth2_invalidate_credential(RemoteAuth* ra, void (*callback)(), void* user, void* arg)
{
	RemoteAuthOAuth2* oa = FOCAL_REMOTE_AUTH_OAUTH2(ra);
	g_assert_null(oa->ctx);

	oa->ctx = g_new0(DeferredFunc, 1);
	oa->ctx->callback = callback;
	oa->ctx->user = user;
	oa->ctx->arg = arg;
	// remove the invalidated auth token from the store with the callback as
	// if we were looking it up. In this case the refresh token will be queried
	secret_password_clear(&focal_oauth2_schema, NULL, on_auth_token_lookup, ra,
						  "type", "auth",
						  "cookie", oa->cfg->cookie,
						  NULL);
}

static void finalize(GObject* gobject)
{
	RemoteAuthOAuth2* oa = FOCAL_REMOTE_AUTH_OAUTH2(gobject);
	g_string_free(oa->auth_resp, TRUE);
	g_object_unref(oa->provider);
	G_OBJECT_CLASS(remote_auth_oauth2_parent_class)->finalize(gobject);
}

enum {
	PROP_0,
	PROP_CALENDAR_CONFIG,
	PROP_OAUTH2_PROVIDER
};

static void set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
	if (prop_id == PROP_CALENDAR_CONFIG) {
		FOCAL_REMOTE_AUTH_OAUTH2(object)->cfg = g_value_get_pointer(value);
	} else if (prop_id == PROP_OAUTH2_PROVIDER) {
		FOCAL_REMOTE_AUTH_OAUTH2(object)->provider = g_value_get_pointer(value);
	} else {
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	}
}

void remote_auth_oauth2_class_init(RemoteAuthOAuth2Class* klass)
{
	GObjectClass* goc = (GObjectClass*) klass;
	goc->finalize = finalize;
	goc->set_property = set_property;
	g_object_class_override_property(G_OBJECT_CLASS(klass), PROP_CALENDAR_CONFIG, "cfg");
	g_object_class_install_property(goc, PROP_OAUTH2_PROVIDER, g_param_spec_pointer("provider", "OAuth2 Provider", "", G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
	FOCAL_REMOTE_AUTH_CLASS(klass)->new_request = remote_auth_oauth2_new_request;
	FOCAL_REMOTE_AUTH_CLASS(klass)->invalidate_credential = remote_auth_oauth2_invalidate_credential;
}

void remote_auth_oauth2_init(RemoteAuthOAuth2* oa)
{
	// Listen to signals from the global application (see main.c) that report OAuth2
	// responses from an external browser (invoked via xdg-open). It is the handler's
	// responsibility to verify that this signal is intended for this RemoteAuth instance.
	g_signal_connect_swapped(g_application_get_default(), "browser-auth-response", G_CALLBACK(on_external_browser_response), oa);
	oa->auth_resp = g_string_new("");
}
