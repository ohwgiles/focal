/*
 * async-curl.c
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
#include <glib-unix.h>
#include <gtk/gtk.h>

#include "async-curl.h"

/* GUnixFDSource doesn't provide a public API to access the tag member,
 * and consequently a polled unix FD can't be modified. "Fix" this by
 * peeking into the ABI. Will have to be fixed if GUnixFDSource changes */
typedef struct {
	GSource source;
	gint fd;
	gpointer tag;
} GUnixFDSource;

typedef struct {
	AsyncCurlCallback callback;
	void* user;
} CallbackInfo;

static CURLM* multi;

static void check_multi_info()
{
	CURLMsg* msg;
	int msgs_left;
	while ((msg = curl_multi_info_read(multi, &msgs_left))) {
		if (msg->msg == CURLMSG_DONE) {
			CURL* hdl = msg->easy_handle;
			CallbackInfo* cbinfo;
			curl_easy_getinfo(hdl, CURLINFO_PRIVATE, &cbinfo);
			curl_multi_remove_handle(multi, hdl);
			(*cbinfo->callback)(hdl, msg->data.result, cbinfo->user);
			curl_easy_cleanup(hdl);
			free(cbinfo);
		} else {
			fprintf(stderr, "error: unexpected message %d\n", msg->msg);
		}
	}
}

static gboolean on_socket_event(gint fd, GIOCondition condition, gpointer user_data)
{
	int ev_bitmask = 0;
	if (condition & G_IO_IN)
		ev_bitmask |= CURL_CSELECT_IN;
	if (condition & G_IO_OUT)
		ev_bitmask |= CURL_CSELECT_OUT;

	int running;
	CURLMcode rc = curl_multi_socket_action(multi, fd, ev_bitmask, &running);
	if (rc != 0)
		fprintf(stderr, "error %s\n", curl_multi_strerror(rc));

	check_multi_info();

	return TRUE;
}

static int on_modify_socket(CURL* e, curl_socket_t s, int what, void* cbp, void* sockp)
{
	intptr_t p = (intptr_t) sockp;
	if (what == CURL_POLL_REMOVE) {
		g_source_remove((guint) p);
	} else {
		GIOCondition cond = 0;
		if (what & CURL_POLL_IN)
			cond |= G_IO_IN;
		if (what & CURL_POLL_OUT)
			cond |= G_IO_OUT;

		if (!p) {
			p = g_unix_fd_add(s, cond, on_socket_event, multi);
			curl_multi_assign(multi, s, (void*) p);
		} else {
			GSource* src = g_main_context_find_source_by_id(NULL, (guint) p);
			GUnixFDSource* usrc = (GUnixFDSource*) src;
			g_source_modify_unix_fd(src, usrc->tag, cond);
		}
	}
	return 0;
}

static gboolean on_timer_event(gpointer user_data)
{
	int running = 0;
	curl_multi_socket_action(user_data, CURL_SOCKET_TIMEOUT, 0, &running);
	return FALSE;
}

static int timer_callback(CURLM* multi, long timeout_ms, void* userp)
{
	if (timeout_ms == 0) {
		on_timer_event(multi);
	} else if (timeout_ms > 0) {
		g_timeout_add(timeout_ms, on_timer_event, multi);
	}

	return 0;
}

void async_curl_add_request(CURL* handle, AsyncCurlCallback cb, void* user)
{
	g_assert_nonnull(multi);
	CallbackInfo* cbinfo = (CallbackInfo*) malloc(sizeof(CallbackInfo));
	cbinfo->callback = cb;
	cbinfo->user = user;
	curl_easy_setopt(handle, CURLOPT_PRIVATE, cbinfo);
	curl_multi_add_handle(multi, handle);

	int still_running;
	CURLMcode rc = curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &still_running);
	g_assert(rc == 0);
}

void async_curl_init()
{
	g_assert_null(multi);
	multi = curl_multi_init();
	curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, on_modify_socket);
	curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, timer_callback);
}

void async_curl_cleanup()
{
	g_assert_nonnull(multi);
	curl_multi_cleanup(multi);
	multi = NULL;
}
