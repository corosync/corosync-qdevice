/*
 * Copyright (c) 2015-2019 Red Hat, Inc.
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
#include "qdevice-config.h"
#include "qdevice-ipc.h"
#include "unix-socket-ipc.h"
#include "dynar-simple-lex.h"
#include "dynar-str.h"
#include "qdevice-ipc-cmd.h"

/*
 * Callbacks
 */

/*
 * IPC server socket
 */
static int
ipc_socket_poll_loop_set_events_cb(int fd, short *events, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;

	if (qdevice_ipc_is_closed(instance)) {
		log(LOG_DEBUG, "Listening socket is closed");

		return (-2);
	}

	return (0);
}

static int
ipc_socket_poll_loop_read_cb(int fd, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;
	struct unix_socket_client *ipc_client;

	qdevice_ipc_accept(instance, &ipc_client);

	return (0);
}

static int
ipc_socket_poll_loop_err_cb(int fd, short revents, void *user_data1, void *user_data2)
{

	if (revents != POLLNVAL) {
		/*
		 * Poll ERR on listening socket is fatal error.
		 * POLL_NVAL is used as a signal to quit poll loop.
		 */
		log(LOG_CRIT, "POLL_ERR (%u) on listening socket", revents);
	} else {
		log(LOG_DEBUG, "Listening socket is closed");
	}

	return (-1);
}

/*
 * IPC client sockets
 */
static int
ipc_client_socket_poll_loop_set_events_cb(int fd, short *events, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;
	struct unix_socket_client *ipc_client = (struct unix_socket_client *)user_data2;

	if (ipc_client->schedule_disconnect) {
		qdevice_ipc_client_disconnect(instance, ipc_client);

		if (pr_poll_loop_del_fd(&instance->main_poll_loop, fd) == -1) {
			log(LOG_ERR, "pr_poll_loop_del_fd for ipc client socket failed");

			return (-2);
		}

		return (-1);
	}

	if (!ipc_client->reading_line && !ipc_client->writing_buffer) {
		return (-1);
	}

	if (ipc_client->reading_line) {
		*events |= POLLIN;
	}

	if (ipc_client->writing_buffer) {
		*events |= POLLOUT;
	}

	return (0);
}

static int
ipc_client_socket_poll_loop_read_cb(int fd, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;
	struct unix_socket_client *ipc_client = (struct unix_socket_client *)user_data2;

	if (!ipc_client->schedule_disconnect) {
		qdevice_ipc_io_read(instance, ipc_client);
	}

	return (0);
}

static int
ipc_client_socket_poll_loop_write_cb(int fd, void *user_data1, void *user_data2)
{
	struct qdevice_instance *instance = (struct qdevice_instance *)user_data1;
	struct unix_socket_client *ipc_client = (struct unix_socket_client *)user_data2;

	if (!ipc_client->schedule_disconnect) {
		qdevice_ipc_io_write(instance, ipc_client);
	}

	return (0);
}

static int
ipc_client_socket_poll_loop_err_cb(int fd, short revents, void *user_data1, void *user_data2)
{
	struct unix_socket_client *ipc_client = (struct unix_socket_client *)user_data2;

	if (!ipc_client->schedule_disconnect) {
		log(LOG_DEBUG, "POLL_ERR (%u) on ipc client socket."
		    " Disconnecting.", revents);

		ipc_client->schedule_disconnect = 1;
	}

	return (0);
}

/*
 * Exported functions
 */
int
qdevice_ipc_init(struct qdevice_instance *instance)
{

	if (unix_socket_ipc_init(&instance->local_ipc,
	    instance->advanced_settings->local_socket_file,
	    instance->advanced_settings->set_local_socket_umask,
	    instance->advanced_settings->local_socket_umask,
	    instance->advanced_settings->local_socket_gid,
	    instance->advanced_settings->local_socket_backlog,
	    instance->advanced_settings->ipc_max_clients,
	    instance->advanced_settings->ipc_max_receive_size,
	    instance->advanced_settings->ipc_max_send_size) != 0) {
		log_err(LOG_ERR, "Can't create unix socket");

		return (-1);
	}

	if (pr_poll_loop_add_fd(&instance->main_poll_loop, instance->local_ipc.socket, POLLIN,
	    ipc_socket_poll_loop_set_events_cb,
	    ipc_socket_poll_loop_read_cb,
	    NULL,
	    ipc_socket_poll_loop_err_cb, instance, NULL) == -1) {
		log(LOG_ERR, "Can't add IPC socket to main poll loop");

		return (-1);
	}

	return (0);
}

