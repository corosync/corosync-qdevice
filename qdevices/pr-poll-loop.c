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

#include <sys/types.h>

#include <arpa/inet.h>
#include <sys/queue.h>

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "pr-poll-array.h"
#include "pr-poll-loop.h"

/*
 * Needed for creating nspr handle from unix fd
 */
#include <private/pprio.h>

/*
 * Helper functions declarations
 */
static PRInt16					 poll_events_to_pr_events(short events);

static short					 pr_events_to_poll_events(PRInt16 events);

static int					 pr_poll_loop_add_fd_int(struct pr_poll_loop *poll_loop,
    int fd, PRFileDesc *prfd,
    short events, pr_poll_loop_fd_set_events_cb_fn fd_set_events_cb,
    pr_poll_loop_prfd_set_events_cb_fn prfd_set_events_cb,
    pr_poll_loop_fd_read_cb_fn fd_read_cb, pr_poll_loop_prfd_read_cb_fn prfd_read_cb,
    pr_poll_loop_fd_write_cb_fn fd_write_cb, pr_poll_loop_prfd_write_cb_fn prfd_write_cb,
    pr_poll_loop_fd_err_cb_fn fd_err_cb, pr_poll_loop_prfd_err_cb_fn prfd_err_cb,
    void *user_data1, void *user_data2);

static int					 pr_poll_loop_del_fd_int(
    struct pr_poll_loop *poll_loop, int fd, PRFileDesc *prfd);

static struct pr_poll_loop_fd_entry		*pr_poll_loop_find_by_fd(
    const struct pr_poll_loop *poll_loop, int fd, PRFileDesc *prfd);

static struct pr_poll_loop_pre_poll_cb_entry	*pr_poll_loop_find_pre_poll_cb(
    const struct pr_poll_loop *poll_loop,
    pr_poll_loop_pre_poll_cb_fn pre_poll_cb);

static int				 prepare_poll_array(struct pr_poll_loop *poll_loop);

/*
 * Helper functions definitions
 */
static PRInt16
poll_events_to_pr_events(short events)
{
	PRInt16 res;

	res = 0;

	if (events & POLLIN) {
		res |= PR_POLL_READ;
	}

	if (events & POLLOUT) {
		res |= PR_POLL_WRITE;
	}

	if (events & POLLPRI) {
		res |= PR_POLL_EXCEPT;
	}

	return (res);
}

static short
pr_events_to_poll_events(PRInt16 events)
{
	short res;

	res = 0;

	if (events & PR_POLL_READ) {
		res |= POLLIN;
	}

	if (events & PR_POLL_WRITE) {
		res |= POLLOUT;
	}

	if (events & PR_POLL_ERR) {
		res |= POLLERR;
	}

	if (events & PR_POLL_NVAL) {
		res |= POLLNVAL;
	}

	if (events & PR_POLL_HUP) {
		res |= POLLHUP;
	}

	if (events & PR_POLL_EXCEPT) {
		res |= POLLPRI;
	}

	return (res);
}

