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

#ifndef _QDEVICE_MODEL_NET_H_
#define _QDEVICE_MODEL_NET_H_

#include "qdevice-instance.h"
#include "qdevice-model.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int	qdevice_model_net_init(struct qdevice_instance *instance);

extern int	qdevice_model_net_destroy(struct qdevice_instance *instance);

extern int	qdevice_model_net_pre_poll_loop(struct qdevice_instance *instance);

extern int	qdevice_model_net_post_poll_loop(struct qdevice_instance *instance,
    enum qdevice_model_post_poll_loop_exit_reason exit_reason);

extern int	qdevice_model_net_get_config_node_list_failed(struct qdevice_instance *instance);

extern int	qdevice_model_net_config_node_list_changed(struct qdevice_instance *instance,
    const struct node_list *nlist, int config_version_set, uint64_t config_version);

extern int	qdevice_model_net_votequorum_quorum_notify(struct qdevice_instance *instance,
    uint32_t quorate, uint32_t node_list_entries, votequorum_node_t node_list[]);

extern int	qdevice_model_net_votequorum_node_list_notify(struct qdevice_instance *instance,
    votequorum_ring_id_t votequorum_ring_id, uint32_t node_list_entries, uint32_t node_list[]);

extern int	qdevice_model_net_votequorum_node_list_heuristics_notify(
    struct qdevice_instance *instance,
    votequorum_ring_id_t votequorum_ring_id, uint32_t node_list_entries, uint32_t node_list[],
    enum qdevice_heuristics_exec_result heuristics_exec_result);

extern int 	qdevice_model_net_votequorum_expected_votes_notify(struct qdevice_instance *instance,
    uint32_t expected_votes);

extern int      qdevice_model_net_cmap_changed(struct qdevice_instance *instance,
    const struct qdevice_cmap_change_events *events);

extern int	qdevice_model_net_ipc_cmd_status(struct qdevice_instance *instance,
    struct dynar *outbuf, int verbose);

extern int	qdevice_model_net_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _QDEVICE_MODEL_NET_H_ */
