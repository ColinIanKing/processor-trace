/*
 * Copyright (c) 2014-2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "pt_observer.h"

#include <string.h>


void pt_obsvc_init(struct pt_obsv_collection *obsvc)
{
	if (!obsvc)
		return;

	memset(obsvc, 0, sizeof(*obsvc));
	obsvc->tick.limit = UINT64_MAX;
}

void pt_obsvc_fini(struct pt_obsv_collection *obsvc)
{
	(void) obsvc;
}

static int pt_obsvc_on_tick(struct pt_obsv_collection *obsvc,
			    struct pt_observer *obsv)
{
	struct pt_observer *next;

	if (!obsvc || !obsv)
		return -pte_internal;

	for (next = obsvc->tick.obsv; next; next = next->tick.next)
		if (next == obsv)
			return 1;

	return 0;
}

static int pt_obsvc_on_state(struct pt_obsv_collection *obsvc,
			     struct pt_observer *obsv)
{
	struct pt_observer *next;

	if (!obsvc || !obsv)
		return -pte_internal;

	for (next = obsvc->state.obsv; next; next = next->state.next)
		if (next == obsv)
			return 1;

	return 0;
}

static int pt_obsvc_on_ip(struct pt_obsv_collection *obsvc,
			  struct pt_observer *obsv)
{
	struct pt_observer *next;

	if (!obsvc || !obsv)
		return -pte_internal;

	for (next = obsvc->ip.obsv; next; next = next->ip.next)
		if (next == obsv)
			return 1;

	return 0;
}

static struct pt_observer *pt_obsvc_postpone_tick(struct pt_observer *obsv)
{
	struct pt_observer *root, **pnext, *next;
	uint64_t limit;

	if (!obsv)
		return NULL;

	root = obsv->tick.next;
	limit = obsv->tick.limit;

	pnext = &root;
	for (next = *pnext; next; next = *pnext) {
		if (limit < next->tick.limit)
			break;

		pnext = &next->tick.next;
	}

	*pnext = obsv;
	obsv->tick.next = next;

	return root;
}

static int pt_obsvc_add_tick(struct pt_obsv_collection *obsvc,
			     struct pt_observer *obsv)
{
	if (!obsvc || !obsv)
		return -pte_internal;

	if (!obsv->tick.callback)
		return -pte_invalid;

	if (obsv->tick.next)
		return -pte_invalid;

	obsv->tick.next = obsvc->tick.obsv;
	obsv = pt_obsvc_postpone_tick(obsv);
	if (!obsv)
		return -pte_internal;

	obsvc->tick.obsv = obsv;
	obsvc->tick.limit = obsv->tick.limit;

	return 0;
}

static int pt_obsvc_remove_tick(struct pt_obsv_collection *obsvc,
				struct pt_observer *obsv)
{
	struct pt_observer **pnext, *next;

	if (!obsv)
		return -pte_internal;

	pnext = &obsvc->tick.obsv;
	for (next = *pnext; next; next = *pnext) {
		if (next == obsv) {
			*pnext = next->tick.next;
			break;
		}

		pnext = &next->tick.next;
	}

	next = obsvc->tick.obsv;
	obsvc->tick.limit = next ? next->tick.limit : UINT64_MAX;

	return 0;
}

static int pt_obsvc_add_state(struct pt_obsv_collection *obsvc,
			      struct pt_observer *obsv)
{
	if (!obsvc || !obsv)
		return -pte_internal;

	if (!obsv->state.callback)
		return -pte_invalid;

	if (obsv->state.next)
		return -pte_invalid;

	obsv->state.next = obsvc->state.obsv;
	obsvc->state.obsv = obsv;

	return 0;
}

static int pt_obsvc_remove_state(struct pt_obsv_collection *obsvc,
				 struct pt_observer *obsv)
{
	struct pt_observer **pnext, *next;

	if (!obsv)
		return -pte_internal;

	pnext = &obsvc->state.obsv;
	for (next = *pnext; next; next = *pnext) {
		if (next == obsv) {
			*pnext = next->state.next;
			break;
		}

		pnext = &next->state.next;
	}

	return 0;
}

static int pt_obsvc_add_ip(struct pt_obsv_collection *obsvc,
			   struct pt_observer *obsv)
{
	if (!obsvc || !obsv)
		return -pte_internal;

	if (!obsv->ip.callback)
		return -pte_invalid;

	if (obsv->ip.next)
		return -pte_invalid;

	obsv->ip.next = obsvc->ip.obsv;
	obsvc->ip.obsv = obsv;

	return 0;
}

static int pt_obsvc_remove_ip(struct pt_obsv_collection *obsvc,
			      struct pt_observer *obsv)
{
	struct pt_observer **pnext, *next;

	if (!obsv)
		return -pte_internal;

	pnext = &obsvc->ip.obsv;
	for (next = *pnext; next; next = *pnext) {
		if (next == obsv) {
			*pnext = next->ip.next;
			break;
		}

		pnext = &next->ip.next;
	}

	return 0;
}

int pt_obsvc_add(struct pt_obsv_collection *obsvc, struct pt_observer *obsv)
{
	int errcode;

	if (!obsvc)
		return -pte_internal;

	if (!obsv)
		return -pte_invalid;

	if (pt_obsvc_on_tick(obsvc, obsv) || pt_obsvc_on_state(obsvc, obsv) ||
	    pt_obsvc_on_ip(obsvc, obsv))
		return -pte_invalid;

	if (obsv->tick.callback) {
		errcode = pt_obsvc_add_tick(obsvc, obsv);
		if (errcode < 0)
			return errcode;
	}

	if (obsv->state.callback) {
		errcode = pt_obsvc_add_state(obsvc, obsv);
		if (errcode < 0)
			return errcode;
	}

	if (obsv->ip.callback) {
		errcode = pt_obsvc_add_ip(obsvc, obsv);
		if (errcode < 0)
			return errcode;
	}

	return 0;
}

/* Update the tick chain of @obsv in @obsvc after a non-tick update.
 *
 * The state of @obsv prior to the callback is stored in @prev.
 */