static int
pr_poll_loop_add_fd_int(struct pr_poll_loop *poll_loop, int fd, PRFileDesc *prfd,
    short events, pr_poll_loop_fd_set_events_cb_fn fd_set_events_cb,
    pr_poll_loop_prfd_set_events_cb_fn prfd_set_events_cb,
    pr_poll_loop_fd_read_cb_fn fd_read_cb, pr_poll_loop_prfd_read_cb_fn prfd_read_cb,
    pr_poll_loop_fd_write_cb_fn fd_write_cb, pr_poll_loop_prfd_write_cb_fn prfd_write_cb,
    pr_poll_loop_fd_err_cb_fn fd_err_cb, pr_poll_loop_prfd_err_cb_fn prfd_err_cb,
    void *user_data1, void *user_data2)
{
	struct pr_poll_loop_fd_entry *new_entry;

	assert((prfd != NULL && fd == -1) || (fd != -1 && prfd == NULL));

	if ((events & ~(POLLIN|POLLOUT|POLLPRI)) != 0) {
		return (-1);
	}

	if (pr_poll_loop_find_by_fd(poll_loop, fd, prfd) != NULL) {
		return (-1);
	}

	new_entry = malloc(sizeof(*new_entry));
	if (new_entry == NULL) {
		return (-1);
	}

	memset(new_entry, 0, sizeof(*new_entry));

	new_entry->fd = fd;

	if (fd != -1) {
		new_entry->prfd = PR_CreateSocketPollFd(fd);
		if (new_entry->prfd == NULL) {
			free(new_entry);

			return (-1);
		}
	} else {
		new_entry->prfd = prfd;
	}

	new_entry->events = events;

	new_entry->fd_set_events_cb = fd_set_events_cb;
	new_entry->prfd_set_events_cb = prfd_set_events_cb;

	new_entry->fd_read_cb = fd_read_cb;
	new_entry->prfd_read_cb = prfd_read_cb;

	new_entry->fd_write_cb = fd_write_cb;
	new_entry->prfd_write_cb = prfd_write_cb;

	new_entry->fd_err_cb = fd_err_cb;
	new_entry->prfd_err_cb = prfd_err_cb;

	new_entry->user_data1 = user_data1;
	new_entry->user_data2 = user_data2;

	TAILQ_INSERT_TAIL(&poll_loop->fd_list, new_entry, entries);

	return (0);
}

static int
pr_poll_loop_del_fd_int(struct pr_poll_loop *poll_loop, int fd, PRFileDesc *prfd)
{
	struct pr_poll_loop_fd_entry *fd_entry;

	fd_entry = pr_poll_loop_find_by_fd(poll_loop, fd, prfd);
	if (fd_entry == NULL) {
		return (-1);
	}

	TAILQ_REMOVE(&poll_loop->fd_list, fd_entry, entries);

	if (fd_entry->fd != -1) {
		(void)PR_DestroySocketPollFd(fd_entry->prfd);
	}

	free(fd_entry);

	return (0);
}

static struct pr_poll_loop_fd_entry *
pr_poll_loop_find_by_fd(const struct pr_poll_loop *poll_loop, int fd, PRFileDesc *prfd)
{
	struct pr_poll_loop_fd_entry *fd_entry;

	assert((prfd != NULL && fd == -1) || (fd != -1 && prfd == NULL));

	TAILQ_FOREACH(fd_entry, &poll_loop->fd_list, entries) {
		if (fd != -1) {
			if (fd_entry->fd == fd) {
				return (fd_entry);
			}
		} else {
			if (fd_entry->prfd == prfd) {
				return (fd_entry);
			}
		}
	}

	return (NULL);
}

static struct pr_poll_loop_pre_poll_cb_entry *
pr_poll_loop_find_pre_poll_cb(const struct pr_poll_loop *poll_loop,
    pr_poll_loop_pre_poll_cb_fn pre_poll_cb)
{
	struct pr_poll_loop_pre_poll_cb_entry *pre_poll_cb_entry;

	TAILQ_FOREACH(pre_poll_cb_entry, &poll_loop->pre_poll_cb_list, entries) {
		if (pre_poll_cb_entry->pre_poll_cb == pre_poll_cb) {
			return (pre_poll_cb_entry);
		}
	}

	return (NULL);
}

