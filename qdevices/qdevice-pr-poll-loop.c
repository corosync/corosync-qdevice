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
#include "qdevice-pr-poll-loop.h"
#include "timer-list.h"

static int
wait_for_initial_heuristics_exec_result_timeout_timer_cb(void *data1, void *data2)
{
	int *timeout_called = (int *)data1;

	*timeout_called = 1;

	return (0);
}

int
qdevice_pr_poll_loop_wait_for_initial_heuristics_exec_result(struct qdevice_instance *instance)
{
	struct timer_list_entry *timeout_timer;
	int timeout_called;
	int poll_res;
	PRUint32 timeout;

	timeout_called = 0;

	/*
	 * We know this is never larger than QDEVICE_DEFAULT_HEURISTICS_MAX_TIMEOUT * 2
	 */
	timeout = (PRUint32)instance->heuristics_instance.sync_timeout * 2;

	timeout_timer = timer_list_add(pr_poll_loop_get_timer_list(&instance->main_poll_loop),
	    timeout, wait_for_initial_heuristics_exec_result_timeout_timer_cb,
	    &timeout_called, NULL);

	while ((poll_res = pr_poll_loop_exec(&instance->main_poll_loop)) == 0 &&
	    !instance->vq_node_list_initial_heuristics_finished &&
	    !timeout_called) {
	}

	if (poll_res == -2) {
		log(LOG_CRIT, "Initial heuristics exec result poll failed - internal error");
		return (-1);
	} else if (poll_res == -3) {
		log_nss(LOG_CRIT, "Initial heuristics exec result poll failed - PR_Poll error");
		return (-1);
	}

	if (timeout_called) {
		log(LOG_ERR, "Timeout waiting for initial heuristics exec result");
		return (-1);
	}

	timer_list_entry_delete(pr_poll_loop_get_timer_list(&instance->main_poll_loop), timeout_timer);

	return (poll_res);
}
