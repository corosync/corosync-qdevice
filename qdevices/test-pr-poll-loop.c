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

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "pr-poll-loop.h"

/*
 * Needed for creating nspr handle from unix fd
 */
#include <private/pprio.h>

#define BUF_SIZE		256

#define TIMER_TIMEOUT		10000
#define TIMER_TEST_TIMEOUT	100
/*
 * Must be smaller than BUF_SIZE
 */
#define READ_STR		"test"

static int fd_set_events_cb1_return_called = -1;
static int fd_set_events_cb2_return_called = -1;
static int fd_read_cb1_called = -1;
static int fd_read_cb2_called = -1;
static int fd_write_cb1_called = -1;
static int fd_err_cb1_called = -1;
static int timeout_cb_called = -1;

static int prfd_set_events_cb1_return_called = -1;
static int prfd_set_events_cb2_return_called = -1;
static int prfd_read_cb1_called = -1;
static int prfd_read_cb2_called = -1;
static int prfd_write_cb1_called = -1;
static int prfd_err_cb1_called = -1;

static int test_complex_state = -1;
static int test_complex_set_events_pipe1_read_called = -1;
static int test_complex_read_pipe1_read_called = -1;
static int test_complex_set_events_pipe1_write_called = -1;
static int test_complex_write_pipe1_write_called = -1;
static int test_complex_set_events_pipe2_read_called = -1;
static int test_complex_read_pipe2_read_called = -1;
static int test_complex_set_events_pipe2_write_called = -1;
static int test_complex_write_pipe2_write_called = -1;
static int test_complex_read_pipe1_fd = -1;

static int pre_poll_cb1_called = -1;
static int pre_poll_cb2_called = -1;
static int pre_poll_cb_return_called = -1;

static int
timeout_cb(void *data1, void *data2)
{

	timeout_cb_called = 1;

	return (0);
}

static int
fd_set_events_cb1_return(int fd, short *events, void *user_data1, void *user_data2)
{

	fd_set_events_cb1_return_called++;

	assert(user_data1 == &fd_set_events_cb1_return_called);
	assert(user_data2 == NULL);

	return (-2);
}

static int
fd_set_events_cb2_return(int fd, short *events, void *user_data1, void *user_data2)
{

	fd_set_events_cb2_return_called++;

	assert(user_data1 == NULL);
	assert(user_data2 == &fd_set_events_cb2_return_called);

	return (-2);
}

static int
fd_read_cb1(int fd, void *user_data1, void *user_data2)
{
	char buf[BUF_SIZE];

	assert(user_data1 == &fd_read_cb1_called);
	assert(user_data2 == fd_read_cb1);

	fd_read_cb1_called++;

	assert(read(fd, buf, BUF_SIZE) == strlen(READ_STR) + 1);
	assert(memcmp(buf, READ_STR, strlen(READ_STR) + 1) == 0);

	return (0);
}

static int
fd_read_cb2(int fd, void *user_data1, void *user_data2)
{
	char buf[BUF_SIZE];

	assert(user_data1 == &fd_read_cb2_called);
	assert(user_data2 == fd_read_cb2);

	fd_read_cb2_called++;

	assert(read(fd, buf, BUF_SIZE) == strlen(READ_STR) + 1);
	assert(memcmp(buf, READ_STR, strlen(READ_STR) + 1) == 0);

	return (-1);
}

static int
fd_write_cb1(int fd, void *user_data1, void *user_data2)
{

	assert(user_data1 == &fd_write_cb1_called);
	assert(user_data2 == fd_write_cb1);

	fd_write_cb1_called++;

	return (0);
}

static int
fd_err_cb1(int fd, short revents, void *user_data1, void *user_data2)
{

	assert(user_data1 == &fd_err_cb1_called);
	assert(user_data2 == fd_err_cb1);

	fd_err_cb1_called++;

	return (0);
}

static int
prfd_set_events_cb1_return(PRFileDesc *prfd, short *events, void *user_data1, void *user_data2)
{

	prfd_set_events_cb1_return_called++;

	assert(user_data1 == &prfd_set_events_cb1_return_called);
	assert(user_data2 == NULL);

	return (-2);
}

static int
prfd_set_events_cb2_return(PRFileDesc *prfd, short *events, void *user_data1, void *user_data2)
{

	prfd_set_events_cb2_return_called++;

	assert(user_data1 == NULL);
	assert(user_data2 == &prfd_set_events_cb2_return_called);

	return (-2);
}

