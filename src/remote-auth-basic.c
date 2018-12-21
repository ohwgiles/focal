/*
 * remote-auth-basic.c
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
#include "remote-auth-basic.h"
#include "calendar-config.h"
#include <libsecret/secret.h>

typedef struct {
	void (*callback)();
	void* user;
	void* arg;
} DeferredFunc;

struct _RemoteAuthBasic {
	RemoteAuth parent;
	CalendarConfig* cfg;
	DeferredFunc* ctx;
};
G_DEFINE_TYPE(RemoteAuthBasic, remote_auth_basic, TYPE_REMOTE_AUTH)

// libsecret schema for username/password (basic authentication)
static const SecretSchema focal_basic_schema = {
	"net.ohwg.focal", SECRET_SCHEMA_NONE, {
											  {"url", SECRET_SCHEMA_ATTRIBUTE_STRING},
											  {"user", SECRET_SCHEMA_ATTRIBUTE_STRING},
											  {"NULL", 0},
										  }};

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

static void on_password_lookup(GObject* source, GAsyncResult* result, gpointer user);

static void password_lookup(RemoteAuthBasic* ba)
{
	CalendarConfig* cfg = ba->cfg;
	secret_password_lookup(&focal_basic_schema, NULL, on_password_lookup, ba,
						   "url", cfg->location,
						   "user", cfg->login,
						   NULL);
}

static void on_password_stored(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	RemoteAuthBasic* ba = (RemoteAuthBasic*) user;
	secret_password_store_finish(result, &error);
	if (error != NULL) {
		g_warning(error->message, G_LOG_LEVEL_WARNING);
		g_error_free(error);
		// cancel current operation
		g_free(ba->ctx);
		ba->ctx = NULL;
	} else {
		// immediately look up the password again so we can continue the originally
		// requested async operation
		password_lookup(ba);
	}
}

static void on_password_lookup(GObject* source, GAsyncResult* result, gpointer user)
{
	GError* error = NULL;
	gchar* password = secret_password_lookup_finish(result, &error);
	RemoteAuthBasic* ba = (RemoteAuthBasic*) user;
	DeferredFunc* dfc = ba->ctx;
	CalendarConfig* cfg = ba->cfg;

	if (error != NULL) {
		g_critical(error->message, G_LOG_LEVEL_CRITICAL);
		g_error_free(error);
	} else if (password == NULL) {
		// no matching password found, prompt the user to create one
		GtkWindow* window = gtk_application_get_active_window(GTK_APPLICATION(g_application_get_default()));
		char* pass = prompt_user_credentials(window, cfg->label, cfg->login);
		if (pass) {
			// We could just use the password here, but there's a good chance that it
			// would be immediately required again, and the next lookup would pop up
			// another dialog. Instead, wait until the password is stored, then trigger
			// a lookup again with the original callback data
			secret_password_store(&focal_basic_schema, SECRET_COLLECTION_DEFAULT, "Focal Remote Calendar password",
								  pass, NULL, on_password_stored, ba,
								  "url", cfg->location,
								  "user", cfg->login,
								  NULL);
			g_free(pass);
			// skip free of dfc, it will be cleaned up in on_password_stored callback
			return;
		} else {
			// user declined to enter password
			g_signal_emit_by_name(ba, "cancelled", NULL);
		}
	} else {
		CURL* curl = curl_easy_init();
		g_assert_nonnull(curl);
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
		curl_easy_setopt(curl, CURLOPT_USERNAME, cfg->login);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
		struct curl_slist* hdrs = curl_slist_append(NULL, "User-Agent: Focal/0.1");
		(*dfc->callback)(dfc->user, curl, hdrs, dfc->arg);
		secret_password_free(password);
	}
	g_free(dfc);
	ba->ctx = NULL;
}

static void remote_auth_basic_new_request(RemoteAuth* ra, void (*callback)(), void* user, void* arg)
{
	RemoteAuthBasic* ba = FOCAL_REMOTE_AUTH_BASIC(ra);
	g_assert_null(ba->ctx);
	ba->ctx = g_new0(DeferredFunc, 1);
	ba->ctx->callback = callback;
	ba->ctx->user = user;
	ba->ctx->arg = arg;

	password_lookup(ba);
}

enum {
	PROP_0,
	PROP_CALENDAR_CONFIG,
};

static void set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
	g_assert(prop_id == PROP_CALENDAR_CONFIG);
	FOCAL_REMOTE_AUTH_BASIC(object)->cfg = g_value_get_pointer(value);
}

void remote_auth_basic_class_init(RemoteAuthBasicClass* klass)
{
	G_OBJECT_CLASS(klass)->set_property = set_property;
	g_object_class_override_property(G_OBJECT_CLASS(klass), PROP_CALENDAR_CONFIG, "cfg");
	FOCAL_REMOTE_AUTH_CLASS(klass)->new_request = remote_auth_basic_new_request;
}

void remote_auth_basic_init(RemoteAuthBasic* rab)
{
}