static int pt_obsvc_update_tick(struct pt_obsv_collection *obsvc,
				struct pt_observer *obsv,
				const struct pt_observer *prev)
{
	if (!obsv || !prev)
		return -pte_internal;

	if (obsv->tick.callback) {
		if (!prev->tick.callback)
			return pt_obsvc_add_tick(obsvc, obsv);

		if (obsv->tick.limit != prev->tick.limit) {
			int errcode;

			errcode = pt_obsvc_remove_tick(obsvc, obsv);
			if (errcode < 0)
				return errcode;

			return pt_obsvc_add_tick(obsvc, obsv);
		}
	} else {
		if (prev->tick.callback)
			return pt_obsvc_remove_tick(obsvc, obsv);
	}

	return 0;
}

/* Update the state chain of @obsv in @obsvc after a non-state update.
 *
 * The state of @obsv prior to the callback is stored in @prev.
 */
static int pt_obsvc_update_state(struct pt_obsv_collection *obsvc,
				 struct pt_observer *obsv,
				 const struct pt_observer *prev)
{
	if (!obsv || !prev)
		return -pte_internal;

	if (obsv->state.callback) {
		if (!prev->state.callback)
			return pt_obsvc_add_state(obsvc, obsv);
	} else {
		if (prev->state.callback)
			return pt_obsvc_remove_state(obsvc, obsv);
	}

	return 0;
}

/* Update the ip chain of @obsv in @obsvc after a non-ip update.
 *
 * The state of @obsv prior to the callback is stored in @prev.
 */
static int pt_obsvc_update_ip(struct pt_obsv_collection *obsvc,
			      struct pt_observer *obsv,
			      const struct pt_observer *prev)
{
	if (!obsv || !prev)
		return -pte_internal;

	if (obsv->ip.callback) {
		if (!prev->ip.callback)
			return pt_obsvc_add_ip(obsvc, obsv);
	} else {
		if (prev->ip.callback)
			return pt_obsvc_remove_ip(obsvc, obsv);
	}

	return 0;
}

int pt_obsvc_notify_tick(struct pt_obsv_collection *obsvc, uint64_t tsc,
			 uint32_t lost_mtc, uint32_t lost_cyc)
{
	struct pt_observer **pnext, *next, *fixup;
	int errcode;

	if (!obsvc)
		return -pte_internal;

	if (tsc < obsvc->tick.limit)
		return 0;

	errcode = 0;
	fixup = NULL;

	pnext = &obsvc->tick.obsv;
	for (next = *pnext; next; next = *pnext) {
		struct pt_observer current, *update;
		int errupd;

		/* Observers are supposed to unsubscribe themselves only
		 * during a callback call.
		 *
		 * We have no means to enforce this, though, so let's check.
		 */
		if (!next->tick.callback)
			return -pte_invalid;

		/* The observer list is sorted by limit. */
		if (tsc < next->tick.limit)
			break;

		/* Copy the current observer so we know what changed. */
		current = *next;

		/* We delay processing of observer errors to handle an
		 * additional configuration change.
		 */
		errcode = current.tick.callback(next, tsc, lost_mtc, lost_cyc);

		/* Check if the observer's configuration changed. */
		if (!next->tick.callback) {
			/* It unsubscribed - remove it from the list. */
			next->tick.next = NULL;
			*pnext = current.tick.next;
		} else if (current.tick.limit < next->tick.limit) {
			/* It set the limit into its future. */
			update = pt_obsvc_postpone_tick(next);
			if (!update) {
				errcode = -pte_internal;
				break;
			}

			/* Update the observer for the next iteration in case
			 * it changed.
			 *
			 * Note that the observer's future might still lie in
			 * our past, i.e. the observer's new limit might still
			 * be smaller than @tsc.
			 *
			 * In this case, the observer might be called again.
			 */
			if (update != next)
				*pnext = update;
		} else if (next->tick.limit < current.tick.limit) {
			/* It set the limit into its past.
			 *
			 * Remove the observer for now - we will re-insert it
			 * at the correct position when we're done.
			 */
			next->tick.next = fixup;
			fixup = next;
			*pnext = current.tick.next;
		} else {
			/* No change that would affect this traversal. */
			pnext = &next->tick.next;
		}

		/* Apply state configuration changes before reporting callback
		 * errors so we don't end up with an inconsistent state in case
		 * of errors.
		 */
		errupd = pt_obsvc_update_state(obsvc, next, &current);
		if (errupd < 0)
			errcode = errupd;

		errupd = pt_obsvc_update_ip(obsvc, next, &current);
		if (errupd < 0)
			errcode = errupd;

		/* Any error aborts the traversal after completing the current
		 * observer.
		 */
		if (errcode < 0)
			break;
	}

	/* Re-insert temporarily removed observers into the tick queue.
	 *
	 * We postponed that to not affect the current traversal.
	 */
	while (fixup) {
		next = fixup;
		fixup = fixup->tick.next;

		next->tick.next = obsvc->tick.obsv;
		next = pt_obsvc_postpone_tick(next);
		if (!next)
			return -pte_internal;

		obsvc->tick.obsv = next;
	}

	/* Recompute the global tick limit - it's too complicated to keep
	 * tack of it during all the updates above.
	 */
	next = obsvc->tick.obsv;
	if (next)
		obsvc->tick.limit = next->tick.limit;
	else
		obsvc->tick.limit = UINT64_MAX;

	return errcode;
}

