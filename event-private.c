/*
 * event-private.c
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
#include <glib.h>

#include "event-private.h"

/* To implement this function, the libical ABI has to be known. This
 * must be updated if libical changes */

/* An icalproperty is abused to store a pointer to the private data.
 * ABI-compatible definition of icalproperty: */
typedef struct {
	char __unused1[5];
	icalproperty_kind kind;
	char* __unused2[5];
	EventPrivate priv;
} icalproperty_compat;

/* Should not conflict with pre-existing types in icalproperty_kind */
#define ICAL_FOCAL_PRIV_PROPERTY ((icalproperty_kind) 174)

gboolean icalcomponent_has_private(icalcomponent* cmp)
{
	icalproperty_compat* prop = (icalproperty_compat*) icalcomponent_get_first_property(cmp, ICAL_FOCAL_PRIV_PROPERTY);
	return prop != NULL;
}

EventPrivate* icalcomponent_get_private(icalcomponent* cmp)
{
	icalproperty_compat* prop = (icalproperty_compat*) icalcomponent_get_first_property(cmp, ICAL_FOCAL_PRIV_PROPERTY);
	g_assert_nonnull(prop);
	return &prop->priv;
}

EventPrivate* icalcomponent_create_private(icalcomponent* cmp)
{
	icalproperty_compat* prop = g_new0(icalproperty_compat, 1);
	prop->kind = ICAL_FOCAL_PRIV_PROPERTY;
	icalcomponent_add_property(cmp, (icalproperty*) prop);
	return &prop->priv;
}

void icalcomponent_free_private(icalcomponent* cmp)
{
	icalproperty_compat* prop = (icalproperty_compat*) icalcomponent_get_first_property(cmp, ICAL_FOCAL_PRIV_PROPERTY);
	g_assert_nonnull(prop);
	free(prop);
}