static int
prfd_read_cb1(PRFileDesc *prfd, const PRPollDesc *pd, void *user_data1, void *user_data2)
{
	char buf[BUF_SIZE];

	assert(user_data1 == &prfd_read_cb1_called);
	assert(user_data2 == prfd_read_cb1);

	prfd_read_cb1_called++;

	assert(PR_Read(prfd, buf, BUF_SIZE) == strlen(READ_STR) + 1);
	assert(memcmp(buf, READ_STR, strlen(READ_STR) + 1) == 0);

	return (0);
}

static int
prfd_read_cb2(PRFileDesc *prfd, const PRPollDesc *pd, void *user_data1, void *user_data2)
{
	char buf[BUF_SIZE];

	assert(user_data1 == &prfd_read_cb2_called);
	assert(user_data2 == prfd_read_cb2);

	prfd_read_cb2_called++;

	assert(PR_Read(prfd, buf, BUF_SIZE) == strlen(READ_STR) + 1);
	assert(memcmp(buf, READ_STR, strlen(READ_STR) + 1) == 0);

	return (-1);
}

static int
prfd_write_cb1(PRFileDesc *prfd, const PRPollDesc *pd, void *user_data1, void *user_data2)
{

	assert(user_data1 == &prfd_write_cb1_called);
	assert(user_data2 == prfd_write_cb1);

	prfd_write_cb1_called++;

	return (0);
}

static int
prfd_err_cb1(PRFileDesc *prfd, short revents, const PRPollDesc *pd, void *user_data1, void *user_data2)
{

	assert(user_data1 == &prfd_err_cb1_called);
	assert(user_data2 == prfd_err_cb1);

	prfd_err_cb1_called++;

	return (0);
}

static int
test_complex_set_events_pipe1_read_cb(PRFileDesc *prfd, short *events, void *user_data1, void *user_data2)
{

	test_complex_set_events_pipe1_read_called++;

	assert(user_data1 == &test_complex_set_events_pipe1_read_called);
	assert(user_data2 == test_complex_set_events_pipe1_read_cb);
	assert(*events == 0);

	if (test_complex_state == 2) {
		*events = POLLIN;
	}

	return (0);
}

static int
test_complex_read_pipe1_read_cb(PRFileDesc *prfd, const PRPollDesc *pd, void *user_data1, void *user_data2)
{
	char buf[BUF_SIZE];

	assert(user_data1 == &test_complex_set_events_pipe1_read_called);
	assert(user_data2 == test_complex_set_events_pipe1_read_cb);

	test_complex_read_pipe1_read_called++;

	/*
	 * prfd for this case is just a wrapper, we need to use real fd
	 */
	assert(read(test_complex_read_pipe1_fd, buf, BUF_SIZE) == strlen(READ_STR) + 1);
	assert(memcmp(buf, READ_STR, strlen(READ_STR) + 1) == 0);

	return (0);
}

static int
test_complex_write_pipe1_read_cb(PRFileDesc *prfd, const PRPollDesc *pd, void *user_data1, void *user_data2)
{

	assert(0);

	return (-1);
}

static int
test_complex_set_events_pipe1_write_cb(int fd, short *events, void *user_data1, void *user_data2)
{

	test_complex_set_events_pipe1_write_called++;

	assert(user_data1 == &test_complex_set_events_pipe1_write_called);
	assert(user_data2 == test_complex_set_events_pipe1_write_cb);
	assert(*events == 0);

	if (test_complex_state == 1) {
		*events = POLLOUT;
	}

	return (0);
}

static int
test_complex_read_pipe1_write_cb(int fd, void *user_data1, void *user_data2)
{

	assert(0);

	return (-1);
}

static int
test_complex_write_pipe1_write_cb(int fd, void *user_data1, void *user_data2)
{

	assert(user_data1 == &test_complex_set_events_pipe1_write_called);
	assert(user_data2 == test_complex_set_events_pipe1_write_cb);
	test_complex_write_pipe1_write_called++;

	return (0);
}

static int
test_complex_set_events_pipe2_read_cb(int fd, short *events, void *user_data1, void *user_data2)
{

	test_complex_set_events_pipe2_read_called++;

	assert(user_data1 == &test_complex_set_events_pipe2_read_called);
	assert(user_data2 == test_complex_set_events_pipe2_read_cb);
	assert(*events == POLLIN);

	return (0);
}

static int
test_complex_read_pipe2_read_cb(int fd, void *user_data1, void *user_data2)
{
	char buf[BUF_SIZE];

	assert(user_data1 == &test_complex_set_events_pipe2_read_called);
	assert(user_data2 == test_complex_set_events_pipe2_read_cb);

	test_complex_read_pipe2_read_called++;

	assert(read(fd, buf, BUF_SIZE) == strlen(READ_STR) + 1);
	assert(memcmp(buf, READ_STR, strlen(READ_STR) + 1) == 0);

	return (0);
}

