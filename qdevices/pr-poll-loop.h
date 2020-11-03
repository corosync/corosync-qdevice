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

#ifndef _PR_POLL_LOOP_H_
#define _PR_POLL_LOOP_H_

#include <sys/types.h>

#include <sys/queue.h>
#include <inttypes.h>

#include <nspr.h>
#include <poll.h>

#include "pr-poll-array.h"
#include "timer-list.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Return code: 0 - Ok, -1 - Don't add client, -2 - Return poll error
 */
typedef int (*pr_poll_loop_fd_set_events_cb_fn)(int fd, short *events,
    void *user_data1, void *user_data2);
typedef int (*pr_poll_loop_prfd_set_events_cb_fn)(PRFileDesc *prfd, short *events,
    void *user_data1, void *user_data2);

/*
 * Return code: 0 - Ok, -1 - Return error
 */
typedef int (*pr_poll_loop_fd_read_cb_fn)(int fd, void *user_data1, void *user_data2);
typedef int (*pr_poll_loop_prfd_read_cb_fn)(PRFileDesc *prfd, const PRPollDesc *pd,
    void *user_data1, void *user_data2);

/*
 * Return code: 0 - Ok, -1 - Return error
 */
typedef int (*pr_poll_loop_fd_write_cb_fn)(int fd, void *user_data1, void *user_data2);
typedef int (*pr_poll_loop_prfd_write_cb_fn)(PRFileDesc *prfd,
    const PRPollDesc *pd, void *user_data1, void *user_data2);

/*
 * Return code: 0 - Ok, -1 - Return error
 */
typedef int (*pr_poll_loop_fd_err_cb_fn)(int fd, short revents, void *user_data1, void *user_data2);
typedef int (*pr_poll_loop_prfd_err_cb_fn)(PRFileDesc *prfd, short revents, const PRPollDesc *pd,
    void *user_data1, void *user_data2);

/*
 * Return code: 0 - Ok, -1 - Return error
 */
typedef int (*pr_poll_loop_pre_poll_cb_fn)(void *user_data1, void *user_data2);

struct pr_poll_loop_fd_entry {
	int fd;
	PRFileDesc *prfd;
	short events;
	pr_poll_loop_fd_set_events_cb_fn fd_set_events_cb;
	pr_poll_loop_prfd_set_events_cb_fn prfd_set_events_cb;
	pr_poll_loop_fd_read_cb_fn fd_read_cb;
	pr_poll_loop_prfd_read_cb_fn prfd_read_cb;
	pr_poll_loop_fd_write_cb_fn fd_write_cb;
	pr_poll_loop_prfd_write_cb_fn prfd_write_cb;
	pr_poll_loop_fd_err_cb_fn fd_err_cb;
	pr_poll_loop_prfd_err_cb_fn prfd_err_cb;
	void *user_data1;
	void *user_data2;
	TAILQ_ENTRY(pr_poll_loop_fd_entry) entries;
};

struct pr_poll_loop_pre_poll_cb_entry {
	pr_poll_loop_pre_poll_cb_fn pre_poll_cb;
	void *user_data1;
	void *user_data2;
	TAILQ_ENTRY(pr_poll_loop_pre_poll_cb_entry) entries;
};

struct pr_poll_loop {
	TAILQ_HEAD(, pr_poll_loop_fd_entry) fd_list;
	TAILQ_HEAD(, pr_poll_loop_pre_poll_cb_entry) pre_poll_cb_list;
	struct timer_list tlist;
	struct pr_poll_array poll_array;
};

extern void			 pr_poll_loop_init(struct pr_poll_loop *poll_loop);

extern int			 pr_poll_loop_add_fd(struct pr_poll_loop *poll_loop, int fd,
    short events, pr_poll_loop_fd_set_events_cb_fn fd_set_events_cb,
    pr_poll_loop_fd_read_cb_fn fd_read_cb,
    pr_poll_loop_fd_read_cb_fn fd_write_cb,
    pr_poll_loop_fd_err_cb_fn fd_err_cb,
    void *user_data1, void *user_data2);

extern int			 pr_poll_loop_add_pre_poll_cb(struct pr_poll_loop *poll_loop,
    pr_poll_loop_pre_poll_cb_fn pre_poll_cb,
    void *user_data1, void *user_data2);

extern int			 pr_poll_loop_add_prfd(struct pr_poll_loop *poll_loop,
    PRFileDesc *prfd, short events,
    pr_poll_loop_prfd_set_events_cb_fn prfd_set_events_cb,
    pr_poll_loop_prfd_read_cb_fn fd_read_cb,
    pr_poll_loop_prfd_read_cb_fn fd_write_cb,
    pr_poll_loop_prfd_err_cb_fn fd_err_cb,
    void *user_data1, void *user_data2);

extern int			 pr_poll_loop_del_fd(struct pr_poll_loop *poll_loop, int fd);

extern int			pr_poll_loop_del_pre_poll_cb(struct pr_poll_loop *poll_loop,
    pr_poll_loop_pre_poll_cb_fn pre_poll_cb);

extern int			 pr_poll_loop_del_prfd(struct pr_poll_loop *poll_loop,
    PRFileDesc *prfd);

/*
 * Return codes:
 *  0 - No error and all callbacks returned 0
 * -1 - Either set_events returned -2 or some other callbacks returned -1
 * -2 - Other error (events is not POLLIN|POLLOUT, or set_events cb was not 0, -1 or -2)
 * -3 - PR_Poll returned -1
 */
extern int			 pr_poll_loop_exec(struct pr_poll_loop *poll_loop);

extern int			 pr_poll_loop_destroy(struct pr_poll_loop *poll_loop);

extern struct timer_list	*pr_poll_loop_get_timer_list(struct pr_poll_loop *poll_loop);

#ifdef __cplusplus
}
#endif

#endif /* _PR_POLL_LOOP_H_ */
