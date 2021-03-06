/*
 * Copyright (c) 2015-2016 Red Hat, Inc.
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

#include <sys/types.h>

#include <string.h>

#include "qnet-config.h"
#include "qnetd-client.h"

void
qnetd_client_init(struct qnetd_client *client, PRFileDesc *sock, PRNetAddr *addr,
    char *addr_str,
    size_t max_receive_size, size_t max_send_buffers, size_t max_send_size,
    struct timer_list *main_timer_list)
{

	memset(client, 0, sizeof(*client));
	client->socket = sock;
	client->addr_str = addr_str;
	memcpy(&client->addr, addr, sizeof(*addr));
	dynar_init(&client->receive_buffer, max_receive_size);
	send_buffer_list_init(&client->send_buffer_list, max_send_buffers, max_send_size);
	node_list_init(&client->configuration_node_list);
	node_list_init(&client->last_membership_node_list);
	node_list_init(&client->last_quorum_node_list);
	client->main_timer_list = main_timer_list;
	/*
	 * Set max heartbeat interval before client sends init msg
	 */
	client->heartbeat_interval = QNETD_DEFAULT_HEARTBEAT_INTERVAL_MAX;
}

void
qnetd_client_destroy(struct qnetd_client *client)
{

	free(client->cluster_name);
	free(client->addr_str);
	node_list_free(&client->last_quorum_node_list);
	node_list_free(&client->last_membership_node_list);
	node_list_free(&client->configuration_node_list);
	send_buffer_list_free(&client->send_buffer_list);
	dynar_destroy(&client->receive_buffer);
}
