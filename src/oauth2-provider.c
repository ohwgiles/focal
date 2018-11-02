/*
 * oauth2-provider.c
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
#include "oauth2-provider.h"

G_DEFINE_TYPE(OAuth2Provider, oauth2_provider, G_TYPE_OBJECT);

const char* oauth2_provider_token_url(OAuth2Provider* op)
{
	return FOCAL_OAUTH2_PROVIDER_GET_CLASS(op)->token_url();
}

char* oauth2_provider_auth_code_query(OAuth2Provider* op, const char* code, const char* code_verifier)
{
	return FOCAL_OAUTH2_PROVIDER_GET_CLASS(op)->auth_code_query(code, code_verifier);
}

char* oauth2_provider_refresh_token_query(OAuth2Provider* op, const char* refresh_token)
{
	return FOCAL_OAUTH2_PROVIDER_GET_CLASS(op)->refresh_token_query(refresh_token);
}

char* oauth2_provider_ext_auth_url(OAuth2Provider* op, const char* code)
{
	return FOCAL_OAUTH2_PROVIDER_GET_CLASS(op)->ext_auth_url(code);
}

void oauth2_provider_class_init(OAuth2ProviderClass* klass)
{
}

void oauth2_provider_init(OAuth2Provider* op)
{
}
