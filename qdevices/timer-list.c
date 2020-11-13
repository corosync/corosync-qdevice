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

#include <assert.h>
#include <string.h>

#include "timer-list.h"

void
timer_list_init(struct timer_list *tlist)
{

	memset(tlist, 0, sizeof(*tlist));

	TAILQ_INIT(&tlist->free_list);
}

static PRIntervalTime
timer_list_entry_time_to_expire(const struct timer_list_entry *entry, PRIntervalTime current_time)
{
	PRIntervalTime diff, half_interval;

	diff = entry->expire_time - current_time;
	half_interval = ~0;
	half_interval /= 2;

	if (diff > half_interval) {
		return (0);
	}

	return (diff);
}

static int
timer_list_entry_cmp(const struct timer_list_entry *entry1,
    const struct timer_list_entry *entry2, PRIntervalTime current_time)
{
	PRIntervalTime diff1, diff2;
	int res;

	diff1 = timer_list_entry_time_to_expire(entry1, current_time);
	diff2 = timer_list_entry_time_to_expire(entry2, current_time);

	res = 0;

	if (diff1 < diff2) res = -1;
	if (diff1 > diff2) res = 1;

	return (res);
}

static size_t
timer_list_heap_index_left(size_t index)
{

	return (2 * index + 1);
}

static size_t
timer_list_heap_index_right(size_t index)
{

	return (2 * index + 2);
}

static size_t
timer_list_heap_index_parent(size_t index)
{

	return ((index - 1) / 2);
}

static void
timer_list_heap_entry_set(struct timer_list *tlist, size_t item_pos, struct timer_list_entry *entry)
{

	assert(item_pos < tlist->size);

	tlist->entries[item_pos] = entry;
	tlist->entries[item_pos]->heap_pos = item_pos;
}

static struct timer_list_entry *
timer_list_heap_entry_get(struct timer_list *tlist, size_t item_pos)
{

	assert(item_pos < tlist->size);

	return (tlist->entries[item_pos]);
}

static void
timer_list_heap_sift_up(struct timer_list *tlist, size_t item_pos)
{
	size_t parent_pos;
	struct timer_list_entry *parent_entry;
	struct timer_list_entry *item_entry;

	item_entry = timer_list_heap_entry_get(tlist, item_pos);

	parent_pos = timer_list_heap_index_parent(item_pos);

	while (item_pos > 0 &&
	    (parent_entry = timer_list_heap_entry_get(tlist, parent_pos),
	    timer_list_entry_cmp(parent_entry, item_entry, item_entry->epoch) > 0)) {
		/*
		 * Swap item and parent
		 */
		timer_list_heap_entry_set(tlist, parent_pos, item_entry);
		timer_list_heap_entry_set(tlist, item_pos, parent_entry);

		item_pos = parent_pos;
		parent_pos = timer_list_heap_index_parent(item_pos);
	}
}

static void
timer_list_heap_sift_down(struct timer_list *tlist, size_t item_pos)
{
	int cont;
	size_t left_pos, right_pos, smallest_pos;
	struct timer_list_entry *left_entry;
	struct timer_list_entry *right_entry;
	struct timer_list_entry *smallest_entry;
	struct timer_list_entry *tmp_entry;

	cont = 1;

	while (cont) {
		smallest_pos = item_pos;
		left_pos = timer_list_heap_index_left(item_pos);
		right_pos = timer_list_heap_index_right(item_pos);

		smallest_entry = timer_list_heap_entry_get(tlist, smallest_pos);

		if (left_pos < tlist->size &&
		    (left_entry = timer_list_heap_entry_get(tlist, left_pos),
		    timer_list_entry_cmp(left_entry, smallest_entry, smallest_entry->epoch) < 0)) {
			smallest_entry = left_entry;
			smallest_pos = left_pos;
		}

		if (right_pos < tlist->size &&
		    (right_entry = timer_list_heap_entry_get(tlist, right_pos),
		    timer_list_entry_cmp(right_entry, smallest_entry, smallest_entry->epoch) < 0)) {
			smallest_entry = right_entry;
			smallest_pos = right_pos;
		}

		if (smallest_pos == item_pos) {
			/*
			 * Item is smallest (or has no childs) -> heap property is restored
			 */
			cont = 0;
		} else {
			/*
			 * Swap item with smallest child
			 */
			tmp_entry = timer_list_heap_entry_get(tlist, item_pos);
			timer_list_heap_entry_set(tlist, item_pos, smallest_entry);
			timer_list_heap_entry_set(tlist, smallest_pos, tmp_entry);

			item_pos = smallest_pos;
		}
	}
}

