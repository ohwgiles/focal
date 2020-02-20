/*
 * calendar-collection.c
 * This file is part of focal, a calendar application for Linux
 * Copyright 2019-2020 Oliver Giles and focal contributors.
 *
 * Focal is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Focal is distributed without any explicit or implied warranty.
 * You should have received a copy of the GNU General Public License
 * version 3 with focal. If not, see <http://www.gnu.org/licenses/>.
 */

#include "calendar-collection.h"

#include "calendar.h"

typedef struct {
	Calendar* calendar;
	GHashTable* attributes;
	GHashTable* links;
	gboolean initial_sync_done;
	gboolean enabled;
} CalendarItem;

struct _CalendarCollection {
	GObject parent;
	GSList* items;
};

enum {
	SIGNAL_CONFIG_CHANGED,
	SIGNAL_CALENDAR_ADDED,
	SIGNAL_CALENDAR_REMOVED,
	SIGNAL_SYNC_DONE,
	LAST_SIGNAL,
};

static guint calendar_collection_signals[LAST_SIGNAL] = {0};

static GtkTreeModelFlags collection_model_get_flags(GtkTreeModel* tree_model)
{
	return GTK_TREE_MODEL_ITERS_PERSIST | GTK_TREE_MODEL_LIST_ONLY;
}

static gint collection_model_get_n_columns(GtkTreeModel* tree_model)
{
	return 1;
}

static GType collection_model_get_column_type(GtkTreeModel* tree_model, gint index)
{
	return index == 0 ? G_TYPE_STRING : G_TYPE_INVALID;
}

static gboolean collection_model_iter_children(GtkTreeModel* tree_model, GtkTreeIter* iter, GtkTreeIter* parent)
{
	if (parent)
		return FALSE;

	CalendarCollection* cc = FOCAL_CALENDAR_COLLECTION(tree_model);
	if (!cc->items)
		return FALSE;

	iter->user_data = cc->items;
	return TRUE;
}

static gboolean collection_model_get_iter(GtkTreeModel* tree_model, GtkTreeIter* iter, GtkTreePath* path)
{
	iter->user_data = g_slist_nth(FOCAL_CALENDAR_COLLECTION(tree_model)->items, gtk_tree_path_get_indices(path)[0]);
	return TRUE;
}

static GtkTreePath* collection_model_get_path(GtkTreeModel* tree_model, GtkTreeIter* iter)
{
	GtkTreePath* path;
	path = gtk_tree_path_new();
	gtk_tree_path_append_index(path, g_slist_index(FOCAL_CALENDAR_COLLECTION(tree_model)->items, ((GSList*) iter->user_data)->data));
	return path;
}

static void collection_model_get_value(GtkTreeModel* tree_model, GtkTreeIter* iter, gint column, GValue* value)
{
	CalendarItem* item = (CalendarItem*) ((GSList*) iter->user_data)->data;
	g_value_init(value, G_TYPE_STRING);
	g_value_set_string(value, calendar_get_name(item->calendar));
}

static gboolean collection_model_iter_next(GtkTreeModel* tree_model, GtkTreeIter* iter)
{
	iter->user_data = ((GSList*) iter->user_data)->next;
	return iter->user_data != NULL;
}

static gboolean collection_model_iter_has_child(GtkTreeModel* tree_model, GtkTreeIter* iter)
{
	return FALSE;
}

static gint collection_model_iter_n_children(GtkTreeModel* tree_model, GtkTreeIter* iter)
{
	if (iter == NULL)
		return (gint) g_slist_length(FOCAL_CALENDAR_COLLECTION(tree_model)->items);
	return 0;
}

static gboolean collection_model_iter_nth_child(GtkTreeModel* tree_model, GtkTreeIter* iter, GtkTreeIter* parent, gint n)
{
	if (parent)
		return FALSE;
	iter->user_data = g_slist_nth(FOCAL_CALENDAR_COLLECTION(tree_model)->items, n);
	return TRUE;
}

static gboolean collection_model_iter_parent(GtkTreeModel* tree_model, GtkTreeIter* iter, GtkTreeIter* child)
{
	return FALSE;
}

static void calendar_collection_tree_model_init(GtkTreeModelIface* iface)
{
	iface->get_flags = collection_model_get_flags;
	iface->get_n_columns = collection_model_get_n_columns;
	iface->get_column_type = collection_model_get_column_type;
	iface->get_iter = collection_model_get_iter;
	iface->get_path = collection_model_get_path;
	iface->get_value = collection_model_get_value;
	iface->iter_next = collection_model_iter_next;
	iface->iter_children = collection_model_iter_children;
	iface->iter_has_child = collection_model_iter_has_child;
	iface->iter_n_children = collection_model_iter_n_children;
	iface->iter_nth_child = collection_model_iter_nth_child;
	iface->iter_parent = collection_model_iter_parent;
}