static
int prepare_poll_array(struct pr_poll_loop *poll_loop)
{
	struct pr_poll_loop_fd_entry *fd_entry;
	struct pr_poll_loop_fd_entry *fd_entry_next;
	struct pr_poll_loop_pre_poll_cb_entry *pre_poll_cb_entry;
	struct pr_poll_loop_pre_poll_cb_entry *pre_poll_cb_entry_next;
	struct pr_poll_loop_fd_entry **user_data;
	short events;
	int res;
	PRPollDesc *poll_desc;
	struct pr_poll_array *poll_array;

	poll_array = &poll_loop->poll_array;

	pr_poll_array_clean(poll_array);

	/*
	 * Call pre poll callbacks
	 */
	pre_poll_cb_entry = TAILQ_FIRST(&poll_loop->pre_poll_cb_list);
	while (pre_poll_cb_entry != NULL) {
		pre_poll_cb_entry_next = TAILQ_NEXT(pre_poll_cb_entry, entries);

		if (pre_poll_cb_entry->pre_poll_cb != NULL) {
			res = pre_poll_cb_entry->pre_poll_cb(pre_poll_cb_entry->user_data1,
			    pre_poll_cb_entry->user_data2);
		} else {
			res = 0;
		}

		switch (res) {
		case 0:
			/*
			 * Continue
			 */
			break;
		case -1:
			/*
			 * return immediately
			 */
			return (-1);
			break;
		default:
			return (-2);
			break;
		}

		pre_poll_cb_entry = pre_poll_cb_entry_next;
	}

	/*
	 * Fill in poll_array
	 */
	fd_entry = TAILQ_FIRST(&poll_loop->fd_list);

	while (fd_entry != NULL) {
		fd_entry_next = TAILQ_NEXT(fd_entry, entries);

		events = fd_entry->events;

		if (fd_entry->fd_set_events_cb != NULL || fd_entry->prfd_set_events_cb != NULL) {
			if (fd_entry->fd_set_events_cb != NULL) {
				res = fd_entry->fd_set_events_cb(fd_entry->fd, &events,
				    fd_entry->user_data1, fd_entry->user_data2);
			} else {
				res = fd_entry->prfd_set_events_cb(fd_entry->prfd, &events,
				    fd_entry->user_data1, fd_entry->user_data2);
			}
		} else {
			/*
			 * Add entry
			 */
			res = 0;
		}

		if ((events & ~(POLLIN|POLLOUT|POLLPRI)) != 0) {
			return (-2);
		}

		if (res == 0 && events == 0) {
			/*
			 * Empty events -> do not add entry
			 */
			res = -1;
		}

		switch (res) {
		case 0:
			/*
			 * Add entry
			 */
			if (pr_poll_array_add(poll_array, &poll_desc, (void **)&user_data) < 0) {
				return (-1);
			}

			poll_desc->fd = fd_entry->prfd;
			poll_desc->in_flags = poll_events_to_pr_events(events);

			*user_data = fd_entry;
			break;
		case -1:
			/*
			 * Do not add entry
			 */
			break;
		case -2:
			/*
			 * -2 = return immediately
			 */
			return (-1);
			break;
		default:
			return (-2);
			break;
		}

		fd_entry = fd_entry_next;
	}

	pr_poll_array_gc(poll_array);

	return (0);
}

/*
 * Exported functions
 */
void
pr_poll_loop_init(struct pr_poll_loop *poll_loop)
{

	memset(poll_loop, 0, sizeof(*poll_loop));

	TAILQ_INIT(&(poll_loop->fd_list));
	TAILQ_INIT(&(poll_loop->pre_poll_cb_list));

	pr_poll_array_init(&poll_loop->poll_array, sizeof(struct pr_poll_loop_fd_entry *));
	timer_list_init(&poll_loop->tlist);
}

int
pr_poll_loop_del_fd(struct pr_poll_loop *poll_loop, int fd)
{

	return (pr_poll_loop_del_fd_int(poll_loop, fd, NULL));
}

