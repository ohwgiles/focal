/*
 * accounts-dialog.c
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
#include "accounts-dialog.h"
#include "account-edit-dialog.h"
#include "calendar-config.h"

struct _AccountsDialog {
	GtkDialog parent;
	GtkWidget* list;
	GSList** accounts;
};
G_DEFINE_TYPE(AccountsDialog, accounts_dialog, GTK_TYPE_DIALOG)

enum {
	SIGNAL_CONFIG_CHANGED,
	LAST_SIGNAL,
};

static guint accounts_dialog_signals[LAST_SIGNAL] = {0};

static void populate_account_list(AccountsDialog* dialog)
{
	gtk_container_forall(GTK_CONTAINER(dialog->list), (GtkCallback) gtk_widget_destroy, NULL);
	for (GSList* p = *dialog->accounts; p; p = p->next) {
		CalendarConfig* cfg = (CalendarConfig*) p->data;
		gtk_container_add(GTK_CONTAINER(dialog->list), gtk_label_new(cfg->label));
	}
	gtk_widget_show_all(dialog->list);
}

static void edit_accounts_response(AccountsDialog* dialog, gint response_id, AccountEditDialog* edit_dialog)
{
	if (response_id == GTK_RESPONSE_OK) {
		CalendarConfig* cfg = account_edit_dialog_get_account(edit_dialog);
		if (g_slist_find(*dialog->accounts, cfg) == NULL) {
			// new configuration
			*dialog->accounts = g_slist_append(*dialog->accounts, cfg);
		}
		g_signal_emit(dialog, accounts_dialog_signals[SIGNAL_CONFIG_CHANGED], 0);
		populate_account_list(dialog);
	}
	gtk_widget_destroy(GTK_WIDGET(edit_dialog));
}

static void on_clicked_new(AccountsDialog* dialog, GtkToolButton* button)
{
	GtkWidget* edit_dialog = account_edit_dialog_new(gtk_window_get_transient_for(GTK_WINDOW(dialog)), NULL);
	g_signal_connect_swapped(edit_dialog, "response", (GCallback) edit_accounts_response, dialog);
	gtk_widget_show_all(edit_dialog);
}

static void on_clicked_edit(AccountsDialog* dialog, GtkToolButton* button)
{
	gint selected_index = gtk_list_box_row_get_index(gtk_list_box_get_selected_row(GTK_LIST_BOX(dialog->list)));
	g_assert(selected_index >= 0);
	CalendarConfig* cfg = g_slist_nth_data(*dialog->accounts, (guint) selected_index);
	GtkWidget* edit_dialog = account_edit_dialog_new(gtk_window_get_transient_for(GTK_WINDOW(dialog)), cfg);
	g_signal_connect_swapped(edit_dialog, "response", (GCallback) edit_accounts_response, dialog);
	gtk_widget_show_all(edit_dialog);
}

static void on_clicked_delete(AccountsDialog* dialog, GtkToolButton* button)
{
	GtkWidget* confirm = gtk_message_dialog_new(gtk_window_get_transient_for(GTK_WINDOW(dialog)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Are you sure you want to remove this calendar?");
	gint ret = gtk_dialog_run(GTK_DIALOG(confirm));
	if (ret == GTK_RESPONSE_YES) {
		gint selected_index = gtk_list_box_row_get_index(gtk_list_box_get_selected_row(GTK_LIST_BOX(dialog->list)));
		g_assert(selected_index >= 0);
		GSList* to_remove = g_slist_nth(*dialog->accounts, (guint) selected_index);
		*dialog->accounts = g_slist_remove_link(*dialog->accounts, to_remove);
		calendar_config_free(to_remove->data);
		g_signal_emit(dialog, accounts_dialog_signals[SIGNAL_CONFIG_CHANGED], 0);
		populate_account_list(dialog);
	}
	gtk_widget_destroy(confirm);
}

static void accounts_dialog_class_init(AccountsDialogClass* klass)
{
	accounts_dialog_signals[SIGNAL_CONFIG_CHANGED] = g_signal_new("accounts-changed", G_TYPE_FROM_CLASS((GObjectClass*) klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void accounts_dialog_init(AccountsDialog* self)
{
}

GtkWidget* accounts_dialog_new(GtkWindow* parent_window, GSList** accounts)
{
	AccountsDialog* dialog = g_object_new(FOCAL_TYPE_ACCOUNTS_DIALOG, NULL);
	dialog->accounts = accounts;
	gtk_window_set_transient_for(GTK_WINDOW(dialog), parent_window);
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

	GtkWidget* header = gtk_header_bar_new();
	gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Accounts");
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
	gtk_window_set_titlebar(GTK_WINDOW(dialog), header);

	GtkWidget* content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	g_object_set(content_area, "margin", 10, NULL);

	GtkWidget* win = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(win), 250);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(win), 150);
	gtk_widget_set_vexpand(win, TRUE);
	dialog->list = gtk_list_box_new();

	populate_account_list(dialog);
	gtk_container_add(GTK_CONTAINER(win), dialog->list);

	GtkWidget* toolbar = gtk_toolbar_new();
	gtk_tool_item_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
	gtk_toolbar_set_show_arrow(GTK_TOOLBAR(toolbar), FALSE);
	gtk_toolbar_set_icon_size(GTK_TOOLBAR(toolbar), GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "inline-toolbar");

	GtkToolItem* btn = gtk_tool_button_new(gtk_image_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR), NULL);
	g_signal_connect_swapped(btn, "clicked", (GCallback) on_clicked_new, dialog);
	gtk_container_add(GTK_CONTAINER(toolbar), GTK_WIDGET(btn));

	btn = gtk_tool_button_new(gtk_image_new_from_icon_name("edit-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR), NULL);
	g_signal_connect_swapped(btn, "clicked", (GCallback) on_clicked_edit, dialog);
	gtk_container_add(GTK_CONTAINER(toolbar), GTK_WIDGET(btn));

	btn = gtk_tool_button_new(gtk_image_new_from_icon_name("list-remove-symbolic", GTK_ICON_SIZE_SMALL_TOOLBAR), NULL);
	g_signal_connect_swapped(btn, "clicked", (GCallback) on_clicked_delete, dialog);
	gtk_container_add(GTK_CONTAINER(toolbar), GTK_WIDGET(btn));

	gtk_container_add(GTK_CONTAINER(content_area), win);
	gtk_container_add(GTK_CONTAINER(content_area), toolbar);

	return GTK_WIDGET(dialog);
}
