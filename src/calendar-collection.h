/*
 * calendar-collection.h
 * This file is part of focal, a calendar application for Linux
 * Copyright 2019 Oliver Giles and focal contributors.
 *
 * Focal is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Focal is distributed without any explicit or implied warranty.
 * You should have received a copy of the GNU General Public License
 * version 3 with focal. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CALENDAR_COLLECTION_H
#define CALENDAR_COLLECTION_H

#include <gtk/gtk.h>

// CalendarCollection owns a number of Calendar objects. It implements
// GtkTreeModel (so that calendars can be presented in e.g. a combo box)
// and GMenuModel (so that calendars can be presented in a menu).

#define FOCAL_TYPE_CALENDAR_COLLECTION (calendar_collection_get_type())
G_DECLARE_FINAL_TYPE(CalendarCollection, calendar_collection, FOCAL, CALENDAR_COLLECTION, GMenuModel)

typedef struct _Calendar Calendar;

CalendarCollection* calendar_collection_new(void);

void calendar_collection_populate_from_config(CalendarCollection* cc, GSList* configs);

Calendar* calendar_collection_get_by_name(CalendarCollection* cc, const gchar* name);

void calendar_collection_set_enabled(CalendarCollection* cc, Calendar* c, gboolean enabled);

void calendar_collection_sync_all(CalendarCollection* cc);

GtkTreeModel* calendar_collection_new_filtered_model(CalendarCollection* cc, gboolean only_enabled, gboolean only_writable);

enum CalendarCollectionIteratorFlags {
	CALENDAR_COLLECTION_ITERATOR_FLAGS_NONE,
	CALENDAR_COLLECTION_ITERATOR_FLAGS_ONLY_VISIBLE
};

typedef struct {
	Calendar* cal;
	gpointer priv;
	enum CalendarCollectionIteratorFlags flags;
} CalendarCollectionIterator;

CalendarCollectionIterator calendar_collection_iterator(CalendarCollection* cc, enum CalendarCollectionIteratorFlags flags);

void calendar_collection_iterator_next(CalendarCollectionIterator* cci);

#define calendar_collection_foreach(it, cc) \
	for (CalendarCollectionIterator it = calendar_collection_iterator(cc, CALENDAR_COLLECTION_ITERATOR_FLAGS_NONE); it.cal; calendar_collection_iterator_next(&it))

#define calendar_collection_foreach_visible(it, cc) \
	for (CalendarCollectionIterator it = calendar_collection_iterator(cc, CALENDAR_COLLECTION_ITERATOR_FLAGS_ONLY_VISIBLE); it.cal; calendar_collection_iterator_next(&it))

#endif // CALENDAR_COLLECTION_H