static int
test_complex_write_pipe2_read_cb(int fd, void *user_data1, void *user_data2)
{

	assert(0);

	return (-1);
}

static int
test_complex_set_events_pipe2_write_cb(PRFileDesc *prfd, short *events, void *user_data1, void *user_data2)
{

	test_complex_set_events_pipe2_write_called++;

	assert(user_data1 == &test_complex_set_events_pipe2_write_called);
	assert(user_data2 == test_complex_set_events_pipe2_write_cb);
	assert(*events == POLLOUT);

	return (0);
}

static int
test_complex_read_pipe2_write_cb(PRFileDesc *prfd, const PRPollDesc *pd, void *user_data1, void *user_data2)
{

	assert(0);

	return (-1);
}

static int
test_complex_write_pipe2_write_cb(PRFileDesc *prfd, const PRPollDesc *pd, void *user_data1, void *user_data2)
{

	assert(user_data1 == &test_complex_set_events_pipe2_write_called);
	assert(user_data2 == test_complex_set_events_pipe2_write_cb);
	test_complex_write_pipe2_write_called++;

	return (0);
}

static int
test_pre_poll_cb1(void *user_data1, void *user_data2)
{

	assert(user_data1 == &pre_poll_cb1_called);
	assert(user_data2 == test_pre_poll_cb1);
	pre_poll_cb1_called++;

	return (0);
}

static int
test_pre_poll_cb2(void *user_data1, void *user_data2)
{

	assert(user_data1 == &pre_poll_cb2_called);
	assert(user_data2 == test_pre_poll_cb2);
	pre_poll_cb2_called++;

	return (0);
}

static int
test_pre_poll_cb_return(void *user_data1, void *user_data2)
{

	assert(user_data1 == &pre_poll_cb_return_called);
	assert(user_data2 == test_pre_poll_cb_return);
	pre_poll_cb_return_called++;

	return (-1);
}

static void
init_global_vars(void)
{
	fd_set_events_cb1_return_called = -1;
	fd_set_events_cb2_return_called = -1;
	fd_read_cb1_called = -1;
	fd_read_cb2_called = -1;
	fd_write_cb1_called = -1;
	fd_err_cb1_called = -1;
	timeout_cb_called = -1;

	prfd_set_events_cb1_return_called = -1;
	prfd_set_events_cb2_return_called = -1;
	prfd_read_cb1_called = -1;
	prfd_read_cb2_called = -1;
	prfd_write_cb1_called = -1;
	prfd_err_cb1_called = -1;

	test_complex_set_events_pipe1_read_called = -1;
	test_complex_read_pipe1_read_called = -1;
	test_complex_set_events_pipe1_write_called = -1;
	test_complex_write_pipe1_write_called = -1;
	test_complex_set_events_pipe2_read_called = -1;
	test_complex_read_pipe2_read_called = -1;
	test_complex_set_events_pipe2_write_called = -1;
	test_complex_write_pipe2_write_called = -1;
	test_complex_read_pipe1_fd = -1;
}

