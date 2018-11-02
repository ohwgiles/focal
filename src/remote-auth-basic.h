/*
 * remote-auth-basic.h
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
#ifndef REMOTE_AUTH_BASIC_H
#define REMOTE_AUTH_BASIC_H

#include "remote-auth.h"

#define REMOTE_AUTH_BASIC_TYPE (remote_auth_basic_get_type())
G_DECLARE_FINAL_TYPE(RemoteAuthBasic, remote_auth_basic, FOCAL, REMOTE_AUTH_BASIC, RemoteAuth)

#endif // REMOTE_AUTH_BASIC_H