G_DEFINE_TYPE_WITH_CODE(CalendarCollection, calendar_collection, G_TYPE_MENU_MODEL, G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_MODEL, calendar_collection_tree_model_init))

static void item_free(gpointer item)
{
	CalendarItem* ci = (CalendarItem*) item;
	g_object_unref(ci->calendar);
	free(item);
}

static void remove_all_calendars(CalendarCollection* cc)
{
	for (GSList* p = cc->items; p; p = p->next) {
		CalendarItem* item = (CalendarItem*) p->data;
		g_signal_handlers_disconnect_by_data(item->calendar, cc);
		g_signal_emit(cc, calendar_collection_signals[SIGNAL_CALENDAR_REMOVED], 0, item->calendar);
	}
	g_slist_free_full(cc->items, item_free);
	cc->items = NULL;
}

static void finalize(GObject* obj)
{
	CalendarCollection* cc = FOCAL_CALENDAR_COLLECTION(obj);
	remove_all_calendars(cc);
	G_OBJECT_CLASS(calendar_collection_parent_class)->finalize(obj);
}

static gboolean is_mutable(GMenuModel* mm)
{
	return FALSE;
}

static gint get_n_items(GMenuModel* mm)
{
	return (gint) g_slist_length(FOCAL_CALENDAR_COLLECTION(mm)->items);
}

static void get_item_attributes(GMenuModel* model, gint position, GHashTable** table)
{
	*table = g_hash_table_ref(((CalendarItem*) g_slist_nth_data(FOCAL_CALENDAR_COLLECTION(model)->items, position))->attributes);
}

static void get_item_links(GMenuModel* model, gint position, GHashTable** table)
{
	*table = g_hash_table_ref(((CalendarItem*) g_slist_nth_data(FOCAL_CALENDAR_COLLECTION(model)->items, position))->links);
}