static void
test_fd_basics(struct pr_poll_loop *poll_loop)
{
	int pipe_fd1[2];
	struct timer_list_entry *timeout_timer;

	init_global_vars();

	assert(pipe(pipe_fd1) == 0);

	/*
	 * Add POLLNVAL -> failure
	 */
	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd1[0], POLLNVAL, NULL, NULL, NULL, NULL,
	    NULL, NULL) == -1);
	/*
	 * Del non-existing fdL -> failure
	 */
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[0]) == -1);

	/*
	 * Add and delete fd twice
	 */
	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd1[0], 0, NULL, NULL, NULL, NULL,
	    NULL, NULL) == 0);
	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd1[0], 0, NULL, NULL, NULL, NULL,
	    NULL, NULL) == -1);
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[0]) == 0);
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[0]) == -1);

	/*
	 * Test timeout timer
	 * with empty list
	 */
	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TEST_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	timeout_cb_called = 0;
	assert(pr_poll_loop_exec(poll_loop) == 0);
	assert(timeout_cb_called == 1);

	/*
	 * Test user_data passing
	 */
	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd1[0], POLLIN, fd_set_events_cb1_return,
	    NULL, NULL, NULL, (void *)&fd_set_events_cb1_return_called, NULL) == 0);

	fd_set_events_cb1_return_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == -1);

	assert(fd_set_events_cb1_return_called == 1);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Remove entry and try with zero events and -2 return callback
	 */
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[0]) == 0);
	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd1[0], 0, fd_set_events_cb1_return,
	    NULL, NULL, NULL, (void *)&fd_set_events_cb1_return_called, NULL) == 0);

	fd_set_events_cb1_return_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == -1);

	assert(fd_set_events_cb1_return_called == 1);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Remove entry and try different cb
	 */
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[0]) == 0);
	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd1[1], POLLOUT, fd_set_events_cb2_return,
	    NULL, NULL, NULL, NULL, (void *)&fd_set_events_cb2_return_called) == 0);

	fd_set_events_cb1_return_called = 0;
	fd_set_events_cb2_return_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == -1);

	assert(fd_set_events_cb1_return_called == 0);
	assert(fd_set_events_cb2_return_called == 1);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Delete entry and try timeout again
	 */
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[0]) == -1);
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[1]) == 0);

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TEST_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	timeout_cb_called = 0;
	assert(pr_poll_loop_exec(poll_loop) == 0);
	assert(timeout_cb_called == 1);

	/*
	 * Try reading
	 */
	assert(write(pipe_fd1[1], READ_STR, strlen(READ_STR) + 1) == strlen(READ_STR) + 1);

	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd1[0], POLLIN, NULL,
	    fd_read_cb1, NULL, NULL,
	    &fd_read_cb1_called, fd_read_cb1) == 0);

	fd_read_cb1_called = 0;
	timeout_cb_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(fd_read_cb1_called == 1);
	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Try timeout with valid entry
	 */
	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TEST_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	timeout_cb_called = 0;
	fd_read_cb1_called = 0;

	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(timeout_cb_called == 1);
	assert(fd_read_cb1_called == 0);

	/*
	 * Try reading where cb returns err
	 */
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[0]) == 0);
	assert(write(pipe_fd1[1], READ_STR, strlen(READ_STR) + 1) == strlen(READ_STR) + 1);

	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd1[0], POLLIN, NULL,
	    fd_read_cb2, NULL, NULL,
	    &fd_read_cb2_called, fd_read_cb2) == 0);

	fd_read_cb1_called = 0;
	fd_read_cb2_called = 0;
	timeout_cb_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == -1);

	assert(fd_read_cb1_called == 0);
	assert(fd_read_cb2_called == 1);
	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Try writing
	 */
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[0]) == 0);

	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd1[1], POLLOUT, NULL,
	    NULL, fd_write_cb1, NULL,
	    &fd_write_cb1_called, fd_write_cb1) == 0);

	fd_write_cb1_called = 0;
	timeout_cb_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(fd_write_cb1_called == 1);
	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Try err cb
	 */
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[1]) == 0);

	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd1[0], POLLIN, NULL,
	    NULL, NULL, fd_err_cb1,
	    &fd_err_cb1_called, fd_err_cb1) == 0);

	assert(close(pipe_fd1[0]) == 0);
	assert(close(pipe_fd1[1]) == 0);
	fd_err_cb1_called = 0;
	timeout_cb_called = 0;
	fd_write_cb1_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(fd_err_cb1_called == 1);
	assert(fd_write_cb1_called == 0);
	assert(timeout_cb_called == 0);
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[0]) == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);
}

