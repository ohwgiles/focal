/*
 * event-private.h
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
#ifndef EVENT_PRIVATE_H
#define EVENT_PRIVATE_H

/* This file provides a mechanism for attaching private data
 * to an icalcomponent, allowing the use of a single pointer
 * for both regular use of the libical API and extended use
 * by the application */

#include <libical/ical.h>

typedef struct {
	/* Focal application fields defined here */
	char* url;
} EventPrivate;

EventPrivate* icalcomponent_get_private(icalcomponent* cmp);

EventPrivate* icalcomponent_create_private(icalcomponent* cmp);

void icalcomponent_free_private(icalcomponent* cmp);

#endif // EVENT_PRIVATE_H