static void calendar_collection_class_init(CalendarCollectionClass* ccc)
{
	GObjectClass* object_class = G_OBJECT_CLASS(ccc);
	GMenuModelClass* model_class = G_MENU_MODEL_CLASS(ccc);

	object_class->finalize = finalize;

	model_class->is_mutable = is_mutable;
	model_class->get_n_items = get_n_items;
	model_class->get_item_attributes = get_item_attributes;
	model_class->get_item_links = get_item_links;

	calendar_collection_signals[SIGNAL_CONFIG_CHANGED] = g_signal_new("config-changed", G_TYPE_FROM_CLASS(object_class), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
	calendar_collection_signals[SIGNAL_CALENDAR_ADDED] = g_signal_new("calendar-added", G_TYPE_FROM_CLASS(object_class), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
	calendar_collection_signals[SIGNAL_CALENDAR_REMOVED] = g_signal_new("calendar-removed", G_TYPE_FROM_CLASS(object_class), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
	calendar_collection_signals[SIGNAL_SYNC_DONE] = g_signal_new("sync-done", G_TYPE_FROM_CLASS(object_class), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION, 0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_BOOLEAN, G_TYPE_POINTER);
}

static void calendar_collection_init(CalendarCollection* cc)
{
}

CalendarCollection* calendar_collection_new()
{
	return g_object_new(FOCAL_TYPE_CALENDAR_COLLECTION, NULL);
}

Calendar* calendar_collection_get_by_name(CalendarCollection* cc, const gchar* name)
{
	if (!name)
		return NULL;

	for (GSList* p = cc->items; p; p = p->next) {
		CalendarItem* item = (CalendarItem*) p->data;
		if (strcmp(calendar_get_name(item->calendar), name) == 0)
			return item->calendar;
	}
	return NULL;
}

static void on_calendar_config_modified(CalendarCollection* cc, Calendar* c)
{
	g_signal_emit(cc, calendar_collection_signals[SIGNAL_CONFIG_CHANGED], 0, c);
}

static gint calendar_item_from_calendar(CalendarItem* item, Calendar* cal)
{
	return item->calendar == cal ? 0 : 1;
}

static void on_calendar_sync_done(CalendarCollection* cc, gboolean success, Calendar* cal)
{
	g_signal_emit(cc, calendar_collection_signals[SIGNAL_SYNC_DONE], 0, success, cal);
}

static void on_calendar_initial_sync_done(CalendarCollection* cc, gboolean success, Calendar* cal)
{
	g_signal_handlers_disconnect_by_func(cal, (gpointer) on_calendar_initial_sync_done, cc);

	GSList* el = g_slist_find_custom(cc->items, cal, (GCompareFunc) calendar_item_from_calendar);
	g_assert_nonnull(el);

	CalendarItem* item = (CalendarItem*) el->data;
	item->initial_sync_done = TRUE;

	// Whether the initial sync succeeded or not is ignored. This is imperfect because if it later
	// succeeds, the WeekView might get a *large* number of event-updated signals.
	// In conjunction with this, FocalApp needs to check if there are any errors when a calendar is
	// added to the collection (and not just when it receives an error signal)
	g_signal_connect_swapped(cal, "sync-done", (GCallback) on_calendar_sync_done, cc);
	g_signal_emit(cc, calendar_collection_signals[SIGNAL_CALENDAR_ADDED], 0, cal);
}

void calendar_collection_populate_from_config(CalendarCollection* cc, GSList* configs)
{
	remove_all_calendars(cc);

	for (GSList* p = configs; p; p = p->next) {
		Calendar* cal = calendar_create(p->data);
		g_signal_connect_swapped(cal, "config-modified", (GCallback) on_calendar_config_modified, cc);

		CalendarItem* item = g_new0(CalendarItem, 1);
		item->calendar = cal;
		item->attributes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
		item->links = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
		item->enabled = TRUE;

		char* action_name = g_strdup_printf("win.toggle-calendar.%s", calendar_get_name(cal));
		g_hash_table_insert(item->attributes, G_MENU_ATTRIBUTE_LABEL, g_variant_new_string(calendar_get_name(cal)));
		g_hash_table_insert(item->attributes, G_MENU_ATTRIBUTE_ACTION, g_variant_new_string(action_name));
		g_free(action_name);

		cc->items = g_slist_append(cc->items, item);
		// Perform initial sync once, before signalling calendar-added. This means for example,
		// the calendar won't be added to the week view before the initial sync, which would have
		// caused it to be inundanted with event-updated signals.
		g_signal_connect_swapped(cal, "sync-done", G_CALLBACK(on_calendar_initial_sync_done), cc);
		calendar_sync(cal);
	}
}

void calendar_collection_sync_all(CalendarCollection* cc)
{
	for (GSList* p = cc->items; p; p = p->next)
		calendar_sync(FOCAL_CALENDAR(((CalendarItem*) p->data)->calendar));
}

static gint is_enabled(gconstpointer data, CalendarItem* item)
{
	return item->enabled;
}

CalendarCollectionIterator calendar_collection_iterator(CalendarCollection* cc, enum CalendarCollectionIteratorFlags flags)
{
	CalendarCollectionIterator it;
	it.flags = flags;

	it.priv = (flags & CALENDAR_COLLECTION_ITERATOR_FLAGS_ONLY_VISIBLE) ? g_slist_find_custom(cc->items, NULL, (GCompareFunc) is_enabled) : cc->items;
	it.cal = cc->items ? ((CalendarItem*) cc->items->data)->calendar : NULL;

	return it;
}

void calendar_collection_iterator_next(CalendarCollectionIterator* cci)
{
	cci->priv = (cci->flags & CALENDAR_COLLECTION_ITERATOR_FLAGS_ONLY_VISIBLE) ? g_slist_find_custom(cci->priv, NULL, (GCompareFunc) is_enabled) : ((GSList*) cci->priv)->next;
	cci->cal = cci->priv ? ((CalendarItem*) ((GSList*) cci->priv)->data)->calendar : NULL;
}

enum ModelFilterFlags {
	MODEL_FILTER_FLAGS_NONE = 0,
	MODEL_FILTER_FLAGS_ONLY_ENABLED = 1 << 0,
	MODEL_FILTER_FLAGS_ONLY_WRITABLE = 1 << 1
};

static gboolean calendar_collection_model_filter(GtkTreeModel* model, GtkTreeIter* iter, gpointer data)
{
	gintptr flags = (gintptr) data;
	GSList* el = (GSList*) iter->user_data;
	CalendarItem* item = (CalendarItem*) el->data;

	gboolean display =
		(!(flags & MODEL_FILTER_FLAGS_ONLY_ENABLED) || item->enabled) &&
		(!(flags & MODEL_FILTER_FLAGS_ONLY_WRITABLE) || !calendar_is_read_only(item->calendar));

	return display;
}

GtkTreeModel* calendar_collection_new_filtered_model(CalendarCollection* cc, gboolean only_enabled, gboolean only_writable)
{
	GtkTreeModel* model = gtk_tree_model_filter_new(GTK_TREE_MODEL(cc), NULL);
	gintptr flags = 0;

	if (only_enabled)
		flags |= MODEL_FILTER_FLAGS_ONLY_ENABLED;
	if (only_writable)
		flags |= MODEL_FILTER_FLAGS_ONLY_WRITABLE;

	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(model), calendar_collection_model_filter, (gpointer) flags, NULL);

	return model;
}

void calendar_collection_set_enabled(CalendarCollection* cc, Calendar* c, gboolean enabled)
{
	GSList* el = g_slist_find_custom(cc->items, c, (GCompareFunc) calendar_item_from_calendar);
	((CalendarItem*) el->data)->enabled = enabled;
}
