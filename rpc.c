/*
 * rpc.c
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
#include <errno.h>
#include <gtk/gtk.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "rpc.h"

static struct sockaddr_un saddr;
static int sd;

static void (*read_callback)(const char*, void*);

static gboolean rpc_handle_new_client(GIOChannel* source, GIOCondition condition, gpointer data)
{
	int cfd = accept(g_io_channel_unix_get_fd(source), NULL, NULL);
	char buffer[256];
	int n = read(cfd, buffer, 255);
	buffer[n] = '\0';
	read_callback(buffer, data);
	return TRUE;
}

rpc_status_t rpc_init()
{
	sd = socket(AF_UNIX, SOCK_STREAM, 0);
	memset(&saddr, 0, sizeof(saddr));
	saddr.sun_family = AF_UNIX;
	sprintf(saddr.sun_path + 1, "focal");

	if (bind(sd, &saddr, sizeof(struct sockaddr_un)) < 0) {
		if (errno == EADDRINUSE)
			return RPC_BIND_INUSE;
		perror("bind");
		return RPC_BIND_ERROR;
	}
	return RPC_BIND_SUCCESS;
}

int rpc_connect()
{
	return connect(sd, &saddr, sizeof(struct sockaddr_un));
}

GIOChannel* rpc_server(void (*callback)(const char*, void*), void* ud)
{
	read_callback = callback;
	listen(sd, 5);
	GIOChannel* channel = g_io_channel_unix_new(sd);
	g_io_add_watch(channel, G_IO_IN, &rpc_handle_new_client, ud);
	return channel;
}

void rpc_send_command(const char* cmd)
{
	write(sd, cmd, strlen(cmd));
}
