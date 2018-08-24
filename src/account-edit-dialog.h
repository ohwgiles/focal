/*
 * account-edit-dialog.h
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
#ifndef ACCOUNT_EDIT_DIALOG_H
#define ACCOUNT_EDIT_DIALOG_H

#include <gtk/gtk.h>

#include "calendar-config.h"

#define FOCAL_TYPE_ACCOUNT_EDIT_DIALOG (account_edit_dialog_get_type())
G_DECLARE_FINAL_TYPE(AccountEditDialog, account_edit_dialog, FOCAL, ACCOUNT_EDIT_DIALOG, GtkDialog)

GtkWidget* account_edit_dialog_new(GtkWindow* parent_window, CalendarConfig* cfg);

CalendarConfig* account_edit_dialog_get_account(AccountEditDialog* dialog);

#endif // ACCOUNT_EDIT_DIALOG_H