static void
test_prfd_basics(struct pr_poll_loop *poll_loop)
{
	PRFileDesc *read_pipe;
	PRFileDesc *write_pipe;
	struct timer_list_entry *timeout_timer;
	int pipe_fd1[2];

	init_global_vars();

	assert(PR_CreatePipe(&read_pipe, &write_pipe) == PR_SUCCESS);

	/*
	 * Add POLLNVAL -> failure
	 */
	assert(pr_poll_loop_add_prfd(poll_loop, read_pipe, POLLNVAL, NULL, NULL, NULL, NULL,
	    NULL, NULL) == -1);
	/*
	 * Del non-existing fdL -> failure
	 */
	assert(pr_poll_loop_del_prfd(poll_loop, read_pipe) == -1);

	/*
	 * Add and delete fd twice
	 */
	assert(pr_poll_loop_add_prfd(poll_loop, read_pipe, 0, NULL, NULL, NULL, NULL,
	    NULL, NULL) == 0);
	assert(pr_poll_loop_add_prfd(poll_loop, read_pipe, 0, NULL, NULL, NULL, NULL,
	    NULL, NULL) == -1);
	assert(pr_poll_loop_del_prfd(poll_loop, read_pipe) == 0);
	assert(pr_poll_loop_del_prfd(poll_loop, read_pipe) == -1);

	/*
	 * Test user_data passing
	 */
	assert(pr_poll_loop_add_prfd(poll_loop, read_pipe, POLLIN, prfd_set_events_cb1_return,
	    NULL, NULL, NULL, (void *)&prfd_set_events_cb1_return_called, NULL) == 0);

	prfd_set_events_cb1_return_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == -1);

	assert(prfd_set_events_cb1_return_called == 1);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Remove entry and try different cb
	 */
	assert(pr_poll_loop_del_prfd(poll_loop, read_pipe) == 0);
	assert(pr_poll_loop_add_prfd(poll_loop, write_pipe, POLLOUT, prfd_set_events_cb2_return,
	    NULL, NULL, NULL, NULL, (void *)&prfd_set_events_cb2_return_called) == 0);

	prfd_set_events_cb1_return_called = 0;
	prfd_set_events_cb2_return_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == -1);

	assert(prfd_set_events_cb1_return_called == 0);
	assert(prfd_set_events_cb2_return_called == 1);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Delete entry and try timeout again
	 */
	assert(pr_poll_loop_del_prfd(poll_loop, read_pipe) == -1);
	assert(pr_poll_loop_del_prfd(poll_loop, write_pipe) == 0);

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TEST_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	timeout_cb_called = 0;
	assert(pr_poll_loop_exec(poll_loop) == 0);
	assert(timeout_cb_called == 1);

	/*
	 * Try reading
	 */
	assert(PR_Write(write_pipe, READ_STR, strlen(READ_STR) + 1) == strlen(READ_STR) + 1);

	assert(pr_poll_loop_add_prfd(poll_loop, read_pipe, POLLIN, NULL,
	    prfd_read_cb1, NULL, NULL,
	    &prfd_read_cb1_called, prfd_read_cb1) == 0);

	prfd_read_cb1_called = 0;
	timeout_cb_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(prfd_read_cb1_called == 1);
	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Try timeout with valid entry
	 */
	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TEST_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	timeout_cb_called = 0;
	prfd_read_cb1_called = 0;

	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(timeout_cb_called == 1);
	assert(prfd_read_cb1_called == 0);

	/*
	 * Try reading where cb returns err
	 */
	assert(pr_poll_loop_del_prfd(poll_loop, read_pipe) == 0);
	assert(PR_Write(write_pipe, READ_STR, strlen(READ_STR) + 1) == strlen(READ_STR) + 1);

	assert(pr_poll_loop_add_prfd(poll_loop, read_pipe, POLLIN, NULL,
	    prfd_read_cb2, NULL, NULL,
	    &prfd_read_cb2_called, prfd_read_cb2) == 0);

	prfd_read_cb1_called = 0;
	prfd_read_cb2_called = 0;
	timeout_cb_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == -1);

	assert(prfd_read_cb1_called == 0);
	assert(prfd_read_cb2_called == 1);
	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Try writing
	 */
	assert(pr_poll_loop_del_prfd(poll_loop, read_pipe) == 0);

	assert(pr_poll_loop_add_prfd(poll_loop, write_pipe, POLLOUT, NULL,
	    NULL, prfd_write_cb1, NULL,
	    &prfd_write_cb1_called, prfd_write_cb1) == 0);

	prfd_write_cb1_called = 0;
	timeout_cb_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(prfd_write_cb1_called == 1);
	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	assert(PR_Close(read_pipe) == 0);
	assert(PR_Close(write_pipe) == 0);

	/*
	 * Try err cb
	 */
	assert(pr_poll_loop_del_prfd(poll_loop, write_pipe) == 0);

	/*
	 * Must use native pipe, because PR_Close deallocate PRFileDesc completelly
	 */
	assert(pipe(pipe_fd1) == 0);
	read_pipe = PR_CreateSocketPollFd(pipe_fd1[0]);
	assert(read_pipe != NULL);

	assert(close(pipe_fd1[0]) == 0);
	assert(close(pipe_fd1[1]) == 0);

	assert(pr_poll_loop_add_prfd(poll_loop, read_pipe, POLLIN, NULL,
	    NULL, NULL, prfd_err_cb1,
	    &prfd_err_cb1_called, prfd_err_cb1) == 0);

	prfd_err_cb1_called = 0;
	timeout_cb_called = 0;
	prfd_write_cb1_called = 0;

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(prfd_err_cb1_called == 1);
	assert(prfd_write_cb1_called == 0);
	assert(timeout_cb_called == 0);
	assert(pr_poll_loop_del_prfd(poll_loop, read_pipe) == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	assert(PR_DestroySocketPollFd(read_pipe) == PR_SUCCESS);
}

