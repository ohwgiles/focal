/*
 * remote-auth.c
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
#include "remote-auth.h"
#include "async-curl.h"
#include "calendar-config.h"
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <libsecret/secret.h>

#define FOCAL_GOOGLE_CLIENT_ID "96466437028-gee73t1rh4t84r4ddf1i17ucpdf8hr3s.apps.googleusercontent.com"

typedef struct {
	void (*callback)();
	void* arg;
} DeferredFunc;

struct _RemoteAuth {
	CalendarConfig* cfg;
	void (*cb_cancelled)(gpointer);
	void* user;
	DeferredFunc* ctx;
	GString* auth_resp;
};

// libsecret schema for username/password (basic authentication)
static const SecretSchema focal_basic_schema = {
	"net.ohwg.focal", SECRET_SCHEMA_NONE, {
											  {"url", SECRET_SCHEMA_ATTRIBUTE_STRING},
											  {"user", SECRET_SCHEMA_ATTRIBUTE_STRING},
											  {"NULL", 0},
										  }};

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
	RemoteAuth* ba = (RemoteAuth*) user_data;
	// Every instance of RemoteAuth receives this callback. Check that this event was really
	// intended for us by comparing the cookie attached to the event
	if (ba->cfg->cookie && strcmp(ba->cfg->cookie, cookie) == 0) {
		CURL* curl = curl_easy_init();
		g_assert_nonnull(curl);
		curl_easy_setopt(curl, CURLOPT_URL, "https://www.googleapis.com/oauth2/v4/token");
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
		g_string_truncate(ba->auth_resp, 0);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, ba->auth_resp);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);
		gchar* query = g_strdup_printf("code=%s"
									   "&client_id=%s"
									   "&redirect_uri=net.ohwg.focal%%3A%%2Fauth%%2Fgoogle"
									   "&grant_type=authorization_code"
									   "&code_verifier=%s",
									   code, FOCAL_GOOGLE_CLIENT_ID, cookie);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, query);
		async_curl_add_request(curl, NULL, on_request_auth_complete, ba);
	}
}

// Called to launch external OAuth2 authentication in a browser window
static void launch_external_authentication(RemoteAuth* ba)
{
	gchar* url = NULL;
	GError* error = NULL;
	g_assert_nonnull(ba->cfg->cookie);
	url = g_strdup_printf("https://accounts.google.com/o/oauth2/v2/auth"
						  "?client_id=" FOCAL_GOOGLE_CLIENT_ID
						  "&redirect_uri=net.ohwg.focal%%3A%%2Fauth%%2Fgoogle"
						  "&response_type=code"
						  "&scope=openid+email+https%%3A%%2F%%2Fwww.googleapis.com%%2Fauth%%2Fcalendar"
						  "&access_type=offline"
						  "&state=%s"
						  "&code_challenge=%s",
						  ba->cfg->cookie, ba->cfg->cookie);

	fprintf(stderr, "Launching browser for %s", url);
	gboolean ret = g_app_info_launch_default_for_uri(url, NULL, &error);
	free(url);
	if (!ret)
		g_error("Could not launch web browser: %s", error->message);
}

static void on_password_lookup(GObject* source, GAsyncResult* result, gpointer user);

static void password_lookup(RemoteAuth* ba)
{
	secret_password_lookup(&focal_basic_schema, NULL, on_password_lookup, ba,
						   "url", ba->cfg->location,
						   "user", ba->cfg->login,
						   NULL);
}

static void on_auth_token_lookup(GObject* source, GAsyncResult* result, gpointer user);

static void auth_token_lookup(RemoteAuth* ba)
{
	secret_password_lookup(&focal_oauth2_schema, NULL, on_auth_token_lookup, ba,
						   "type", "auth",
						   "cookie", ba->cfg->cookie,
						   NULL);
}

static void on_password_stored(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	RemoteAuth* ba = (RemoteAuth*) user;
	secret_password_store_finish(result, &error);
	if (error != NULL) {
		g_warning(error->message);
		g_error_free(error);
		// cancel current operation
		free(ba->ctx);
		ba->ctx = NULL;
	} else {
		// immediately look up the password again so we can continue the originally
		// requested async operation
		password_lookup(ba);
	}
}

static char* prompt_user_credentials(GtkWindow* parent, const char* account_name, const char* user)
{
	GtkWidget* dialog = gtk_dialog_new_with_buttons("Focal", parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_OK", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
	GtkEntryBuffer* buffer = gtk_entry_buffer_new("", 0);

	GtkWidget* grid = gtk_grid_new();
	g_object_set(grid, "column-spacing", 12, "row-spacing", 9, "margin-bottom", 12, "margin-top", 12, NULL);
	gtk_grid_attach(GTK_GRID(grid), gtk_image_new_from_icon_name("dialog-password-symbolic", GTK_ICON_SIZE_DIALOG), 0, 0, 1, 4);
	gtk_grid_attach(GTK_GRID(grid), g_object_new(GTK_TYPE_LABEL, "label", "<b>This operation requires your password</b>", "use-markup", TRUE, NULL), 1, 0, 2, 1);
	gtk_grid_attach(GTK_GRID(grid), g_object_new(GTK_TYPE_LABEL, "label", "Account:", "halign", GTK_ALIGN_END, NULL), 1, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), g_object_new(GTK_TYPE_LABEL, "label", account_name, "halign", GTK_ALIGN_START, NULL), 2, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), g_object_new(GTK_TYPE_LABEL, "label", "Username:", "halign", GTK_ALIGN_END, NULL), 1, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), g_object_new(GTK_TYPE_LABEL, "label", user, "halign", GTK_ALIGN_START, NULL), 2, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), g_object_new(GTK_TYPE_LABEL, "label", "Password:", "halign", GTK_ALIGN_END, NULL), 1, 3, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), g_object_new(GTK_TYPE_ENTRY, "buffer", buffer, "input-purpose", GTK_INPUT_PURPOSE_PASSWORD, "visibility", FALSE, "halign", GTK_ALIGN_START, NULL), 2, 3, 1, 1);

	GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	g_object_set(content, "margin", 6, NULL);
	gtk_container_add(GTK_CONTAINER(content), grid);
	gtk_widget_show_all(dialog);

	int ret = gtk_dialog_run(GTK_DIALOG(dialog));
	char* res = NULL;
	if (ret == GTK_RESPONSE_OK) {
		res = g_strdup(gtk_entry_buffer_get_text(buffer));
	}
	gtk_widget_destroy(dialog);
	g_object_unref(buffer);

	return res;
}

static void on_password_lookup(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	gchar* password = secret_password_lookup_finish(result, &error);
	RemoteAuth* ba = (RemoteAuth*) user;
	DeferredFunc* dfc = ba->ctx;

	if (error != NULL) {
		g_critical(error->message);
		g_error_free(error);
	} else if (password == NULL) {
		// no matching password found, prompt the user to create one
		GtkWindow* window = gtk_application_get_active_window(GTK_APPLICATION(g_application_get_default()));
		char* pass = prompt_user_credentials(window, ba->cfg->label, ba->cfg->login);
		if (pass) {
			// We could just use the password here, but there's a good chance that it
			// would be immediately required again, and the next lookup would pop up
			// another dialog. Instead, wait until the password is stored, then trigger
			// a lookup again with the original callback data
			secret_password_store(&focal_basic_schema, SECRET_COLLECTION_DEFAULT, "Focal Remote Calendar password",
								  pass, NULL, on_password_stored, ba,
								  "url", ba->cfg->location,
								  "user", ba->cfg->login,
								  NULL);
			g_free(pass);
			// skip free of dfc, it will be cleaned up in on_password_stored callback
			return;
		} else {
			// user declined to enter password. Allow a later attempt...
			(ba->cb_cancelled)(ba->user);
		}
	} else {
		CURL* curl = curl_easy_init();
		g_assert_nonnull(curl);
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
		curl_easy_setopt(curl, CURLOPT_USERNAME, ba->cfg->login);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
		struct curl_slist* hdrs = curl_slist_append(NULL, "User-Agent: Focal/0.1");
		(*dfc->callback)(ba->user, curl, hdrs, dfc->arg);
		secret_password_free(password);
	}
	free(dfc);
	ba->ctx = NULL;
}

static void on_auth_token_stored(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	secret_password_store_finish(result, &error);
	if (error != NULL) {
		g_critical(error->message);
		g_error_free(error);
	} else {
		// fetch the auth token again so we can continue with the original async request
		auth_token_lookup((RemoteAuth*) user);
	}
}

static void on_refresh_token_stored(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	secret_password_store_finish(result, &error);
	if (error != NULL) {
		g_critical(error->message);
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

	RemoteAuth* ba = (RemoteAuth*) user;
	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code != 200) {
		g_critical("unhandled response code %ld, response %s\n", response_code, ba->auth_resp->str);
		return;
	}

	JsonParser* parser = json_parser_new();
	json_parser_load_from_data(parser, ba->auth_resp->str, -1, NULL);

	JsonReader* reader = json_reader_new(json_parser_get_root(parser));
	json_reader_read_member(reader, "access_token");
	const char* access_token = json_reader_get_string_value(reader);
	json_reader_end_member(reader);

	json_reader_read_member(reader, "refresh_token");
	const char* refresh_token = json_reader_get_string_value(reader);
	json_reader_end_member(reader);

	if (refresh_token) {
		secret_password_store(&focal_oauth2_schema, SECRET_COLLECTION_DEFAULT, "Focal OAuth Refresh Token",
							  refresh_token, NULL, on_refresh_token_stored, ba,
							  "type", "refresh",
							  "cookie", ba->cfg->cookie,
							  NULL);
	} else {
		g_warning("OAuth2 response did not contain new refresh token");
	}

	secret_password_store(&focal_oauth2_schema, SECRET_COLLECTION_DEFAULT, "Focal OAuth Auth Token",
						  access_token, NULL, on_auth_token_stored, ba,
						  "type", "auth",
						  "cookie", ba->cfg->cookie,
						  NULL);

	g_object_unref(reader);
	g_object_unref(parser);
}

static void request_new_auth_token(RemoteAuth* ba, const char* refresh_token)
{
	CURL* curl = curl_easy_init();
	g_assert_nonnull(curl);
	char* postdata = g_strdup_printf("grant_type=refresh_token"
									 "&refresh_token=%s"
									 "&client_id=%s"
									 "&approval_prompt=force"
									 "&access_type=offline",
									 refresh_token, FOCAL_GOOGLE_CLIENT_ID);
	g_string_truncate(ba->auth_resp, 0);

	curl_easy_setopt(curl, CURLOPT_URL, "https://www.googleapis.com/oauth2/v4/token");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, ba->auth_resp);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_gstring);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);

	async_curl_add_request(curl, NULL, on_request_auth_complete, ba);
}

static void on_refresh_token_lookup(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	gchar* token = secret_password_lookup_finish(result, &error);
	RemoteAuth* ba = (RemoteAuth*) user;

	if (error != NULL) {
		g_critical(error->message);
		g_error_free(error);
		free(ba->ctx);
		ba->ctx = NULL;
	} else if (token == NULL) {
		// no refresh token in password store, we need to run authentication again!
		g_warning("no refresh token, rerun authentication");
		launch_external_authentication(ba);
	} else {
		request_new_auth_token(ba, token);
		secret_password_free(token);
	}
}

static void refresh_token_lookup(RemoteAuth* ba)
{
	secret_password_lookup(&focal_oauth2_schema, NULL, on_refresh_token_lookup, ba,
						   "type", "refresh",
						   "cookie", ba->cfg->cookie,
						   NULL);
}

static void on_auth_token_lookup(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	gchar* token = secret_password_lookup_finish(result, &error);
	RemoteAuth* ba = (RemoteAuth*) user;

	if (error != NULL) {
		g_critical(error->message);
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
		(*ba->ctx->callback)(ba->user, curl, hdrs, ba->ctx->arg);
		secret_password_free(token);
	}
	free(ba->ctx);
	ba->ctx = NULL;
}

RemoteAuth* remote_auth_new(CalendarConfig* cfg, gpointer user)
{
	g_assert_nonnull(cfg);
	g_assert(cfg->type == CAL_TYPE_CALDAV || cfg->type == CAL_TYPE_GOOGLE);

	RemoteAuth* ba = g_new0(RemoteAuth, 1);
	// Listen to signals from the global application (see main.c) that report OAuth2
	// responses from an external browser (invoked via xdg-open). It is the handler's
	// responsibility to verify that this signal is intended for this RemoteAuth instance.
	g_signal_connect_swapped(g_application_get_default(), "browser-auth-response", G_CALLBACK(on_external_browser_response), ba);
	ba->cfg = cfg;
	ba->user = user;
	ba->auth_resp = g_string_new(NULL);
	return ba;
}

void remote_auth_set_declined_callback(RemoteAuth* ba, void (*callback)(gpointer))
{
	ba->cb_cancelled = callback;
}

void remote_auth_new_request(RemoteAuth* ba, void (*callback)(), void* arg)
{
	g_assert_null(ba->ctx);
	ba->ctx = g_new0(DeferredFunc, 1);
	ba->ctx->callback = callback;
	ba->ctx->arg = arg;

	if (ba->cfg->type == CAL_TYPE_GOOGLE && ba->cfg->cookie == NULL) {
		g_warning("cookie is unset, generating new");
		ba->cfg->cookie = g_uuid_string_random();
	}

	if (ba->cfg->type == CAL_TYPE_CALDAV)
		password_lookup(ba);
	else
		auth_token_lookup(ba);
}

void remote_auth_invalidate_credential(RemoteAuth* ba, void (*callback)(), void* arg)
{
	g_assert_null(ba->ctx);
	g_assert(ba->cfg->type == CAL_TYPE_GOOGLE);
	ba->ctx = g_new0(DeferredFunc, 1);
	ba->ctx->callback = callback;
	ba->ctx->arg = arg;
	// remove the invalidated auth token from the store with the callback as
	// if we were looking it up. In this case the refresh token will be queried
	secret_password_clear(&focal_oauth2_schema, NULL, on_auth_token_lookup, ba,
						  "type", "auth",
						  "cookie", ba->cfg->cookie,
						  NULL);
}

void remote_auth_free(RemoteAuth* ba)
{
	// TODO don't assert, but cancel any ongoing requests
	g_assert_null(ba->ctx);
	g_string_free(ba->auth_resp, TRUE);
	g_free(ba);
}
