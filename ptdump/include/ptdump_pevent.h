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

#include "pevent.h"

#include <stdint.h>

struct pt_observer;


/* A perf event sideband dumper configuration. */
struct ptdump_pevent_config {
	/* The perf event configuration. */
	struct pev_config pev;

	/* The memory buffer containing the sideband perf event records. */
	const uint8_t *begin;
	const uint8_t *end;

	/* An optional prefix to use before each sideband record. */
	const char *prefix;

	/* The TSC offset to apply to the trace time. */
	uint64_t tsc_offset;

	/* Verbose mode. */
	uint32_t verbose:1;

	/* Quiet mode. */
	uint32_t quiet:1;

	/* Show the file offset. */
	uint32_t show_offset:1;

	/* Show the file name. */
	uint32_t show_filename:1;
};

/* Allocate a perf event sideband dumper.
 *
 * Returns a pointer to the new sideband dumper on success.
 * Returns NULL otherwise.
 */
extern struct pt_observer *
ptdump_obsv_pevent_alloc(const struct ptdump_pevent_config *config);


/* Free a perf event sideband dumper. */
extern void ptdump_obsv_pevent_free(struct pt_observer *obsv);