static void
test_complex(struct pr_poll_loop *poll_loop)
{
	int pipe_fd1[2], pipe_fd2[2];
	PRFileDesc *read_pipe1;
	PRFileDesc *write_pipe2;
	struct timer_list_entry *timeout_timer;

	init_global_vars();

	assert(pipe(pipe_fd1) == 0);
	assert(pipe(pipe_fd2) == 0);

	test_complex_read_pipe1_fd = pipe_fd1[0];

	/*
	 * Add pre poll cb1
	 */
	assert(pr_poll_loop_add_pre_poll_cb(poll_loop, test_pre_poll_cb1,
	    &pre_poll_cb1_called, test_pre_poll_cb1) == 0);

	read_pipe1 = PR_CreateSocketPollFd(pipe_fd1[0]);
	assert(read_pipe1 != NULL);
	write_pipe2 = PR_CreateSocketPollFd(pipe_fd2[1]);
	assert(write_pipe2 != NULL);

	assert(pr_poll_loop_add_prfd(poll_loop, read_pipe1, 0, test_complex_set_events_pipe1_read_cb,
	    test_complex_read_pipe1_read_cb, test_complex_write_pipe1_read_cb, NULL,
	    &test_complex_set_events_pipe1_read_called, test_complex_set_events_pipe1_read_cb) == 0);

	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd1[1], 0, test_complex_set_events_pipe1_write_cb,
	    test_complex_read_pipe1_write_cb, test_complex_write_pipe1_write_cb, NULL,
	    &test_complex_set_events_pipe1_write_called, test_complex_set_events_pipe1_write_cb) == 0);

	assert(pr_poll_loop_add_fd(poll_loop, pipe_fd2[0], POLLIN, test_complex_set_events_pipe2_read_cb,
	    test_complex_read_pipe2_read_cb, test_complex_write_pipe2_read_cb, NULL,
	    &test_complex_set_events_pipe2_read_called, test_complex_set_events_pipe2_read_cb) == 0);

	assert(pr_poll_loop_add_prfd(poll_loop, write_pipe2, POLLOUT, test_complex_set_events_pipe2_write_cb,
	    test_complex_read_pipe2_write_cb, test_complex_write_pipe2_write_cb, NULL,
	    &test_complex_set_events_pipe2_write_called, test_complex_set_events_pipe2_write_cb) == 0);

	timeout_cb_called = 0;
	test_complex_set_events_pipe1_read_called = 0;
	test_complex_read_pipe1_read_called = 0;
	test_complex_set_events_pipe1_write_called = 0;
	test_complex_write_pipe1_write_called = 0;
	test_complex_set_events_pipe2_read_called = 0;
	test_complex_read_pipe2_read_called = 0;
	test_complex_set_events_pipe2_write_called = 0;
	test_complex_write_pipe2_write_called = 0;
	pre_poll_cb1_called = 0;
	pre_poll_cb2_called = 0;

	/*
	 * Call for first time -> all set_events should be called and pipe2_write should be called
	 */
	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(pre_poll_cb1_called == 1);
	assert(pre_poll_cb2_called == 0);
	assert(test_complex_set_events_pipe1_read_called == 1);
	assert(test_complex_read_pipe1_read_called == 0);
	assert(test_complex_set_events_pipe1_write_called == 1);
	assert(test_complex_write_pipe1_write_called == 0);
	assert(test_complex_set_events_pipe2_read_called == 1);
	assert(test_complex_read_pipe2_read_called == 0);
	assert(test_complex_set_events_pipe2_write_called == 1);
	assert(test_complex_write_pipe2_write_called == 1);

	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Call for second time -> same as first time
	 */
	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(pre_poll_cb1_called == 2);
	assert(pre_poll_cb2_called == 0);
	assert(test_complex_set_events_pipe1_read_called == 2);
	assert(test_complex_read_pipe1_read_called == 0);
	assert(test_complex_set_events_pipe1_write_called == 2);
	assert(test_complex_write_pipe1_write_called == 0);
	assert(test_complex_set_events_pipe2_read_called == 2);
	assert(test_complex_read_pipe2_read_called == 0);
	assert(test_complex_set_events_pipe2_write_called == 2);
	assert(test_complex_write_pipe2_write_called == 2);

	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Change state to prepare for writing
	 */
	test_complex_state = 1;
	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(pre_poll_cb1_called == 3);
	assert(pre_poll_cb2_called == 0);
	assert(test_complex_set_events_pipe1_read_called == 3);
	assert(test_complex_read_pipe1_read_called == 0);
	assert(test_complex_set_events_pipe1_write_called == 3);
	assert(test_complex_write_pipe1_write_called == 1);
	assert(test_complex_set_events_pipe2_read_called == 3);
	assert(test_complex_read_pipe2_read_called == 0);
	assert(test_complex_set_events_pipe2_write_called == 3);
	assert(test_complex_write_pipe2_write_called == 3);

	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Write to first pipe
	 */
	assert(write(pipe_fd1[1], READ_STR, strlen(READ_STR) + 1) == strlen(READ_STR) + 1);

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(pre_poll_cb1_called == 4);
	assert(pre_poll_cb2_called == 0);
	assert(test_complex_set_events_pipe1_read_called == 4);
	assert(test_complex_read_pipe1_read_called == 0);
	assert(test_complex_set_events_pipe1_write_called == 4);
	assert(test_complex_write_pipe1_write_called == 2);
	assert(test_complex_set_events_pipe2_read_called == 4);
	assert(test_complex_read_pipe2_read_called == 0);
	assert(test_complex_set_events_pipe2_write_called == 4);
	assert(test_complex_write_pipe2_write_called == 4);

	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Delete pre poll cb
	 */
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb1) == 0);

	/*
	 * Change state so write can propagate
	 */
	test_complex_state = 2;
	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(pre_poll_cb1_called == 4);
	assert(pre_poll_cb2_called == 0);
	assert(test_complex_set_events_pipe1_read_called == 5);
	assert(test_complex_read_pipe1_read_called == 1);
	assert(test_complex_set_events_pipe1_write_called == 5);
	assert(test_complex_write_pipe1_write_called == 2);
	assert(test_complex_set_events_pipe2_read_called == 5);
	assert(test_complex_read_pipe2_read_called == 0);
	assert(test_complex_set_events_pipe2_write_called == 5);
	assert(test_complex_write_pipe2_write_called == 5);

	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Add pre poll cb 1 and 2
	 */
	assert(pr_poll_loop_add_pre_poll_cb(poll_loop, test_pre_poll_cb1,
	    &pre_poll_cb1_called, test_pre_poll_cb1) == 0);
	assert(pr_poll_loop_add_pre_poll_cb(poll_loop, test_pre_poll_cb2,
	    &pre_poll_cb2_called, test_pre_poll_cb2) == 0);

	/*
	 * Change state so pipe1 events are not called any longer
	 */
	test_complex_state = 4;
	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(pre_poll_cb1_called == 5);
	assert(pre_poll_cb2_called == 1);
	assert(test_complex_set_events_pipe1_read_called == 6);
	assert(test_complex_read_pipe1_read_called == 1);
	assert(test_complex_set_events_pipe1_write_called == 6);
	assert(test_complex_write_pipe1_write_called == 2);
	assert(test_complex_set_events_pipe2_read_called == 6);
	assert(test_complex_read_pipe2_read_called == 0);
	assert(test_complex_set_events_pipe2_write_called == 6);
	assert(test_complex_write_pipe2_write_called == 6);

	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Write to second pipe
	 */
	assert(write(pipe_fd2[1], READ_STR, strlen(READ_STR) + 1) == strlen(READ_STR) + 1);

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(pre_poll_cb1_called == 6);
	assert(pre_poll_cb2_called == 2);
	assert(test_complex_set_events_pipe1_read_called == 7);
	assert(test_complex_read_pipe1_read_called == 1);
	assert(test_complex_set_events_pipe1_write_called == 7);
	assert(test_complex_write_pipe1_write_called == 2);
	assert(test_complex_set_events_pipe2_read_called == 7);
	assert(test_complex_read_pipe2_read_called == 1);
	assert(test_complex_set_events_pipe2_write_called == 7);
	assert(test_complex_write_pipe2_write_called == 7);

	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * And call again
	 */
	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(pre_poll_cb1_called == 7);
	assert(pre_poll_cb2_called == 3);
	assert(test_complex_set_events_pipe1_read_called == 8);
	assert(test_complex_read_pipe1_read_called == 1);
	assert(test_complex_set_events_pipe1_write_called == 8);
	assert(test_complex_write_pipe1_write_called == 2);
	assert(test_complex_set_events_pipe2_read_called == 8);
	assert(test_complex_read_pipe2_read_called == 1);
	assert(test_complex_set_events_pipe2_write_called == 8);
	assert(test_complex_write_pipe2_write_called == 8);

	assert(timeout_cb_called == 0);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	assert(PR_DestroySocketPollFd(read_pipe1) == PR_SUCCESS);
	assert(PR_DestroySocketPollFd(write_pipe2) == PR_SUCCESS);

	assert(close(pipe_fd1[0]) == 0);
	assert(close(pipe_fd1[1]) == 0);

	assert(close(pipe_fd2[0]) == 0);
	assert(close(pipe_fd2[1]) == 0);

	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb1) == 0);
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb2) == 0);

	assert(pr_poll_loop_del_prfd(poll_loop, read_pipe1) == 0);
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd1[1]) == 0);
	assert(pr_poll_loop_del_fd(poll_loop, pipe_fd2[0]) == 0);
	assert(pr_poll_loop_del_prfd(poll_loop, write_pipe2) == 0);
}

