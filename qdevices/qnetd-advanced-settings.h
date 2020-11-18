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

#ifndef _QNETD_ADVANCED_SETTINGS_H_
#define _QNETD_ADVANCED_SETTINGS_H_

#ifdef __cplusplus
extern "C" {
#endif

struct qnetd_advanced_settings {
	int listen_backlog;
	size_t max_client_send_buffers;
	size_t max_client_send_size;
	size_t max_client_receive_size;
	char *nss_db_dir;
	char *cert_nickname;
	uint32_t heartbeat_interval_min;
	uint32_t heartbeat_interval_max;
	uint8_t dpd_enabled;
	char *lock_file;
	char *local_socket_file;
	int local_socket_backlog;
	size_t ipc_max_clients;
	size_t ipc_max_send_size;
	size_t ipc_max_receive_size;
	enum tlv_keep_active_partition_tie_breaker keep_active_partition_tie_breaker;
	double dpd_interval_coefficient;
};

extern int		qnetd_advanced_settings_init(struct qnetd_advanced_settings *settings);

extern int		qnetd_advanced_settings_set(struct qnetd_advanced_settings *settings,
    const char *option, const char *value);

extern void		qnetd_advanced_settings_destroy(struct qnetd_advanced_settings *settings);

#ifdef __cplusplus
}
#endif

#endif /* _QNETD_ADVANCED_SETTINGS_H_ */