int
pr_poll_loop_del_pre_poll_cb(struct pr_poll_loop *poll_loop,
    pr_poll_loop_pre_poll_cb_fn pre_poll_cb)
{
	struct pr_poll_loop_pre_poll_cb_entry *pre_poll_cb_entry;

	pre_poll_cb_entry = pr_poll_loop_find_pre_poll_cb(poll_loop, pre_poll_cb);
	if (pre_poll_cb_entry == NULL) {
		return (-1);
	}

	TAILQ_REMOVE(&poll_loop->pre_poll_cb_list, pre_poll_cb_entry, entries);

	free(pre_poll_cb_entry);

	return (0);
}

int
pr_poll_loop_del_prfd(struct pr_poll_loop *poll_loop, PRFileDesc *prfd)
{

	return (pr_poll_loop_del_fd_int(poll_loop, -1, prfd));
}

int
pr_poll_loop_destroy(struct pr_poll_loop *poll_loop)
{
	struct pr_poll_loop_fd_entry *fd_entry;
	struct pr_poll_loop_fd_entry *fd_entry_next;
	struct pr_poll_loop_pre_poll_cb_entry *pre_poll_cb_entry;
	struct pr_poll_loop_pre_poll_cb_entry *pre_poll_cb_entry_next;

	fd_entry = TAILQ_FIRST(&poll_loop->fd_list);

	while (fd_entry != NULL) {
		fd_entry_next = TAILQ_NEXT(fd_entry, entries);

		if (fd_entry->fd != -1) {
			(void)PR_DestroySocketPollFd(fd_entry->prfd);
		}

		free(fd_entry);

		fd_entry = fd_entry_next;
	}

	TAILQ_INIT(&(poll_loop->fd_list));

	pre_poll_cb_entry = TAILQ_FIRST(&poll_loop->pre_poll_cb_list);

	while (pre_poll_cb_entry != NULL) {
		pre_poll_cb_entry_next = TAILQ_NEXT(pre_poll_cb_entry, entries);

		free(pre_poll_cb_entry);

		pre_poll_cb_entry = pre_poll_cb_entry_next;
	}

	TAILQ_INIT(&(poll_loop->pre_poll_cb_list));

	pr_poll_array_destroy(&poll_loop->poll_array);
	timer_list_free(&poll_loop->tlist);

	return (0);
}

int
pr_poll_loop_add_fd(struct pr_poll_loop *poll_loop, int fd,
    short events, pr_poll_loop_fd_set_events_cb_fn fd_set_events_cb,
    pr_poll_loop_fd_read_cb_fn fd_read_cb,
    pr_poll_loop_fd_write_cb_fn fd_write_cb,
    pr_poll_loop_fd_err_cb_fn fd_err_cb,
    void *user_data1, void *user_data2)
{

	return (pr_poll_loop_add_fd_int(poll_loop, fd, NULL, events,
	    fd_set_events_cb, NULL, fd_read_cb, NULL, fd_write_cb, NULL,
	    fd_err_cb, NULL,
	    user_data1, user_data2));
}

int
pr_poll_loop_add_pre_poll_cb(struct pr_poll_loop *poll_loop,
    pr_poll_loop_pre_poll_cb_fn pre_poll_cb,
    void *user_data1, void *user_data2)
{
	struct pr_poll_loop_pre_poll_cb_entry *new_entry;

	if (pr_poll_loop_find_pre_poll_cb(poll_loop, pre_poll_cb) != NULL) {
		return (-1);
	}

	new_entry = malloc(sizeof(*new_entry));
	if (new_entry == NULL) {
		return (-1);
	}

	memset(new_entry, 0, sizeof(*new_entry));

	new_entry->pre_poll_cb = pre_poll_cb;

	new_entry->user_data1 = user_data1;
	new_entry->user_data2 = user_data2;

	TAILQ_INSERT_TAIL(&poll_loop->pre_poll_cb_list, new_entry, entries);

	return (0);
}

