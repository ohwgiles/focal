/*
 * oauth2-provider-outlook.h
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
#ifndef OAUTH2_PROVIDER_OUTLOOK_H
#define OAUTH2_PROVIDER_OUTLOOK_H

#include "oauth2-provider.h"

#define TYPE_OAUTH2_PROVIDER_OUTLOOK (oauth2_provider_outlook_get_type())
G_DECLARE_FINAL_TYPE(OAuth2ProviderOutlook, oauth2_provider_outlook, FOCAL, OAUTH2_PROVIDER_OUTLOOK, OAuth2Provider)

#endif //OAUTH2_PROVIDER_OUTLOOK_H
