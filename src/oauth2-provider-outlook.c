/*
 * oauth2-provider-outlook.c
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
#include "oauth2-provider-outlook.h"

struct _OAuth2ProviderOutlook {
	OAuth2Provider parent;
};
G_DEFINE_TYPE(OAuth2ProviderOutlook, oauth2_provider_outlook, TYPE_OAUTH2_PROVIDER);

#define FOCAL_OUTLOOK_CLIENT_ID "67169a6a-0f0e-40bf-a11c-f56ad0b3fe36"

static const char* outlook_token_url()
{
	return "https://login.microsoftonline.com/common/oauth2/v2.0/token";
}

static char* outlook_auth_code_query(const char* code, const char* code_verifier)
{
	return g_strdup_printf("grant_type=authorization_code"
						   "&code=%s"
						   "&client_id=%s"
						   "&redirect_uri=net.ohwg.focal%%3A%%2F%%2Fauth",
						   code, FOCAL_OUTLOOK_CLIENT_ID);
}

static char* outlook_refresh_token_query(const char* refresh_token)
{
	return g_strdup_printf("grant_type=refresh_token"
						   "&refresh_token=%s"
						   "&client_id=%s",
						   refresh_token, FOCAL_OUTLOOK_CLIENT_ID);
}

static char* outlook_ext_auth_url(const char* code)
{
	return g_strdup_printf("https://login.microsoftonline.com/common/oauth2/v2.0/authorize"
						   "?client_id=" FOCAL_OUTLOOK_CLIENT_ID
						   "&redirect_uri=net.ohwg.focal%%3A%%2F%%2Fauth"
						   "&response_type=code"
						   "&scope=openid+email+offline_access+https%%3A%%2F%%2Foutlook.office.com%%2Fcalendars.readwrite"
						   "&state=%s",
						   code);
}

void oauth2_provider_outlook_class_init(OAuth2ProviderOutlookClass* klass)
{
	FOCAL_OAUTH2_PROVIDER_CLASS(klass)->token_url = outlook_token_url;
	FOCAL_OAUTH2_PROVIDER_CLASS(klass)->auth_code_query = outlook_auth_code_query;
	FOCAL_OAUTH2_PROVIDER_CLASS(klass)->refresh_token_query = outlook_refresh_token_query;
	FOCAL_OAUTH2_PROVIDER_CLASS(klass)->ext_auth_url = outlook_ext_auth_url;
}

void oauth2_provider_outlook_init(OAuth2ProviderOutlook* op)
{
}
