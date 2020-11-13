/*
 * Copyright (c) 2020 Red Hat, Inc.
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
#include <stdint.h>
#include <poll.h>

#include "timer-list.h"

#define SHORT_TIMEOUT			100
#define LONG_TIMEOUT			(60 * 1000)

#define SPEED_TEST_NO_ITEMS		10000

#define HEAP_TEST_NO_ITEMS		20
/*
 * Valid heap checking is slow
 */
#define HEAP_SPEED_TEST_NO_ITEMS	1000

static int timer_list_fn1_called = 0;

/*
 * Reimplementation of timer_list_entry_time_to_expire
 */
static uint8_t
time_to_expire(uint8_t expire_time, uint8_t current_time)
{
	uint8_t diff, half_interval;

	diff = expire_time - current_time;
	half_interval = ~0;
	half_interval /= 2;

	if (diff > half_interval) {
		return (0);
	}

	return (diff);
}

static void
check_time_to_expire(void)
{
	unsigned int current_time, delta, expire_time, res;

	for (current_time = 0; current_time <= 255 * 2; current_time++) {
		for (delta = 0; delta < 255; delta++) {
			expire_time = current_time + delta;

			res = time_to_expire((uint8_t)expire_time, (uint8_t)current_time);

			if (delta < 128) {
				assert(res == delta);
			} else {
				assert(res == 0);
			}
		}
	}
}

static int
timer_list_fn1(void *data1, void *data2)
{

	timer_list_fn1_called++;
	assert(data1 == &timer_list_fn1_called);
	assert(data2 == timer_list_fn1);

	return (0);
}

static void
check_timer_list_basics(void)
{
	struct timer_list tlist;
	struct timer_list_entry *tlist_entry;
	struct timer_list_entry *tlist_speed_entry[SPEED_TEST_NO_ITEMS];
	int i;

	timer_list_init(&tlist);

	assert(timer_list_add(&tlist, 0, timer_list_fn1, NULL, NULL) == NULL);
	assert(timer_list_add(&tlist, TIMER_LIST_MAX_INTERVAL + 1, timer_list_fn1, NULL, NULL) == NULL);
	assert(timer_list_add(&tlist, 1, NULL, NULL, NULL) == NULL);

	/*
	 * callback is called
	 */
	timer_list_fn1_called = 0;
	tlist_entry = timer_list_add(&tlist, SHORT_TIMEOUT / 2, timer_list_fn1, &timer_list_fn1_called, timer_list_fn1);
	assert(tlist_entry != NULL);
	(void)poll(NULL, 0, SHORT_TIMEOUT);
	assert(timer_list_time_to_expire(&tlist) == 0);
	assert(timer_list_time_to_expire_ms(&tlist) == 0);
	timer_list_expire(&tlist);
	assert(timer_list_fn1_called == 1);

	assert(timer_list_time_to_expire(&tlist) == PR_INTERVAL_NO_TIMEOUT);
	assert(timer_list_time_to_expire_ms(&tlist) == ~((uint32_t)0));
	timer_list_expire(&tlist);
	assert(timer_list_fn1_called == 1);

	/*
	 * Callback is not called
	 */
	timer_list_fn1_called = 0;
	tlist_entry = timer_list_add(&tlist, LONG_TIMEOUT, timer_list_fn1, &timer_list_fn1_called, timer_list_fn1);
	assert(tlist_entry != NULL);
	(void)poll(NULL, 0, SHORT_TIMEOUT);
	assert(timer_list_time_to_expire(&tlist) > 0);
	assert(timer_list_time_to_expire_ms(&tlist) > 0);
	timer_list_expire(&tlist);
	assert(timer_list_fn1_called == 0);

	/*
	 * Delete entry
	 */
	timer_list_entry_delete(&tlist, tlist_entry);
	assert(timer_list_time_to_expire(&tlist) == PR_INTERVAL_NO_TIMEOUT);
	assert(timer_list_time_to_expire_ms(&tlist) == ~((uint32_t)0));

	/*
	 * Check changing of interval
	 */
	timer_list_fn1_called = 0;
	tlist_entry = timer_list_add(&tlist, LONG_TIMEOUT, timer_list_fn1, &timer_list_fn1_called, timer_list_fn1);
	assert(tlist_entry != NULL);
	assert(timer_list_entry_set_interval(&tlist, tlist_entry, SHORT_TIMEOUT) == 0);
	(void)poll(NULL, 0, SHORT_TIMEOUT);
	assert(timer_list_time_to_expire(&tlist) == 0);
	assert(timer_list_time_to_expire_ms(&tlist) == 0);
	timer_list_expire(&tlist);
	assert(timer_list_fn1_called == 1);

	/*
	 * Test speed and more entries
	 */
	timer_list_fn1_called = 0;

	for (i = 0; i < SPEED_TEST_NO_ITEMS; i++) {
		tlist_speed_entry[i] = timer_list_add(&tlist, SHORT_TIMEOUT / 2,
		    timer_list_fn1, &timer_list_fn1_called, timer_list_fn1);
		assert(tlist_speed_entry[i] != NULL);
	}

	for (i = 0; i < SPEED_TEST_NO_ITEMS; i++) {
		timer_list_entry_reschedule(&tlist, tlist_speed_entry[i]);
	}

	(void)poll(NULL, 0, SHORT_TIMEOUT);
	timer_list_expire(&tlist);
	assert(timer_list_fn1_called == SPEED_TEST_NO_ITEMS);

	timer_list_free(&tlist);
}