static void
test_pre_poll_cb(struct pr_poll_loop *poll_loop)
{
	struct timer_list_entry *timeout_timer;

	init_global_vars();

	assert(pr_poll_loop_add_pre_poll_cb(poll_loop, test_pre_poll_cb1,
	    &pre_poll_cb1_called, test_pre_poll_cb1) == 0);
	assert(pr_poll_loop_add_pre_poll_cb(poll_loop, test_pre_poll_cb1,
	    &pre_poll_cb1_called, test_pre_poll_cb1) == -1);

	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb1) == 0);
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb1) == -1);
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb2) == -1);

	/*
	 * Just one pre poll cb
	 */
	pre_poll_cb1_called = 0;
	pre_poll_cb2_called = 0;
	pre_poll_cb_return_called = 0;

	assert(pr_poll_loop_add_pre_poll_cb(poll_loop, test_pre_poll_cb1,
	    &pre_poll_cb1_called, test_pre_poll_cb1) == 0);

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TEST_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	timeout_cb_called = 0;

	assert(pr_poll_loop_exec(poll_loop) == 0);
	assert(timeout_cb_called == 1);
	assert(pre_poll_cb1_called == 1);
	assert(pre_poll_cb2_called == 0);
	assert(pre_poll_cb_return_called == 0);

	/*
	 * Test again
	 */
	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TEST_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	timeout_cb_called = 0;

	assert(pr_poll_loop_exec(poll_loop) == 0);
	assert(timeout_cb_called == 1);
	assert(pre_poll_cb1_called == 2);
	assert(pre_poll_cb2_called == 0);
	assert(pre_poll_cb_return_called == 0);

	/*
	 * Add second cb
	 */
	assert(pr_poll_loop_add_pre_poll_cb(poll_loop, test_pre_poll_cb2,
	    &pre_poll_cb2_called, test_pre_poll_cb2) == 0);

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TEST_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	timeout_cb_called = 0;

	assert(pr_poll_loop_exec(poll_loop) == 0);
	assert(timeout_cb_called == 1);
	assert(pre_poll_cb1_called == 3);
	assert(pre_poll_cb2_called == 1);
	assert(pre_poll_cb_return_called == 0);

	/*
	 * Remove cb1
	 */
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb1) == 0);

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TEST_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	timeout_cb_called = 0;

	assert(pr_poll_loop_exec(poll_loop) == 0);
	assert(timeout_cb_called == 1);
	assert(pre_poll_cb1_called == 3);
	assert(pre_poll_cb2_called == 2);
	assert(pre_poll_cb_return_called == 0);

	/*
	 * Add cb1 and cb return
	 */
	assert(pr_poll_loop_add_pre_poll_cb(poll_loop, test_pre_poll_cb1,
	    &pre_poll_cb1_called, test_pre_poll_cb1) == 0);
	assert(pr_poll_loop_add_pre_poll_cb(poll_loop, test_pre_poll_cb_return,
	    &pre_poll_cb_return_called, test_pre_poll_cb_return) == 0);

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TEST_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	timeout_cb_called = 0;

	assert(pr_poll_loop_exec(poll_loop) == -1);
	assert(timeout_cb_called == 0);
	assert(pre_poll_cb1_called == 4);
	assert(pre_poll_cb2_called == 3);
	assert(pre_poll_cb_return_called == 1);
	timer_list_entry_delete(pr_poll_loop_get_timer_list(poll_loop), timeout_timer);

	/*
	 * Remove cb_return
	 */
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb_return) == 0);

	timeout_timer = timer_list_add(
	    pr_poll_loop_get_timer_list(poll_loop), TIMER_TEST_TIMEOUT, timeout_cb, NULL, NULL);
	assert(timeout_timer != NULL);
	timeout_cb_called = 0;

	assert(pr_poll_loop_exec(poll_loop) == 0);

	assert(timeout_cb_called == 1);
	assert(pre_poll_cb1_called == 5);
	assert(pre_poll_cb2_called == 4);
	assert(pre_poll_cb_return_called == 1);

	/*
	 * Cleanup
	 */
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb1) == 0);
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb2) == 0);
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb_return) == -1);
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb1) == -1);
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb2) == -1);
	assert(pr_poll_loop_del_pre_poll_cb(poll_loop, test_pre_poll_cb_return) == -1);
}

int
main(void)
{
	struct pr_poll_loop poll_loop;

	PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 0);

	pr_poll_loop_init(&poll_loop);

	test_fd_basics(&poll_loop);

	test_prfd_basics(&poll_loop);

	test_pre_poll_cb(&poll_loop);

	test_complex(&poll_loop);

	pr_poll_loop_destroy(&poll_loop);

	assert(PR_Cleanup() == PR_SUCCESS);

	return (0);
}
