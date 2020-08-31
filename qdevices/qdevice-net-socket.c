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
#include "msg.h"
#include "msgio.h"
#include "qnet-config.h"
#include "qdevice-net-msg-received.h"
#include "qdevice-net-nss.h"
#include "qdevice-net-send.h"
#include "qdevice-net-socket.h"

/*
 * Socket callbacks
 */
static int
socket_set_events_cb(PRFileDesc *prfd, short *events, void *user_data1, void *user_data2)
{
	struct qdevice_net_instance *instance = (struct qdevice_net_instance *)user_data1;

	if (!send_buffer_list_empty(&instance->send_buffer_list)) {
		*events |= POLLOUT;
	}

	return (0);
}

static int
socket_read_cb(PRFileDesc *prfd, const PRPollDesc *pd, void *user_data1, void *user_data2)
{
	struct qdevice_net_instance *instance = (struct qdevice_net_instance *)user_data1;

	if (qdevice_net_socket_read(instance) == -1) {
		instance->schedule_disconnect = 1;

		return (-1);
	}

	return (0);
}

static int
socket_write_cb(PRFileDesc *prfd, const PRPollDesc *pd, void *user_data1, void *user_data2)
{
	struct qdevice_net_instance *instance = (struct qdevice_net_instance *)user_data1;

	if (qdevice_net_socket_write(instance) == -1) {
		instance->schedule_disconnect = 1;

		return (-1);
	}

	return (0);
}

static int
non_blocking_client_socket_write_cb(PRFileDesc *prfd, const PRPollDesc *pd, void *user_data1,
    void *user_data2)
{
	int res;

	struct qdevice_net_instance *instance = (struct qdevice_net_instance *)user_data1;

	res = nss_sock_non_blocking_client_succeeded(pd);
	if (res == -1) {
		/*
		 * Connect failed -> remove this fd from main loop and try next
		 */
		res = qdevice_net_socket_del_from_main_poll_loop(instance);
		if (res == -1) {
			return (-1);
		}

		res = nss_sock_non_blocking_client_try_next(&instance->non_blocking_client);
		if (res == -1) {
			log_nss(LOG_ERR, "Can't connect to qnetd host.");
			nss_sock_non_blocking_client_destroy(&instance->non_blocking_client);
		}

		res = qdevice_net_socket_add_to_main_poll_loop(instance);
		if (res == -1) {
			return (-1);
		}
	} else if (res == 0) {
		/*
		 * Poll again
		 */
	} else if (res == 1) {
		/*
		 * Connect success -> delete socket from main loop and add final one
		 */
		res = qdevice_net_socket_del_from_main_poll_loop(instance);
		if (res == -1) {
			return (-1);
		}

		instance->socket = instance->non_blocking_client.socket;
		nss_sock_non_blocking_client_destroy(&instance->non_blocking_client);
		instance->non_blocking_client.socket = NULL;

		instance->state = QDEVICE_NET_INSTANCE_STATE_SENDING_PREINIT_REPLY;

		res = qdevice_net_socket_add_to_main_poll_loop(instance);
		if (res == -1) {
			return (-1);
		}

		log(LOG_DEBUG, "Sending preinit msg to qnetd");
		if (qdevice_net_send_preinit(instance) != 0) {
			instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;
			return (-1);
		}
	} else {
		log(LOG_CRIT, "Unhandled nss_sock_non_blocking_client_succeeded");
		exit(EXIT_FAILURE);
	}

	return (0);
}

static int
socket_err_cb(PRFileDesc *prfd, short revents, const PRPollDesc *pd, void *user_data1, void *user_data2)
{
	struct qdevice_net_instance *instance = (struct qdevice_net_instance *)user_data1;

	log(LOG_ERR, "POLL_ERR (%u) on main socket", revents);

	instance->schedule_disconnect = 1;
	instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_SERVER_CLOSED_CONNECTION;

	return (-1);
}