static void
check_timer_heap(void)
{
	struct timer_list tlist;
	uint32_t u32;
	int i;
	int j;
	struct timer_list_entry *tlist_entry[HEAP_TEST_NO_ITEMS];
	struct timer_list_entry *tlist_entry_small;
	struct timer_list_entry *tlist_speed_entry[HEAP_SPEED_TEST_NO_ITEMS];

	timer_list_init(&tlist);

	/*
	 * Empty tlist
	 */
	assert(tlist.allocated == 0);
	assert(tlist.size == 0);

	u32 = ~((uint32_t)0);
	assert(timer_list_time_to_expire_ms(&tlist) == u32);
	assert(timer_list_time_to_expire(&tlist) == PR_INTERVAL_NO_TIMEOUT);

	/*
	 * Adding increasing numbers keeps heap property so there should be no reshufling
	 */
	for (i = 0; i < HEAP_TEST_NO_ITEMS; i++) {
		tlist_entry[i] = timer_list_add(&tlist, LONG_TIMEOUT * (i + 1),
		    timer_list_fn1, NULL, NULL);

		assert(tlist_entry[i] != NULL);
		assert(tlist.size == i + 1);

		assert(timer_list_debug_is_valid_heap(&tlist));

		for (j = 0; j < i + 1; j++) {
			assert(tlist.entries[j] == tlist_entry[j]);
		}
	}

	/*
	 * Add small item which should become first item in tlist entries to keep heap
	 * property
	 */
	tlist_entry_small = timer_list_add(&tlist, SHORT_TIMEOUT, timer_list_fn1, NULL, NULL);
	assert(timer_list_debug_is_valid_heap(&tlist));
	assert(tlist.size == i + 1);
	assert(tlist.entries[0] == tlist_entry_small);
	assert(timer_list_entry_get_interval(tlist_entry_small) == SHORT_TIMEOUT);

	/*
	 * Remove all items
	 */
	for (i = 0; i < HEAP_TEST_NO_ITEMS; i++) {
		timer_list_entry_delete(&tlist, tlist_entry[i]);

		assert(timer_list_debug_is_valid_heap(&tlist));
		assert(tlist.entries[0] == tlist_entry_small);
		assert(timer_list_entry_get_interval(tlist_entry[i]) == LONG_TIMEOUT * (i + 1));
	}

	/*
	 * Remove small item
	 */
	timer_list_entry_delete(&tlist, tlist_entry_small);
	assert(timer_list_debug_is_valid_heap(&tlist));

	/*
	 * Add items in reverse order
	 */
	for (i = 0; i < HEAP_TEST_NO_ITEMS; i++) {
		tlist_entry[i] = timer_list_add(&tlist, LONG_TIMEOUT * ((HEAP_TEST_NO_ITEMS - i) + 1),
		    timer_list_fn1, NULL, NULL);

		assert(tlist_entry[i] != NULL);
		assert(tlist.size == i + 1);

		assert(timer_list_debug_is_valid_heap(&tlist));
	}

	/*
	 * Remove all items
	 */
	for (i = 0; i < HEAP_TEST_NO_ITEMS; i++) {
		timer_list_entry_delete(&tlist, tlist_entry[i]);

		assert(timer_list_debug_is_valid_heap(&tlist));
	}

	/*
	 * Add items in standard and reverse order
	 */
	for (i = 0; i < HEAP_TEST_NO_ITEMS / 2; i++) {
		tlist_entry[i * 2] = timer_list_add(&tlist, LONG_TIMEOUT * ((HEAP_TEST_NO_ITEMS - i) + 1),
		    timer_list_fn1, NULL, NULL);

		assert(tlist_entry[i] != NULL);
		assert(tlist.size == (i * 2) + 1);

		tlist_entry[i * 2 + 1] = timer_list_add(&tlist, LONG_TIMEOUT * (i + 1),
		    timer_list_fn1, NULL, NULL);

		assert(tlist_entry[i] != NULL);
		assert(tlist.size == (i * 2) + 2);

		assert(timer_list_debug_is_valid_heap(&tlist));
	}

	/*
	 * Remove items
	 */
	for (i = 0; i < HEAP_TEST_NO_ITEMS; i++) {
		timer_list_entry_delete(&tlist, tlist_entry[i]);

		assert(timer_list_debug_is_valid_heap(&tlist));
	}

	assert(tlist.size == 0);

	/*
	 * Add items again
	 */
	for (i = 0; i < HEAP_TEST_NO_ITEMS / 2; i++) {
		tlist_entry[i * 2] = timer_list_add(&tlist, LONG_TIMEOUT * ((HEAP_TEST_NO_ITEMS - i) + 1),
		    timer_list_fn1, NULL, NULL);

		assert(tlist_entry[i] != NULL);
		assert(tlist.size == (i * 2) + 1);

		tlist_entry[i * 2 + 1] = timer_list_add(&tlist, LONG_TIMEOUT * (i + 1),
		    timer_list_fn1, NULL, NULL);

		assert(tlist_entry[i] != NULL);
		assert(tlist.size == (i * 2) + 2);

		assert(timer_list_debug_is_valid_heap(&tlist));
	}

	tlist_entry_small = timer_list_add(&tlist, SHORT_TIMEOUT, timer_list_fn1, NULL, NULL);
	assert(timer_list_debug_is_valid_heap(&tlist));
	assert(tlist.entries[0] == tlist_entry_small);

	/*
	 * And try reschedule
	 */
	for (i = 0; i < HEAP_TEST_NO_ITEMS; i++) {
		timer_list_entry_reschedule(&tlist, tlist_entry[i]);

		assert(timer_list_debug_is_valid_heap(&tlist));

		assert(tlist.entries[0] == tlist_entry_small);
	}

	/*
	 * Try delete
	 */
	timer_list_entry_delete(&tlist, tlist_entry_small);
	assert(timer_list_debug_is_valid_heap(&tlist));

	for (i = 0; i < HEAP_TEST_NO_ITEMS; i++) {
		timer_list_entry_delete(&tlist, tlist_entry[i]);

		assert(timer_list_debug_is_valid_heap(&tlist));
	}

	assert(tlist.size == 0);

	/*
	 * Speed test
	 */
	for (i = 0; i < HEAP_SPEED_TEST_NO_ITEMS; i++) {
		tlist_speed_entry[i] = timer_list_add(&tlist, SHORT_TIMEOUT / 2,
		    timer_list_fn1, &timer_list_fn1_called, timer_list_fn1);
		assert(tlist_speed_entry[i] != NULL);
		assert(timer_list_debug_is_valid_heap(&tlist));
	}

	for (i = 0; i < HEAP_SPEED_TEST_NO_ITEMS; i++) {
		timer_list_entry_reschedule(&tlist, tlist_speed_entry[i]);
		assert(timer_list_debug_is_valid_heap(&tlist));
	}

	/*
	 * Free list
	 */
	timer_list_free(&tlist);
}

int
main(void)
{

	PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 0);

	check_time_to_expire();

	check_timer_heap();

	check_timer_list_basics();

	assert(PR_Cleanup() == PR_SUCCESS);

	return (0);
}
