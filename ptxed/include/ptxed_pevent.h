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

#ifndef PTXED_PEVENT_H
#define PTXED_PEVENT_H

#include "pevent.h"
#include "ptxed.h"

#include <stdint.h>

struct pt_observer;


/* A collection of perf event sideband decoder flags. */
enum ptxed_pevent_flag {
	/* Log image section changes. */
	ppf_log_image	= 1u << 0,

	/* Log image switches. */
	ppf_log_switch	= 1u << 1
};

/* A perf event sideband decoder configuration. */
struct ptxed_pevent_config {
	/* The perf event configuration. */
	struct pev_config pev;

	/* The memory buffer containing the sideband perf event records.
	 *
	 * It is owned by this decoder and will be freed when the sideband
	 * decoder is freed.
	 */
	uint8_t *begin;
	uint8_t *end;

	/* The decoder to maintain.
	 *
	 * The decoder is maintained for master sideband channels that are
	 * directly related to the trace.
	 *
	 * For secondary sideband channels, we maintain the images of processes
	 * whose execution trace is currently not being decoded.  Set the
	 * decoder to NULL in this case.
	 */
	struct ptxed_decoder decoder;

	/* The global image section cache to use.
	 *
	 * This allows sharing image sections across process contexts.
	 */
	struct pt_image_section_cache *iscache;

	/* The kernel start address.  This is used to distinguish kernel objects
	 * from user objects.
	 *
	 * This is only required when tracing ring-0.
	 */
	uint64_t kernel_start;

	/* The file containing the VDSO.
	 *
	 * The VDSO is mapped into each process.
	 */
	const char *vdso;

	/* The (optional) sysroot.
	 *
	 * If not NULL, this is prepended to every perf event file name.
	 */
	const char *sysroot;

	/* The sideband TSC offset.
	 *
	 * The number of TSC ticks to add when processing sideband events.
	 *
	 * This causes sideband events to be processed a bit earlier, which
	 * may help with coarse timing information in the trace.
	 *
	 * A good value is about the number of ticks it takes from context
	 * switch to userland.
	 */
	uint64_t tsc_offset;

	/* A bit-vector of ptxed_pevent_flags. */
	uint32_t flags;

	/* A collection of configuration flags saying:
	 *
	 * - whether ring-0 has been traced
	 */
	uint32_t ring_0:1;

	/* - whether ring-3 has been traced */
	uint32_t ring_3:1;
};

/* Allocate a perf event sideband decoder.
 *
 * Returns a pointer to the new sideband decoder on success.
 * Returns NULL otherwise.
 */
extern struct pt_observer *
ptxed_obsv_pevent_alloc(const struct ptxed_pevent_config *config);

/* Free a perf event sideband decoder.
 *
 * This also frees the sideband buffer.
 */
extern void ptxed_obsv_pevent_free(struct pt_observer *obsv);

/* Set the maintained decoder.
 *
 * On success, set's @obsv's maintained decoder to @decoder.
 *
 * This is to allow observers to be allocated before decoders to avoid either
 * having to store observer configurations or imposing an order on ptxed
 * options.
 *
 * Returns zero on success, a negative error code otherwise.
 */
extern int ptxed_obsv_pevent_set_decoder(struct pt_observer *obsv,
					 const struct ptxed_decoder *decoder);

/* Add the kcore file to the kernel image using @iscache.
 *
 * Returns zero on success, a negative error code otherwise.
 */
extern int ptxed_obsv_pevent_kcore(struct pt_image_section_cache *iscache,
				   const char *filename, uint64_t base,
				   const char *prog, int verbose);

#endif /* PTXED_PEVENT_H */
