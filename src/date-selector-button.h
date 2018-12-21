/*
 * date-selector-button.h
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
#ifndef DATE_SELECTOR_BUTTON_H
#define DATE_SELECTOR_BUTTON_H

#include <gtk/gtk.h>

#define FOCAL_TYPE_DATE_SELECTOR_BUTTON (date_selector_button_get_type())
G_DECLARE_FINAL_TYPE(DateSelectorButton, date_selector_button, FOCAL, DATE_SELECTOR_BUTTON, GtkButton)

#endif // DATE_SELECTOR_BUTTON_H
