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

#ifndef PT_OBSERVER_H
#define PT_OBSERVER_H

#include "intel-pt.h"


/* A collection of attached observers. */
struct pt_obsv_collection {
	/* Time-based observation. */
	struct {
		/* The tick observer queue. */
		struct pt_observer *obsv;

		/* The smallest limit - UINT64_MAX if @obsv is NULL. */
		uint64_t limit;
	} tick;
};


/* Initialize an observer collection. */
extern void pt_obsvc_init(struct pt_obsv_collection *obsvc);

/* Finalize an observer collection. */
extern void pt_obsvc_fini(struct pt_obsv_collection *obsvc);

/* Add a new observer to all specified queues.
 *
 * Returns zero on success, a negative error code, otherwise.
 * Returns -pte_internal if @obsvc is NULL.
 * Returns -pte_invalid if @obsv is NULL or already on any queue in @obsvc.
 */
extern int pt_obsvc_add(struct pt_obsv_collection *obsvc,
			struct pt_observer *obsv);

/* Notify observers of a time change.
 *
 * Returns zero on success, a negative error code, otherwise.
 * Returns -pte_internal if @obsvc is NULL.
 */
extern int pt_obsvc_notify_tick(struct pt_obsv_collection *obsvc, uint64_t tsc,
				uint32_t lost_mtc, uint32_t lost_cyc);

static inline int pt_obsvc_tick(struct pt_obsv_collection *obsvc, uint64_t tsc,
				uint32_t lost_mtc, uint32_t lost_cyc)
{
	if (!obsvc)
		return -pte_internal;

	if (tsc < obsvc->tick.limit)
		return 0;

	return pt_obsvc_notify_tick(obsvc, tsc, lost_mtc, lost_cyc);
}

#endif /* PT_OBSERVER_H */