int
qdevice_ipc_close(struct qdevice_instance *instance)
{
	int res;

	res = unix_socket_ipc_close(&instance->local_ipc);
	if (res != 0) {
		log_err(LOG_WARNING, "Can't close local IPC");
	}

	return (res);
}

int
qdevice_ipc_is_closed(struct qdevice_instance *instance)
{

	return (unix_socket_ipc_is_closed(&instance->local_ipc));
}

int
qdevice_ipc_destroy(struct qdevice_instance *instance)
{
	int res;
	struct unix_socket_client *client;
	const struct unix_socket_client_list *ipc_client_list;

	ipc_client_list = &instance->local_ipc.clients;

	TAILQ_FOREACH(client, ipc_client_list, entries) {
		free(client->user_data);
	}

	res = unix_socket_ipc_destroy(&instance->local_ipc);
	if (res != 0) {
		log_err(LOG_WARNING, "Can't destroy local IPC");
	}

	return (res);
}

int
qdevice_ipc_accept(struct qdevice_instance *instance, struct unix_socket_client **res_client)
{
	int res;
	int accept_res;

	accept_res = unix_socket_ipc_accept(&instance->local_ipc, res_client);

	switch (accept_res) {
	case -1:
		log_err(LOG_ERR, "Can't accept local IPC connection");
		res = -1;
		goto return_res;
		break;
	case -2:
		log(LOG_ERR, "Maximum IPC clients reached. Not accepting connection");
		res = -1;
		goto return_res;
		break;
	case -3:
		log(LOG_ERR, "Can't add client to list");
		res = -1;
		goto return_res;
		break;
	default:
		unix_socket_client_read_line(*res_client, 1);
		res = 0;
		break;
	}

	(*res_client)->user_data = malloc(sizeof(struct qdevice_ipc_user_data));
	if ((*res_client)->user_data == NULL) {
		log(LOG_ERR, "Can't alloc IPC client user data");
		res = -1;
		qdevice_ipc_client_disconnect(instance, *res_client);

		goto return_res;
	} else {
		memset((*res_client)->user_data, 0, sizeof(struct qdevice_ipc_user_data));
	}

	if (pr_poll_loop_add_fd(&instance->main_poll_loop, (*res_client)->socket, 0,
	    ipc_client_socket_poll_loop_set_events_cb,
	    ipc_client_socket_poll_loop_read_cb,
	    ipc_client_socket_poll_loop_write_cb,
	    ipc_client_socket_poll_loop_err_cb, instance, *res_client) == -1) {
		log(LOG_ERR, "Can't add IPC client socket to main poll loop");
		res = -1;

		qdevice_ipc_client_disconnect(instance, *res_client);

		goto return_res;
	}

return_res:
	return (res);
}

void
qdevice_ipc_client_disconnect(struct qdevice_instance *instance, struct unix_socket_client *client)
{

	free(client->user_data);
	unix_socket_ipc_client_disconnect(&instance->local_ipc, client);
}

int
qdevice_ipc_send_error(struct qdevice_instance *instance, struct unix_socket_client *client,
    const char *error_fmt, ...)
{
	va_list ap;
	int res;

	va_start(ap, error_fmt);
	res = ((dynar_str_cpy(&client->send_buffer, "Error\n") == 0) &&
	    (dynar_str_vcatf(&client->send_buffer, error_fmt, ap) > 0) &&
	    (dynar_str_cat(&client->send_buffer, "\n") == 0));

	va_end(ap);

	if (res) {
		unix_socket_client_write_buffer(client, 1);
	} else {
		log(LOG_ERR, "Can't send ipc error to client (buffer too small)");
	}

	return (res ? 0 : -1);
}

int
qdevice_ipc_send_buffer(struct qdevice_instance *instance, struct unix_socket_client *client)
{

	if (dynar_str_prepend(&client->send_buffer, "OK\n") != 0) {
		log(LOG_ERR, "Can't send ipc message to client (buffer too small)");

		if (qdevice_ipc_send_error(instance, client, "Internal IPC buffer too small") != 0) {
			return (-1);
		}

		return (0);
	}

	unix_socket_client_write_buffer(client, 1);

	return (0);
}

