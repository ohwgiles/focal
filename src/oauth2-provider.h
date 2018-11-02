/*
 * oauth2-provider.h
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
#ifndef OAUTH2_PROVIDER_H
#define OAUTH2_PROVIDER_H

#include <gtk/gtk.h>

#define TYPE_OAUTH2_PROVIDER (oauth2_provider_get_type())
G_DECLARE_DERIVABLE_TYPE(OAuth2Provider, oauth2_provider, FOCAL, OAUTH2_PROVIDER, GObject)

struct _OAuth2ProviderClass {
	GObjectClass parent;
	const char* (*token_url)();
	char* (*auth_code_query)(const char* code, const char* code_verifier);
	char* (*refresh_token_query)(const char* refresh_token);
	char* (*ext_auth_url)(const char* code);
};

// OAuth2Provider abstracts the differences between different server
// implementations of OAuth2.

// Returns the provider's token url. Typically this is of the form
// https://provider.com/oauth2/v2/token
const char* oauth2_provider_token_url(OAuth2Provider* op);

// Allocates an HTTP query string to be used for an authorization_code
// grant request. This is used to obtain initial access and refresh tokens.
char* oauth2_provider_auth_code_query(OAuth2Provider* op, const char* code, const char* code_verifier);

// Allocates a string to be used as the POST data for a refresh_token
// grant request. This should be used when the access token has expired.
char* oauth2_provider_refresh_token_query(OAuth2Provider* op, const char* refresh_token);

// Allocates an initial HTTP URL (including query parameters) to be
// opened by a web browser so that the user can grant access on the
// provider's platform.
char* oauth2_provider_ext_auth_url(OAuth2Provider* op, const char* code);

#endif //OAUTH2_PROVIDER_H