static int
non_blocking_client_socket_err_cb(PRFileDesc *prfd, short revents, const PRPollDesc *pd,
    void *user_data1, void *user_data2)
{
	struct qdevice_net_instance *instance = (struct qdevice_net_instance *)user_data1;

	/*
	 * Workaround for RHEL<7. Pollout is never set for nonblocking connect (doesn't work
	 * only with poll, select works as expected!???).
	 * So test if client is still valid and if pollout was not already called (ensured
	 * by default because of order in PR_Poll).
	 * If both applies it's possible to emulate pollout set by calling poll_write.
	 */
	if (!instance->non_blocking_client.destroyed) {
		return (non_blocking_client_socket_write_cb(prfd, pd, user_data1, user_data2));
	}

	return (0);
}
/*
 * Exported functions
 */

/*
 * -1 means end of connection (EOF) or some other unhandled error. 0 = success
 */
int
qdevice_net_socket_read(struct qdevice_net_instance *instance)
{
	int res;
	int ret_val;
	int orig_skipping_msg;

	orig_skipping_msg = instance->skipping_msg;

	res = msgio_read(instance->socket, &instance->receive_buffer,
	    &instance->msg_already_received_bytes, &instance->skipping_msg);

	if (!orig_skipping_msg && instance->skipping_msg) {
		log(LOG_DEBUG, "msgio_read set skipping_msg");
	}

	ret_val = 0;

	switch (res) {
	case 0:
		/*
		 * Partial read
		 */
		break;
	case -1:
		log(LOG_DEBUG, "Server closed connection");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_SERVER_CLOSED_CONNECTION;
		ret_val = -1;
		break;
	case -2:
		log(LOG_ERR, "Unhandled error when reading from server. "
		    "Disconnecting from server");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_READ_MESSAGE;
		ret_val = -1;
		break;
	case -3:
		log(LOG_ERR, "Can't store message header from server. "
		    "Disconnecting from server");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_READ_MESSAGE;
		ret_val = -1;
		break;
	case -4:
		log(LOG_ERR, "Can't store message from server. "
		    "Disconnecting from server");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_READ_MESSAGE;
		ret_val = -1;
		break;
	case -5:
		log(LOG_WARNING, "Server sent unsupported msg type %u. "
		    "Disconnecting from server", msg_get_type(&instance->receive_buffer));
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_UNSUPPORTED_MSG;
		ret_val = -1;
		break;
	case -6:
		log(LOG_WARNING,
		    "Server wants to send too long message %u bytes. Disconnecting from server",
		    msg_get_len(&instance->receive_buffer));
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_READ_MESSAGE;
		ret_val = -1;
		break;
	case 1:
		/*
		 * Full message received / skipped
		 */
		if (!instance->skipping_msg) {
			if (qdevice_net_msg_received(instance) == -1) {
				ret_val = -1;
			}
		} else {
			log(LOG_CRIT, "net_socket_read in skipping msg state");
			exit(EXIT_FAILURE);
		}

		instance->skipping_msg = 0;
		instance->msg_already_received_bytes = 0;
		dynar_clean(&instance->receive_buffer);
		break;
	default:
		log(LOG_CRIT, "qdevice_net_socket_read unhandled error %d", res);
		exit(EXIT_FAILURE);
		break;
	}

	return (ret_val);
}

static int
qdevice_net_socket_write_finished(struct qdevice_net_instance *instance)
{
	PRFileDesc *new_pr_fd;

	if (instance->state == QDEVICE_NET_INSTANCE_STATE_WAITING_STARTTLS_BEING_SENT) {
		/*
		 * StartTLS sent to server. Begin with TLS handshake
		 */
		if ((new_pr_fd = nss_sock_start_ssl_as_client(instance->socket,
		    instance->advanced_settings->net_nss_qnetd_cn,
		    qdevice_net_nss_bad_cert_hook,
		    qdevice_net_nss_get_client_auth_data,
		    instance, 0, NULL)) == NULL) {
			log_nss(LOG_ERR, "Can't start TLS");
			instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_START_TLS;
			return (-1);
		}

		/*
		 * And send init msg
		 */
		if (qdevice_net_send_init(instance) != 0) {
			instance->disconnect_reason =
			    QDEVICE_NET_DISCONNECT_REASON_CANT_ALLOCATE_MSG_BUFFER;

			return (-1);
		}

		instance->socket = new_pr_fd;
		instance->using_tls = 1;
	}

	return (0);
}

