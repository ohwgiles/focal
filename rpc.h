/*
 * rpc.h
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
#ifndef RPC_H
#define RPC_H

struct _GIOChannel;
typedef struct _GIOChannel GIOChannel;

typedef enum {
	RPC_BIND_SUCCESS,
	RPC_BIND_INUSE,
	RPC_BIND_ERROR
} rpc_status_t;

rpc_status_t rpc_init(void);

GIOChannel* rpc_server(void (*callback)(const char*, void*), void* userdata);

int rpc_connect();

void rpc_send_command(const char*);

#endif // RPC_H
