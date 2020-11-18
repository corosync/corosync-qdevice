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
#include "qnetd-client-dpd-timer.h"

static int
qnetd_dpd_timer_cb(void *data1, void *data2)
{
	struct qnetd_client *client;

	client = (struct qnetd_client *)data1;

	log(LOG_WARNING, "Client %s doesn't sent any message during "
	    "%" PRIu32 "ms. Disconnecting",
	    client->addr_str,
	    timer_list_entry_get_interval(client->dpd_timer));

	client->schedule_disconnect = 1;
	/*
	 * Timer gets removed by timer-list because of returning 0
	 */
	client->dpd_timer = NULL;

	return (0);
}

int
qnetd_client_dpd_timer_init(struct qnetd_instance *instance, struct qnetd_client *client)
{

	if (!instance->advanced_settings->dpd_enabled) {
		return (0);
	}

	client->dpd_timer = timer_list_add(pr_poll_loop_get_timer_list(&instance->main_poll_loop),
	    (PRUint32)(instance->advanced_settings->dpd_interval_coefficient * client->heartbeat_interval),
	    qnetd_dpd_timer_cb, (void *)client, NULL);
	if (client->dpd_timer == NULL) {
		log(LOG_ERR, "Can't initialize dpd timer for client %s", client->addr_str);

		return (-1);
	}

	return (0);
}

void
qnetd_client_dpd_timer_destroy(struct qnetd_instance *instance, struct qnetd_client *client)
{

	if (client->dpd_timer != NULL) {
		timer_list_entry_delete(pr_poll_loop_get_timer_list(&instance->main_poll_loop),
		    client->dpd_timer);

		client->dpd_timer = NULL;
	}
}

void
qnetd_client_dpd_timer_reschedule(struct qnetd_instance *instance, struct qnetd_client *client)
{

	if (client->dpd_timer != NULL) {
		timer_list_entry_reschedule(pr_poll_loop_get_timer_list(&instance->main_poll_loop),
		    client->dpd_timer);
	}
}

int
qnetd_client_dpd_timer_update_interval(struct qnetd_instance *instance, struct qnetd_client *client)
{
	int res;

	if (client->dpd_timer == NULL) {
		return (0);
	}

	res = timer_list_entry_set_interval(pr_poll_loop_get_timer_list(&instance->main_poll_loop),
	    client->dpd_timer,
	    (PRUint32)(instance->advanced_settings->dpd_interval_coefficient * client->heartbeat_interval));

	return (res);
}
