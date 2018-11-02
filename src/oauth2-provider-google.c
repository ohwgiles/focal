/*
 * oauth2-provider-google.c
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
#include "oauth2-provider-google.h"

struct _OAuth2ProviderGoogle {
	OAuth2Provider parent;
};
G_DEFINE_TYPE(OAuth2ProviderGoogle, oauth2_provider_google, TYPE_OAUTH2_PROVIDER);

#define FOCAL_GOOGLE_CLIENT_ID "96466437028-gee73t1rh4t84r4ddf1i17ucpdf8hr3s.apps.googleusercontent.com"

static const char* google_token_url()
{
	return "https://www.googleapis.com/oauth2/v4/token";
}

static char* google_auth_code_query(const char* code, const char* code_verifier)
{
	return g_strdup_printf("code=%s"
						   "&client_id=%s"
						   "&redirect_uri=net.ohwg.focal%%3A%%2Fauth"
						   "&grant_type=authorization_code"
						   "&code_verifier=%s",
						   code, FOCAL_GOOGLE_CLIENT_ID, code_verifier);
}

static char* google_refresh_token_query(const char* refresh_token)
{
	return g_strdup_printf("grant_type=refresh_token"
						   "&refresh_token=%s"
						   "&client_id=%s"
						   "&approval_prompt=force"
						   "&access_type=offline",
						   refresh_token, FOCAL_GOOGLE_CLIENT_ID);
}

static char* google_ext_auth_url(const char* code)
{
	return g_strdup_printf("https://accounts.google.com/o/oauth2/v2/auth"
						   "?client_id=" FOCAL_GOOGLE_CLIENT_ID
						   "&redirect_uri=net.ohwg.focal%%3A%%2Fauth"
						   "&response_type=code"
						   "&scope=openid+email+https%%3A%%2F%%2Fwww.googleapis.com%%2Fauth%%2Fcalendar"
						   "&access_type=offline"
						   "&state=%s"
						   "&code_challenge=%s",
						   code, code);
}

void oauth2_provider_google_class_init(OAuth2ProviderGoogleClass* klass)
{
	FOCAL_OAUTH2_PROVIDER_CLASS(klass)->token_url = google_token_url;
	FOCAL_OAUTH2_PROVIDER_CLASS(klass)->auth_code_query = google_auth_code_query;
	FOCAL_OAUTH2_PROVIDER_CLASS(klass)->refresh_token_query = google_refresh_token_query;
	FOCAL_OAUTH2_PROVIDER_CLASS(klass)->ext_auth_url = google_ext_auth_url;
}

void oauth2_provider_google_init(OAuth2ProviderGoogle* op)
{
}