int
qdevice_net_socket_write(struct qdevice_net_instance *instance)
{
	int res;
	struct send_buffer_list_entry *send_buffer;
	enum msg_type sent_msg_type;

	send_buffer = send_buffer_list_get_active(&instance->send_buffer_list);
	if (send_buffer == NULL) {
		log(LOG_CRIT, "send_buffer_list_get_active returned NULL");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SEND_MESSAGE;

		return (-1);
	}

	res = msgio_write(instance->socket, &send_buffer->buffer,
	    &send_buffer->msg_already_sent_bytes);

	if (res == 1) {
		sent_msg_type = msg_get_type(&send_buffer->buffer);

		send_buffer_list_delete(&instance->send_buffer_list, send_buffer);

		if (sent_msg_type != MSG_TYPE_ECHO_REQUEST) {
			if (qdevice_net_socket_write_finished(instance) == -1) {
				return (-1);
			}
		}
	}

	if (res == -1) {
		log_nss(LOG_CRIT, "PR_Send returned 0");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_SERVER_CLOSED_CONNECTION;
		return (-1);
	}

	if (res == -2) {
		log_nss(LOG_ERR, "Unhandled error when sending message to server");
		instance->disconnect_reason = QDEVICE_NET_DISCONNECT_REASON_CANT_SEND_MESSAGE;

		return (-1);
	}

	return (0);
}

int
qdevice_net_socket_add_to_main_poll_loop(struct qdevice_net_instance *instance)
{

	if (instance->state != QDEVICE_NET_INSTANCE_STATE_WAITING_CONNECT ||
	    !instance->non_blocking_client.destroyed) {
		if (instance->state == QDEVICE_NET_INSTANCE_STATE_WAITING_CONNECT) {
			if (pr_poll_loop_add_prfd(&instance->qdevice_instance_ptr->main_poll_loop,
			    instance->non_blocking_client.socket,
			    POLLOUT|POLLPRI,
			    NULL, NULL, non_blocking_client_socket_write_cb,
			    non_blocking_client_socket_err_cb,
			    instance, NULL) != 0) {
				log(LOG_ERR, "Can't add net socket (non_blocking_client) "
				    "fd to main poll loop");

				return (-1);
			}
		} else {
			if (pr_poll_loop_add_prfd(&instance->qdevice_instance_ptr->main_poll_loop,
			    instance->socket,
			    POLLIN,
			    socket_set_events_cb, socket_read_cb, socket_write_cb, socket_err_cb,
			    instance, NULL) != 0) {
				log(LOG_ERR, "Can't add net socket fd to main poll loop");

				return (-1);
			}
		}
	}

	return (0);
}

int
qdevice_net_socket_del_from_main_poll_loop(struct qdevice_net_instance *instance)
{

	if (!instance->non_blocking_client.destroyed) {
		if (pr_poll_loop_del_prfd(&instance->qdevice_instance_ptr->main_poll_loop,
		    instance->non_blocking_client.socket) != 0) {
			log(LOG_ERR, "Can't remove net socket (non_blocking_client) "
			    "fd from main poll loop");

			return (-1);
		}
	}

	if (instance->socket != NULL) {
		if (pr_poll_loop_del_prfd(&instance->qdevice_instance_ptr->main_poll_loop,
		    instance->socket) != 0) {
			log(LOG_ERR, "Can't remove net socket fd from main poll loop");

			return (-1);
		}
	}

	return (0);
}
