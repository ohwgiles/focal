/*
 * accounts-dialog.h
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
#ifndef ACCOUNTS_DIALOG_H
#define ACCOUNTS_DIALOG_H

#include <gtk/gtk.h>

#define FOCAL_TYPE_ACCOUNTS_DIALOG (accounts_dialog_get_type())
G_DECLARE_FINAL_TYPE(AccountsDialog, accounts_dialog, FOCAL, ACCOUNTS_DIALOG, GtkDialog)

GtkWidget* accounts_dialog_new(GtkWindow* parent_window, GSList* accounts);

#endif // ACCOUNTS_DIALOG_H