static void
timer_list_heap_delete(struct timer_list *tlist, struct timer_list_entry *entry)
{
	size_t entry_pos;
	struct timer_list_entry *replacement_entry;
	int cmp_entries;

	entry_pos = entry->heap_pos;
	entry->heap_pos = (~(size_t)0);

	/*
	 * Swap element with last element
	 */
	replacement_entry = timer_list_heap_entry_get(tlist, tlist->size - 1);
	timer_list_heap_entry_set(tlist, entry_pos, replacement_entry);

	/*
	 * And "remove" last element (= entry)
	 */
	tlist->size--;

	/*
	 * Up (or down) heapify based on replacement item size
	 */
	cmp_entries = timer_list_entry_cmp(replacement_entry, entry, entry->epoch);

	if (cmp_entries < 0) {
		timer_list_heap_sift_up(tlist, entry_pos);
	} else if (cmp_entries > 0) {
		timer_list_heap_sift_down(tlist, entry_pos);
	}
}

/*
 * Check if heap is valid.
 * - Shape property is always fullfiled because of storage in array
 * - Check heap property
 */
int
timer_list_debug_is_valid_heap(struct timer_list *tlist)
{
	size_t i;
	size_t left_pos, right_pos;
	struct timer_list_entry *left_entry;
	struct timer_list_entry *right_entry;
	struct timer_list_entry *cur_entry;

	for (i = 0; i < tlist->size; i++) {
		cur_entry = timer_list_heap_entry_get(tlist, i);

		left_pos = timer_list_heap_index_left(i);
		right_pos = timer_list_heap_index_right(i);

		if (left_pos < tlist->size &&
		    (left_entry = timer_list_heap_entry_get(tlist, left_pos),
		    timer_list_entry_cmp(left_entry, cur_entry, cur_entry->epoch) < 0)) {
			return (0);
		}

		if (right_pos < tlist->size &&
		    (right_entry = timer_list_heap_entry_get(tlist, right_pos),
		    timer_list_entry_cmp(right_entry, cur_entry, cur_entry->epoch) < 0)) {
			return (0);
		}
	}

	return (1);
}

static int
timer_list_insert_into_list(struct timer_list *tlist, struct timer_list_entry *new_entry)
{
	size_t new_size;
	struct timer_list_entry **new_entries;

	/*
	 * This can overflow and it's not a problem
	 */
	new_entry->expire_time = new_entry->epoch + PR_MillisecondsToInterval(new_entry->interval);

	/*
	 * Heap insert
	 */
	if (tlist->size + 1 > tlist->allocated) {
		new_size = (tlist->allocated + 1) * 2;

		new_entries = realloc(tlist->entries, new_size * sizeof(tlist->entries[0]));

		if (new_entries == NULL) {
			return (-1);
		}

		tlist->allocated = new_size;
		tlist->entries = new_entries;
	}

	tlist->size++;
	timer_list_heap_entry_set(tlist, tlist->size - 1, new_entry);

	timer_list_heap_sift_up(tlist, tlist->size - 1);

	return (0);
}