static void
qdevice_ipc_parse_line(struct qdevice_instance *instance, struct unix_socket_client *client)
{
	struct dynar_simple_lex lex;
	struct dynar *token;
	char *str;
	struct qdevice_ipc_user_data *ipc_user_data;
	int verbose;

	ipc_user_data = (struct qdevice_ipc_user_data *)client->user_data;

	dynar_simple_lex_init(&lex, &client->receive_buffer, DYNAR_SIMPLE_LEX_TYPE_PLAIN);
	token = dynar_simple_lex_token_next(&lex);

	verbose = 0;

	if (token == NULL) {
		log(LOG_ERR, "Can't alloc memory for simple lex");

		if (qdevice_ipc_send_error(instance, client, "Command too long") != 0) {
			client->schedule_disconnect = 1;
		}

		return;
	}

	str = dynar_data(token);
	if (strcasecmp(str, "") == 0) {
		log(LOG_DEBUG, "IPC client doesn't send command");
		if (qdevice_ipc_send_error(instance, client, "No command specified") != 0) {
			client->schedule_disconnect = 1;
		}
	} else if (strcasecmp(str, "shutdown") == 0) {
		log(LOG_DEBUG, "IPC client requested shutdown");

		ipc_user_data->shutdown_requested = 1;

		if (qdevice_ipc_send_buffer(instance, client) != 0) {
			client->schedule_disconnect = 1;
		}
	} else if (strcasecmp(str, "status") == 0) {
		token = dynar_simple_lex_token_next(&lex);

		if (token != NULL && (str = dynar_data(token), strcmp(str, "")) != 0) {
			if (strcasecmp(str, "verbose") == 0) {
				verbose = 1;
			}
		}

		if (qdevice_ipc_cmd_status(instance, &client->send_buffer, verbose) != 0) {
			if (qdevice_ipc_send_error(instance, client, "Can't get QDevice status") != 0) {
				client->schedule_disconnect = 1;
			}
		} else {
			if (qdevice_ipc_send_buffer(instance, client) != 0) {
				client->schedule_disconnect = 1;
			}
		}
	} else {
		log(LOG_DEBUG, "IPC client sent unknown command");
		if (qdevice_ipc_send_error(instance, client, "Unknown command '%s'", str) != 0) {
			client->schedule_disconnect = 1;
		}
	}

	dynar_simple_lex_destroy(&lex);
}

void
qdevice_ipc_io_read(struct qdevice_instance *instance, struct unix_socket_client *client)
{
	int res;

	res = unix_socket_client_io_read(client);

	switch (res) {
	case 0:
		/*
		 * Partial read
		 */
		break;
	case -1:
		log(LOG_DEBUG, "IPC client closed connection");
		client->schedule_disconnect = 1;
		break;
	case -2:
		log(LOG_ERR, "Can't store message from IPC client. Disconnecting client.");
		client->schedule_disconnect = 1;
		break;
	case -3:
		log_err(LOG_ERR, "Can't receive message from IPC client. Disconnecting client.");
		client->schedule_disconnect = 1;
		break;
	case 1:
		/*
		 * Full message received
		 */
		unix_socket_client_read_line(client, 0);

		qdevice_ipc_parse_line(instance, client);
		break;
	}
}

void
qdevice_ipc_io_write(struct qdevice_instance *instance, struct unix_socket_client *client)
{
	int res;
	struct qdevice_ipc_user_data *ipc_user_data;

	ipc_user_data = (struct qdevice_ipc_user_data *)client->user_data;

	res = unix_socket_client_io_write(client);

	switch (res) {
	case 0:
		/*
		 * Partial send
		 */
		break;
	case -1:
		log(LOG_DEBUG, "IPC client closed connection");
		client->schedule_disconnect = 1;
		break;
	case -2:
		log_err(LOG_ERR, "Can't send message to IPC client. Disconnecting client");
		client->schedule_disconnect = 1;
		break;
	case 1:
		/*
		 * Full message sent
		 */
		unix_socket_client_write_buffer(client, 0);
		client->schedule_disconnect = 1;

		if (ipc_user_data->shutdown_requested) {
			qdevice_ipc_close(instance);
		}

		break;
	}
}