int
pr_poll_loop_add_prfd(struct pr_poll_loop *poll_loop, PRFileDesc *prfd,
    short events, pr_poll_loop_prfd_set_events_cb_fn prfd_set_events_cb,
    pr_poll_loop_prfd_read_cb_fn prfd_read_cb,
    pr_poll_loop_prfd_read_cb_fn prfd_write_cb,
    pr_poll_loop_prfd_err_cb_fn prfd_err_cb,
    void *user_data1, void *user_data2)
{

	return (pr_poll_loop_add_fd_int(poll_loop, -1, prfd, events,
	    NULL, prfd_set_events_cb, NULL, prfd_read_cb, NULL, prfd_write_cb,
	    NULL, prfd_err_cb,
	    user_data1, user_data2));
}

int
pr_poll_loop_exec(struct pr_poll_loop *poll_loop)
{
	PRInt32 poll_res;
	struct pr_poll_loop_fd_entry *fd_entry;
	struct pr_poll_loop_fd_entry **user_data;
	ssize_t i;
	int cb_res;
	static PRPollDesc *pfds;
	int res;

	if ((res = prepare_poll_array(poll_loop)) != 0) {
		return (res);
	}

	pfds = poll_loop->poll_array.array;

	if ((poll_res = PR_Poll(pfds, pr_poll_array_size(&poll_loop->poll_array),
	    timer_list_time_to_expire(&poll_loop->tlist))) > 0) {
		for (i = 0; i < pr_poll_array_size(&poll_loop->poll_array); i++) {
			user_data = pr_poll_array_get_user_data(&poll_loop->poll_array, i);
			fd_entry = *user_data;

			if (pfds[i].out_flags & PR_POLL_READ &&
			    (fd_entry->fd_read_cb != NULL || fd_entry->prfd_read_cb != NULL)) {
				if (fd_entry->fd_read_cb) {
					cb_res = fd_entry->fd_read_cb(fd_entry->fd,
					    fd_entry->user_data1, fd_entry->user_data2);
				} else {
					cb_res = fd_entry->prfd_read_cb(fd_entry->prfd, &pfds[i],
					    fd_entry->user_data1, fd_entry->user_data2);
				}

				if (cb_res != 0) {
					return (-1);
				}
			}

			if (pfds[i].out_flags & PR_POLL_WRITE &&
			    (fd_entry->fd_write_cb != NULL || fd_entry->prfd_write_cb != NULL)) {
				if (fd_entry->fd_write_cb) {
					cb_res = fd_entry->fd_write_cb(fd_entry->fd,
					    fd_entry->user_data1, fd_entry->user_data2);
				} else {
					cb_res = fd_entry->prfd_write_cb(fd_entry->prfd, &pfds[i],
					    fd_entry->user_data1, fd_entry->user_data2);
				}

				if (cb_res != 0) {
					return (-1);
				}
			}

			if ((pfds[i].out_flags & (PR_POLL_ERR|PR_POLL_NVAL|PR_POLL_HUP|PR_POLL_EXCEPT)) &&
			    !(pfds[i].out_flags & (PR_POLL_READ|PR_POLL_WRITE)) &&
			    (fd_entry->fd_err_cb != NULL || fd_entry->prfd_err_cb != NULL)) {
					if (fd_entry->fd_err_cb) {
						cb_res = fd_entry->fd_err_cb(fd_entry->fd,
						    pr_events_to_poll_events(pfds[i].out_flags),
						    fd_entry->user_data1, fd_entry->user_data2);
					} else {
						cb_res = fd_entry->prfd_err_cb(fd_entry->prfd,
						    pr_events_to_poll_events(pfds[i].out_flags),
						    &pfds[i],
						    fd_entry->user_data1, fd_entry->user_data2);
					}

					if (cb_res != 0) {
						return (-1);
					}
			}
		}
	}

	if (poll_res == -1) {
		return (-3);
	}

	timer_list_expire(&poll_loop->tlist);

	return (0);
}

struct timer_list *
pr_poll_loop_get_timer_list(struct pr_poll_loop *poll_loop)
{

	return (&poll_loop->tlist);
}