struct timer_list_entry *
timer_list_add(struct timer_list *tlist, PRUint32 interval, timer_list_cb_fn func, void *data1,
    void *data2)
{
	struct timer_list_entry *new_entry;

	if (interval < 1 || interval > TIMER_LIST_MAX_INTERVAL || func == NULL) {
		return (NULL);
	}

	if (!TAILQ_EMPTY(&tlist->free_list)) {
		/*
		 * Use free list entry
		 */
		new_entry = TAILQ_FIRST(&tlist->free_list);
		TAILQ_REMOVE(&tlist->free_list, new_entry, entries);
	} else {
		/*
		 * Alloc new entry
		 */
		new_entry = malloc(sizeof(*new_entry));
		if (new_entry == NULL) {
			return (NULL);
		}
	}

	memset(new_entry, 0, sizeof(*new_entry));
	new_entry->epoch = PR_IntervalNow();
	new_entry->interval = interval;
	new_entry->func = func;
	new_entry->user_data1 = data1;
	new_entry->user_data2 = data2;
	new_entry->is_active = 1;
	new_entry->heap_pos = (~(size_t)0);

	if (timer_list_insert_into_list(tlist, new_entry) != 0) {
		TAILQ_INSERT_HEAD(&tlist->free_list, new_entry, entries);

		return (NULL);
	}

	return (new_entry);
}

void
timer_list_entry_reschedule(struct timer_list *tlist, struct timer_list_entry *entry)
{

	if (entry->is_active) {
		timer_list_heap_delete(tlist, entry);

		entry->epoch = PR_IntervalNow();

		timer_list_insert_into_list(tlist, entry);
	}
}

void
timer_list_expire(struct timer_list *tlist)
{
	PRIntervalTime now;
	struct timer_list_entry *entry;
	int res;

	now = PR_IntervalNow();

	while (tlist->size > 0 &&
	    (entry = timer_list_heap_entry_get(tlist, 0),
	    timer_list_entry_time_to_expire(entry, now) == 0)) {
		/*
		 * Expired
		 */
		res = entry->func(entry->user_data1, entry->user_data2);
		if (res == 0) {
			/*
			 * Move item to free list
			 */
			timer_list_entry_delete(tlist, entry);
		} else if (entry->is_active) {
			/*
			 * Schedule again
			 */
			timer_list_heap_delete(tlist, entry);

			entry->epoch = now;

			timer_list_insert_into_list(tlist, entry);
		}
	}
}

PRIntervalTime
timer_list_time_to_expire(struct timer_list *tlist)
{
	struct timer_list_entry *entry;

	if (tlist->size == 0) {
		return (PR_INTERVAL_NO_TIMEOUT);
	}

	entry = timer_list_heap_entry_get(tlist, 0);

	return (timer_list_entry_time_to_expire(entry, PR_IntervalNow()));
}

uint32_t
timer_list_time_to_expire_ms(struct timer_list *tlist)
{
	struct timer_list_entry *entry;
	uint32_t u32;

	if (tlist->size == 0) {
		u32 = ~((uint32_t)0);
		return (u32);
	}

	entry = timer_list_heap_entry_get(tlist, 0);

	return (PR_IntervalToMilliseconds(timer_list_entry_time_to_expire(entry, PR_IntervalNow())));
}

void
timer_list_entry_delete(struct timer_list *tlist, struct timer_list_entry *entry)
{

	if (entry->is_active) {
		/*
		 * Remove item from heap and move it to free list
		 */
		timer_list_heap_delete(tlist, entry);

		TAILQ_INSERT_HEAD(&tlist->free_list, entry, entries);
		entry->is_active = 0;
	}
}

void
timer_list_free(struct timer_list *tlist)
{
	struct timer_list_entry *entry;
	struct timer_list_entry *entry_next;
	size_t i;

	for (i = 0; i < tlist->size; i++) {
		free(timer_list_heap_entry_get(tlist, i));
	}

	free(tlist->entries);

	entry = TAILQ_FIRST(&tlist->free_list);

	while (entry != NULL) {
		entry_next = TAILQ_NEXT(entry, entries);

		free(entry);

		entry = entry_next;
	}

	timer_list_init(tlist);
}

PRUint32
timer_list_entry_get_interval(const struct timer_list_entry *entry)
{

	return (entry->interval);
}

int
timer_list_entry_set_interval(struct timer_list *tlist, struct timer_list_entry *entry,
    PRUint32 interval)
{

	if (interval < 1 || interval > TIMER_LIST_MAX_INTERVAL) {
		return (-1);
	}

	if (!entry->is_active) {
		return (-1);
	}

	timer_list_heap_delete(tlist, entry);

	entry->interval = interval;
	entry->epoch = PR_IntervalNow();

	timer_list_insert_into_list(tlist, entry);

	return (0);
}