int pt_obsvc_notify_state(struct pt_obsv_collection *obsvc,
			  enum pt_decode_state state)
{
	struct pt_observer **pnext, *next;
	int errcode;

	if (!obsvc)
		return -pte_internal;

	errcode = 0;

	pnext = &obsvc->state.obsv;
	for (next = *pnext; next; next = *pnext) {
		struct pt_observer current;
		int errupd;

		/* Observers are supposed to unsubscribe themselves only
		 * during a callback call.
		 *
		 * We have no means to enforce this, though, so let's check.
		 */
		if (!next->state.callback)
			return -pte_invalid;

		/* Copy the current observer so we know what changed. */
		current = *next;

		/* We delay processing of observer errors to handle an
		 * additional configuration change.
		 */
		errcode = current.state.callback(next, state);

		/* Check if the observer's configuration changed. */
		if (!next->state.callback) {
			/* It unsubscribed - remove it from the list. */
			next->state.next = NULL;
			*pnext = current.state.next;
		} else {
			/* No change that would affect this traversal. */
			pnext = &next->state.next;
		}

		/* Apply state configuration changes before reporting callback
		 * errors so we don't end up with an inconsistent state in case
		 * of errors.
		 */
		errupd = pt_obsvc_update_tick(obsvc, next, &current);
		if (errupd < 0)
			errcode = errupd;

		errupd = pt_obsvc_update_ip(obsvc, next, &current);
		if (errupd < 0)
			errcode = errupd;

		/* Any error aborts the traversal after completing the current
		 * observer.
		 */
		if (errcode < 0)
			break;
	}

	return errcode;
}

int pt_obsvc_notify_ip(struct pt_obsv_collection *obsvc, uint64_t ip)
{
	struct pt_observer **pnext, *next;
	int errcode;

	if (!obsvc)
		return -pte_internal;

	errcode = 0;

	pnext = &obsvc->ip.obsv;
	for (next = *pnext; next; next = *pnext) {
		struct pt_observer current;
		int errupd;

		/* Observers are supposed to unsubscribe themselves only
		 * during a callback call.
		 *
		 * We have no means to enforce this, though, so let's check.
		 */
		if (!next->ip.callback)
			return -pte_invalid;

		/* Copy the current observer so we know what changed. */
		current = *next;

		/* We delay processing of observer errors to handle an
		 * additional configuration change.
		 */
		errcode = current.ip.callback(next, ip);

		/* Check if the observer's configuration changed. */
		if (!next->ip.callback) {
			/* It unsubscribed - remove it from the list. */
			next->ip.next = NULL;
			*pnext = current.ip.next;
		} else {
			/* No change that would affect this traversal. */
			pnext = &next->ip.next;
		}

		/* Apply state configuration changes before reporting callback
		 * errors so we don't end up with an inconsistent state in case
		 * of errors.
		 */
		errupd = pt_obsvc_update_tick(obsvc, next, &current);
		if (errupd < 0)
			errcode = errupd;

		errupd = pt_obsvc_update_state(obsvc, next, &current);
		if (errupd < 0)
			errcode = errupd;

		/* Any error aborts the traversal after completing the current
		 * observer.
		 */
		if (errcode < 0)
			break;
	}

	return errcode;
}
