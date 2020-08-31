/*
 * Copyright (c) 2015-2020 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Friesse (jfriesse@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Red Hat, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "log.h"
#include "qdevice-cmap.h"
#include "qdevice-heuristics-cmd.h"
#include "qdevice-heuristics-log.h"
#include "qdevice-pr-poll-loop-cb.h"
#include "qdevice-votequorum.h"

static int
heuristics_pipe_log_recv_read_cb(int fd, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;
	int res;

	res = qdevice_heuristics_log_read_from_pipe(&instance->heuristics_instance);
	if (res == -1) {
		instance->heuristics_closed = 1;
		return (-1);
	}

	return (0);
}

/*
 * Callback is shared for all heuristics pipes
 */
static int
heuristics_pipe_err_cb(int fd, short revents, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;

	instance->heuristics_closed = 1;

	/*
	 * Closed pipe doesn't mean return of PR_POLL_READ. To display
	 * better log message, we call read log as if POLLIN would
	 * be set. Ignore error code because loop closes anyway.
	 */
	(void)qdevice_heuristics_log_read_from_pipe(&instance->heuristics_instance);
	log(LOG_DEBUG, "POLL_ERR (%u) on heuristics pipe. "
	     "Disconnecting.",  revents);

	return (-1);
}

static int
heuristics_pipe_cmd_recv_read_cb(int fd, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;
	int res;

	res = qdevice_heuristics_cmd_read_from_pipe(&instance->heuristics_instance);
	if (res == -1) {
		instance->heuristics_closed = 1;
		return (-1);
	}

	return (0);
}

static int
heuristics_pipe_cmd_send_set_events_cb(int fd, short *events, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;
	int res;

	res = -1;

	if (!send_buffer_list_empty(&instance->heuristics_instance.cmd_out_buffer_list)) {
		*events |= POLLOUT;
		res = 0;
	}

	return (res);
}

static int
heuristics_pipe_cmd_send_write_cb(int fd, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;
	int res;

	res = qdevice_heuristics_cmd_write(&instance->heuristics_instance);
	if (res == -1) {
		instance->heuristics_closed = 1;
		return (-1);
	}

	return (0);
}

static int
votequorum_read_cb(int fd, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;
	int res;

	res = qdevice_votequorum_dispatch(instance);
	if (res == -1) {
		instance->votequorum_closed = 1;
		return (-1);
	}

	return (0);
}

static int
votequorum_err_cb(int fd, short revents, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;

	instance->votequorum_closed = 1;

	log(LOG_DEBUG, "POLL_ERR (%u) on corosync socket. "
	     "Disconnecting.",  revents);

	return (-1);
}

static int
cmap_set_events_cb(int fd, short *events, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;

	if (instance->sync_in_progress) {
		/*
		 * During sync cmap is blocked -> don't add fd
		 */
		return (-1);
	}

	return (0);
}

static int
cmap_read_cb(int fd, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;
	int res;

	res = qdevice_cmap_dispatch(instance);
	if (res == -1) {
		instance->cmap_closed = 1;
		return (-1);
	}

	return (0);
}

static int
cmap_err_cb(int fd, short revents, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;

	instance->cmap_closed = 1;

	log(LOG_DEBUG, "POLL_ERR (%u) on corosync socket. "
	     "Disconnecting.",  revents);

	return (-1);
}

int
qdevice_pr_poll_loop_cb_register(struct qdevice_instance *instance)
{

	if (pr_poll_loop_add_fd(&instance->main_poll_loop, instance->heuristics_instance.pipe_log_recv,
	    POLLIN, NULL, heuristics_pipe_log_recv_read_cb, NULL, heuristics_pipe_err_cb,
	    instance, NULL) != 0) {
		log(LOG_ERR, "Can't add heuristics log pipe to main poll loop");

		return (-1);
	}

	if (pr_poll_loop_add_fd(&instance->main_poll_loop, instance->heuristics_instance.pipe_cmd_recv,
	    POLLIN, NULL, heuristics_pipe_cmd_recv_read_cb, NULL, heuristics_pipe_err_cb,
	    instance, NULL) != 0) {
		log(LOG_ERR, "Can't add heuristics cmd recv pipe to main poll loop");

		return (-1);
	}

	if (pr_poll_loop_add_fd(&instance->main_poll_loop, instance->heuristics_instance.pipe_cmd_send,
	    0, heuristics_pipe_cmd_send_set_events_cb, NULL,
	    heuristics_pipe_cmd_send_write_cb, heuristics_pipe_err_cb,
	    instance, NULL) != 0) {
		log(LOG_ERR, "Can't add heuristics cmd send pipe to main poll loop");

		return (-1);
	}

	if (pr_poll_loop_add_fd(&instance->main_poll_loop, instance->votequorum_poll_fd,
	    POLLIN, NULL, votequorum_read_cb, NULL, votequorum_err_cb,
	    instance, NULL) != 0) {
		log(LOG_ERR, "Can't add votequorum fd to main poll loop");

		return (-1);
	}

	if (pr_poll_loop_add_fd(&instance->main_poll_loop, instance->cmap_poll_fd,
	    POLLIN, cmap_set_events_cb, cmap_read_cb, NULL, cmap_err_cb,
	    instance, NULL) != 0) {
		log(LOG_ERR, "Can't add votequorum fd to main poll loop");

		return (-1);
	}

	return (0);
}
