/*
 * remote-auth-oauth2.h
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
#ifndef REMOTE_AUTH_OAUTH2_H
#define REMOTE_AUTH_OAUTH2_H

#include "remote-auth.h"

#define REMOTE_AUTH_OAUTH2_TYPE (remote_auth_oauth2_get_type())
G_DECLARE_FINAL_TYPE(RemoteAuthOAuth2, remote_auth_oauth2, FOCAL, REMOTE_AUTH_OAUTH2, RemoteAuth)

#endif // REMOTE_AUTH_OAUTH2_H
