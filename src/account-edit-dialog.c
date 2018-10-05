/*
 * account-edit-dialog.c
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
#include "account-edit-dialog.h"
#include "calendar-config.h"
#include "remote-auth.h"

#include <stdlib.h>
#include <string.h>

struct _AccountEditDialog {
	GtkDialog parent;
	GtkWidget* grid;
	GtkWidget* combo_type;
	GtkWidget* name;
	GtkWidget* email;
	GtkWidget* file_path;
	GtkWidget* caldav_url;
	GtkWidget* caldav_user;
	RemoteAuth* auth;
	CalendarConfig* config;
};
G_DEFINE_TYPE(AccountEditDialog, account_edit_dialog, GTK_TYPE_DIALOG);

static void on_auth_success(AccountEditDialog* dialog)
{
	// fake like OK was pressed, close the dialog
	g_signal_emit_by_name(dialog, "response", GTK_RESPONSE_OK);
}
static void dialog_response(AccountEditDialog* dialog, gint response_id);

static void open_web_login(GtkWidget* button, AccountEditDialog* dialog)
{
	// call the dialog-response handler. This is to save the changes in the GUI to the
	// CalendarConfig object, which is necessary so that the RemoteAuth will perform
	// the correct type of authentication
	dialog_response(dialog, GTK_RESPONSE_OK);
	gtk_button_set_label(GTK_BUTTON(button), "Waiting for response");
	remote_auth_new_request(dialog->auth, on_auth_success, NULL);
	// TODO handle closing the window before the response comes (prevent OK, allow cancel)
}

static void edit_accounts_form_create(AccountEditDialog* dialog)
{
	while (gtk_grid_get_child_at(GTK_GRID(dialog->grid), 0, 3))
		gtk_grid_remove_row(GTK_GRID(dialog->grid), 3);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->combo_type))) {
	case CAL_TYPE_CALDAV:
		dialog->caldav_url = gtk_entry_new();
		dialog->caldav_user = gtk_entry_new();
		gtk_grid_attach(GTK_GRID(dialog->grid), gtk_label_new("URL"), 0, 3, 1, 1);
		gtk_grid_attach(GTK_GRID(dialog->grid), dialog->caldav_url, 1, 3, 1, 1);
		gtk_grid_attach(GTK_GRID(dialog->grid), gtk_label_new("Username"), 0, 4, 1, 1);
		gtk_grid_attach(GTK_GRID(dialog->grid), dialog->caldav_user, 1, 4, 1, 1);
		break;
	case CAL_TYPE_GOOGLE: {
		GtkWidget* btn = gtk_button_new_with_label("Click to login");
		g_signal_connect(btn, "clicked", (GCallback) open_web_login, dialog);
		gtk_grid_attach(GTK_GRID(dialog->grid), btn, 0, 3, 1, 1);
	} break;
	case CAL_TYPE_FILE:
		dialog->file_path = gtk_entry_new();
		gtk_grid_attach(GTK_GRID(dialog->grid), gtk_label_new("File Path"), 0, 3, 1, 1);
		gtk_grid_attach(GTK_GRID(dialog->grid), dialog->file_path, 1, 3, 1, 1);
		break;
	}
}

static void populate_fields(AccountEditDialog* dialog)
{
	gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(dialog->name)), dialog->config->label, -1);
	gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(dialog->email)), dialog->config->email, -1);
	gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->combo_type), (gint) dialog->config->type);
	switch (dialog->config->type) {
	case CAL_TYPE_CALDAV:
		gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(dialog->caldav_url)), dialog->config->location, -1);
		gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(dialog->caldav_user)), dialog->config->login, -1);
		break;
	case CAL_TYPE_GOOGLE:
		break;
	case CAL_TYPE_FILE:
		gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(dialog->file_path)), dialog->config->location, -1);
		break;
	}
}

static void account_type_changed(AccountEditDialog* dialog, GtkComboBox* account_type)
{
	edit_accounts_form_create(dialog);
	gtk_widget_show_all(dialog->grid);
}

static void dialog_response(AccountEditDialog* dialog, gint response_id)
{
	if (response_id == GTK_RESPONSE_OK) {
		free(dialog->config->label);
		free(dialog->config->email);
		switch (dialog->config->type) {
		case CAL_TYPE_CALDAV:
			free(dialog->config->location);
			free(dialog->config->login);
			break;
		case CAL_TYPE_GOOGLE:
			free(dialog->config->location);
			break;
		case CAL_TYPE_FILE:
			free(dialog->config->location);
			break;
		}
		dialog->config->type = (CalendarAccountType) gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->combo_type));
		dialog->config->label = strdup(gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(dialog->name))));
		dialog->config->email = strdup(gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(dialog->email))));
		switch (dialog->config->type) {
		case CAL_TYPE_CALDAV:
			dialog->config->location = strdup(gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(dialog->caldav_url))));
			dialog->config->login = strdup(gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(dialog->caldav_user))));
			break;
		case CAL_TYPE_GOOGLE:
			// TODO: remove duplication with calendar_create, and handle the case where the configured email doesn't match the actual logged in one
			dialog->config->location = g_strdup_printf("https://apidata.googleusercontent.com/caldav/v2/%s/events/", dialog->config->email);
			break;
		case CAL_TYPE_FILE:
			dialog->config->location = strdup(gtk_entry_buffer_get_text(gtk_entry_get_buffer(GTK_ENTRY(dialog->file_path))));
			break;
		}
	}
}

GtkWidget* account_edit_dialog_new(GtkWindow* parent_window, CalendarConfig* cfg)
{
	AccountEditDialog* dialog = g_object_new(FOCAL_TYPE_ACCOUNT_EDIT_DIALOG, NULL);
	gtk_window_set_transient_for(GTK_WINDOW(dialog), parent_window);
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

	dialog->config = cfg;

	GtkWidget* header = gtk_header_bar_new();
	gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Edit Account");
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
	gtk_window_set_titlebar(GTK_WINDOW(dialog), header);

	gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button(GTK_DIALOG(dialog), "_OK", GTK_RESPONSE_OK);
	g_signal_connect(dialog, "response", (GCallback) dialog_response, NULL);

	GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	g_object_set(content_area, "margin", 10, NULL);

	dialog->grid = gtk_grid_new();

	dialog->name = gtk_entry_new();
	gtk_grid_attach(GTK_GRID(dialog->grid), gtk_label_new("Name"), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(dialog->grid), dialog->name, 1, 0, 1, 1);

	dialog->email = gtk_entry_new();
	gtk_grid_attach(GTK_GRID(dialog->grid), gtk_label_new("Email"), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(dialog->grid), dialog->email, 1, 1, 1, 1);

	dialog->combo_type = gtk_combo_box_text_new();
	for (int i = CAL_TYPE__FIRST; i <= CAL_TYPE__LAST; ++i) {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialog->combo_type), calendar_type_as_string(i));
	}
	gtk_grid_attach(GTK_GRID(dialog->grid), dialog->combo_type, 0, 2, 2, 1);

	gtk_container_add(GTK_CONTAINER(content_area), dialog->grid);
	g_signal_connect_swapped(dialog->combo_type, "changed", (GCallback) account_type_changed, dialog);

	if (dialog->config == NULL) {
		dialog->config = g_new0(CalendarConfig, 1);
		// default type selection for a new entry
		gtk_combo_box_set_active(GTK_COMBO_BOX(dialog->combo_type), CAL_TYPE_CALDAV);
	} else {
		populate_fields(dialog);
	}

	dialog->auth = remote_auth_new(dialog->config, dialog);

	return GTK_WIDGET(dialog);
}

static void finalize(GObject* gobject)
{
	remote_auth_free(FOCAL_ACCOUNT_EDIT_DIALOG(gobject)->auth);
	G_OBJECT_CLASS(account_edit_dialog_parent_class)->finalize(gobject);
}

static void account_edit_dialog_class_init(AccountEditDialogClass* klass)
{
	G_OBJECT_CLASS(klass)->finalize = finalize;
}

static void account_edit_dialog_init(AccountEditDialog* dialog)
{
}

CalendarConfig* account_edit_dialog_get_account(AccountEditDialog* dialog)
{
	return dialog->config;
}
