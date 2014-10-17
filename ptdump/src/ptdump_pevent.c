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

#include "ptdump_pevent.h"

#include "intel-pt.h"

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>


/* A perf event sideband dumper. */
struct ptdump_obsv_pevent {
	/* The decode observer. */
	struct pt_observer obsv;

	/* The configuration. */
	struct ptdump_pevent_config config;

	/* The current event. */
	struct pev_event event;

	/* The position of @event in the sideband stream. */
	const uint8_t *current;

	/* The position of the next event in the sideband stream. */
	const uint8_t *pos;
};


static void pevent_print_header(const char *name, const struct pev_event *event,
				int verbose)
{
	printf("  %s", name);

	if (!event)
		return;

	/* In verbose mode, we'll print time as part of the samples.  Print it
	 * in compact form in non-verbose mode.
	 */
	if (event->sample.time && !verbose)
		printf(" [%" PRIx64 " (%" PRIx64 ")]",
		       event->sample.tsc, *event->sample.time);
}

static int pevent_any_samples_to_print(const struct pev_event *event,
				       int verbose)
{
	if (!event)
		return -pte_internal;

	if (event->sample.pid)
		return 1;

	if (event->sample.tid)
		return 1;

	if (event->sample.time && verbose)
		return 1;

	if (event->sample.id)
		return 1;

	if (event->sample.stream_id)
		return 1;

	if (event->sample.cpu)
		return 1;

	if (event->sample.identifier)
		return 1;

	return 0;
}

static void pevent_print_samples(const struct pev_event *event, int verbose)
{
	int status;

	if (!event)
		return;

	status = pevent_any_samples_to_print(event, verbose);
	if (status <= 0)
		return;

	if (!verbose)
		printf("  {");

	if (event->sample.pid && event->sample.tid) {
		if (verbose)
			printf("\n  pid: %x, tid: %x", *event->sample.pid,
			       *event->sample.tid);
		else
			printf(" %x/%x", *event->sample.pid,
			       *event->sample.tid);
	}

	/* We already printed the time in the header.  Skip the time sample
	 * unless we're in verbose mode.
	 */
	if (event->sample.time && verbose)
		printf("\n  time: %" PRIx64 ", tsc: %" PRIx64,
		       *event->sample.time, event->sample.tsc);

	if (event->sample.id) {
		if (verbose)
			printf("\n  id: %" PRIx64, *event->sample.id);
		else
			printf(" %" PRIx64, *event->sample.id);
	}

	if (event->sample.stream_id) {
		if (verbose)
			printf("\n  stream id: %" PRIx64,
			       *event->sample.stream_id);
		else
			printf(" %" PRIx64, *event->sample.stream_id);
	}

	if (event->sample.cpu) {
		if (verbose)
			printf("\n  cpu: %x", *event->sample.cpu);
		else
			printf(" cpu-%x", *event->sample.cpu);
	}

	if (event->sample.identifier) {
		if (verbose)
			printf("\n  identifier: %" PRIx64,
			       *event->sample.identifier);
		else
			printf(" %" PRIx64, *event->sample.identifier);
	}


	if (!verbose)
		printf(" }");
}

static int pevent_print_event(const struct pev_event *event, int verbose)
{
	if (!event)
		return -pte_internal;

	switch (event->type) {
	default:
		pevent_print_header("UNKNOWN", event, verbose);

		if (verbose) {
			printf("\n  type: %x", event->type);
			printf("\n  misc: %x", event->misc);
		} else
			printf(" (%x, %x)", event->type, event->misc);

		break;

	case PERF_RECORD_MMAP: {
		const struct pev_record_mmap *mmap;

		pevent_print_header("PERF_RECORD_MMAP", event, verbose);

		mmap = event->record.mmap;
		if (!mmap)
			return -pte_internal;

		if (verbose) {
			printf("\n  pid: %x, tid: %x", mmap->pid, mmap->tid);
			printf("\n  addr: %" PRIx64, mmap->addr);
			printf("\n  len: %" PRIx64, mmap->len);
			printf("\n  pgoff: %" PRIx64, mmap->pgoff);
			printf("\n  filename: %s", mmap->filename);
		} else
			printf("  %x/%x, %" PRIx64 ", %" PRIx64 ", %" PRIx64
			       ", %s",
			       mmap->pid, mmap->tid, mmap->addr, mmap->len,
			       mmap->pgoff, mmap->filename);
	}
		break;

	case PERF_RECORD_LOST: {
		const struct pev_record_lost *lost;

		pevent_print_header("PERF_RECORD_LOST", event, verbose);

		lost = event->record.lost;
		if (!lost)
			return -pte_internal;

		if (verbose) {
			printf("\n  id: %" PRIx64, lost->id);
			printf("\n  lost: %" PRIx64, lost->lost);
		} else
			printf("  %" PRIx64 ", %" PRIx64, lost->id, lost->lost);
	}
		break;

	case PERF_RECORD_COMM: {
		const struct pev_record_comm *comm;
		const char *name;

		if (event->misc & PERF_RECORD_MISC_COMM_EXEC)
			name = "PERF_RECORD_COMM.EXEC";
		else
			name = "PERF_RECORD_COMM";

		pevent_print_header(name, event, verbose);

		comm = event->record.comm;
		if (!comm)
			return -pte_internal;

		if (verbose) {
			printf("\n  pid: %x, tid: %x", comm->pid, comm->tid);
			printf("\n  comm: %s", comm->comm);
		} else
			printf("  %x/%x, %s", comm->pid, comm->tid, comm->comm);
	}
		break;

	case PERF_RECORD_EXIT: {
		const struct pev_record_exit *exit;

		pevent_print_header("PERF_RECORD_EXIT", event, verbose);

		exit = event->record.exit;
		if (!exit)
			return -pte_internal;

		if (verbose) {
			printf("\n  pid: %x, ppid: %x", exit->pid, exit->ppid);
			printf("\n  tid: %x, ptid: %x", exit->tid, exit->ptid);
			printf("\n  time: %" PRIx64, exit->time);
		} else
			printf("\n  %x/%x, %x/%x, %" PRIx64,
			       exit->pid, exit->tid, exit->ppid, exit->ptid,
			       exit->time);
	}
		break;

	case PERF_RECORD_THROTTLE: {
		const struct pev_record_throttle *throttle;

		pevent_print_header("PERF_RECORD_THROTTLE", event, verbose);

		throttle = event->record.throttle;
		if (!throttle)
			return -pte_internal;

		if (verbose) {
			printf("\n  time: %" PRIx64, throttle->time);
			printf("\n  id: %" PRIx64, throttle->id);
			printf("\n  stream_id: %" PRIx64, throttle->stream_id);
		} else
			printf("  %" PRIx64 ", %" PRIx64 ", %" PRIx64,
			       throttle->time, throttle->id,
			       throttle->stream_id);
	}
		break;

	case PERF_RECORD_UNTHROTTLE: {
		const struct pev_record_throttle *throttle;

		pevent_print_header("PERF_RECORD_UNTHROTTLE", event, verbose);

		throttle = event->record.throttle;
		if (!throttle)
			return -pte_internal;

		if (verbose) {
			printf("\n  time: %" PRIx64, throttle->time);
			printf("\n  id: %" PRIx64, throttle->id);
			printf("\n  stream_id: %" PRIx64, throttle->stream_id);
		} else
			printf("  %" PRIx64 ", %" PRIx64 ", %" PRIx64,
			       throttle->time, throttle->id,
			       throttle->stream_id);
	}
		break;

	case PERF_RECORD_FORK: {
		const struct pev_record_fork *fork;

		pevent_print_header("PERF_RECORD_FORK", event, verbose);

		fork = event->record.fork;
		if (!fork)
			return -pte_internal;

		if (verbose) {
			printf("\n  pid: %x, ppid: %x", fork->pid, fork->ppid);
			printf("\n  tid: %x, ptid: %x", fork->tid, fork->ptid);
			printf("\n  time: %" PRIx64, fork->time);
		} else
			printf("  %x/%x, %x/%x, %" PRIx64,
			       fork->pid, fork->tid, fork->ppid, fork->ptid,
			       fork->time);
	}
		break;

	case PERF_RECORD_MMAP2: {
		const struct pev_record_mmap2 *mmap2;

		pevent_print_header("PERF_RECORD_MMAP2", event, verbose);

		mmap2 = event->record.mmap2;
		if (!mmap2)
			return -pte_internal;

		if (verbose) {
			printf("\n  pid: %x, tid: %x", mmap2->pid, mmap2->tid);
			printf("\n  addr: %" PRIx64, mmap2->addr);
			printf("\n  len: %" PRIx64, mmap2->len);
			printf("\n  pgoff: %" PRIx64, mmap2->pgoff);
			printf("\n  maj: %x", mmap2->maj);
			printf("\n   min: %x", mmap2->min);
			printf("\n  ino: %" PRIx64, mmap2->ino);
			printf("\n  ino_generation: %" PRIx64,
			       mmap2->ino_generation);
			printf("\n  prot: %x", mmap2->prot);
			printf("\n  flags: %x", mmap2->flags);
			printf("\n  filename: %s", mmap2->filename);
		} else
			printf("  %x/%x, %" PRIx64 ", %" PRIx64 ", %" PRIx64
			       ", %x, %x, %" PRIx64 ", %" PRIx64 ", %x, %x, %s",
			       mmap2->pid, mmap2->tid, mmap2->addr, mmap2->len,
			       mmap2->pgoff, mmap2->maj, mmap2->min, mmap2->ino,
			       mmap2->ino_generation, mmap2->prot, mmap2->flags,
			       mmap2->filename);
	}
		break;

	case PERF_RECORD_AUX: {
		const struct pev_record_aux *aux;
		const char *name;

		aux = event->record.aux;
		if (!aux)
			return -pte_internal;

		if (aux->flags & PERF_AUX_FLAG_TRUNCATED)
			name = "PERF_RECORD_AUX.TRUNCATED";
		else
			name = "PERF_RECORD_AUX";

		pevent_print_header(name, event, verbose);


		if (verbose) {
			printf("\n  aux_offset: %" PRIx64, aux->aux_offset);
			printf("\n  aux_size: %" PRIx64, aux->aux_size);
			printf("\n  flags: %" PRIx64, aux->flags);
		} else
			printf("  %" PRIx64 ", %" PRIx64 ", %" PRIx64,
			       aux->aux_offset, aux->aux_size, aux->flags);
	}
		break;

	case PERF_RECORD_ITRACE_START: {
		const struct pev_record_itrace_start *itrace_start;

		pevent_print_header("PERF_RECORD_ITRACE_START", event, verbose);

		itrace_start = event->record.itrace_start;
		if (!itrace_start)
			return -pte_internal;

		if (verbose)
			printf("\n  pid: %x, tid: %x",
			       itrace_start->pid, itrace_start->tid);
		else
			printf("  %x/%x", itrace_start->pid, itrace_start->tid);
	}
		break;

	case PERF_RECORD_LOST_SAMPLES: {
		const struct pev_record_lost_samples *lost_samples;

		pevent_print_header("PERF_RECORD_LOST_SAMPLES", event, verbose);

		lost_samples = event->record.lost_samples;
		if (!lost_samples)
			return -pte_internal;

		if (verbose)
			printf("\n  lost: %" PRIx64, lost_samples->lost);
		else
			printf("  %" PRIx64, lost_samples->lost);
	}
		break;

	case PERF_RECORD_SWITCH: {
		const char *name;
		int is_switch_out;

		is_switch_out = pev_is_switch_out(event);
		if (is_switch_out < 0)
			return is_switch_out;

		if (is_switch_out)
			name = "PERF_RECORD_SWITCH.OUT";
		else
			name = "PERF_RECORD_SWITCH.IN";

		pevent_print_header(name, event, verbose);
	}
		break;

	case PERF_RECORD_SWITCH_CPU_WIDE: {
		const struct pev_record_switch_cpu_wide *switch_cpu_wide;
		const char *name, *next_prev;
		int is_switch_out;

		is_switch_out = pev_is_switch_out(event);
		if (is_switch_out < 0)
			return is_switch_out;

		if (is_switch_out) {
			name = "PERF_RECORD_SWITCH_CPU_WIDE.OUT";
			next_prev = "next";
		} else {
			name = "PERF_RECORD_SWITCH_CPU_WIDE.IN";
			next_prev = "prev";
		}

		pevent_print_header(name, event, verbose);

		switch_cpu_wide = event->record.switch_cpu_wide;
		if (!switch_cpu_wide)
			return -pte_internal;

		if (verbose)
			printf("\n  %s pid: %x, tid: %x",
			       next_prev, switch_cpu_wide->next_prev_pid,
			       switch_cpu_wide->next_prev_tid);
		else
			printf("  %x/%x",
			       switch_cpu_wide->next_prev_pid,
			       switch_cpu_wide->next_prev_tid);
	}
		break;
	}

	pevent_print_samples(event, verbose);
	printf("\n");

	return 0;
}

static int ptdump_obsv_pevent_setup(struct ptdump_obsv_pevent *obsv);

static int ptdump_obsv_pevent_tick(struct pt_observer *self, uint64_t tsc,
				   uint32_t lost_mtc, uint32_t lost_cyc)
{
	struct ptdump_obsv_pevent *obsv;

	(void) lost_mtc;
	(void) lost_cyc;

	if (!self)
		return -pte_internal;

	obsv = self->context;
	if (!obsv)
		return -pte_internal;

	if (!obsv->obsv.tick.callback || (tsc < obsv->obsv.tick.limit))
		return -pte_internal;

	/* Apply all events with the same timestamp.
	 *
	 * For events with different timestamp, libipt will recognize the
	 * configuration change and will call us again if the timestamp still
	 * lies within the limit.
	 *
	 * This allows us to better synchronize events from different sources.
	 */
	tsc = obsv->obsv.tick.limit;
	for (;;) {
		int errcode;

		if (!obsv->config.quiet) {
			if (obsv->config.prefix)
				printf("%s: ", obsv->config.prefix);

			printf("%016" PRIx64,
			       (uint64_t) (obsv->current - obsv->config.begin));

			errcode = pevent_print_event(&obsv->event,
						     obsv->config.verbose);
			if (errcode < 0)
				printf(" - error: %s",
				       pt_errstr(pt_errcode(errcode)));
		}

		errcode = ptdump_obsv_pevent_setup(obsv);
		if (errcode < 0)
			return errcode;

		if (!obsv->obsv.tick.callback || (tsc < obsv->obsv.tick.limit))
			break;
	}

	return 0;
}

static int ptdump_obsv_unsubscribe(struct pt_observer *obsv)
{
	if (!obsv)
		return -pte_internal;

	obsv->tick.callback = NULL;
	return 0;
}

static int ptdump_obsv_pevent_setup(struct ptdump_obsv_pevent *obsv)
{
	struct pt_observer *ptobsv;
	int size;

	if (!obsv)
		return -pte_internal;

	ptobsv = &obsv->obsv;

	size = pev_read(&obsv->event, obsv->pos, obsv->config.end,
			&obsv->config.pev);
	if (size < 0) {
		if (size == -pte_eos)
			return ptdump_obsv_unsubscribe(ptobsv);
		return size;
	}

	ptobsv->tick.callback = ptdump_obsv_pevent_tick;
	ptobsv->tick.limit = obsv->event.sample.tsc - obsv->config.tsc_offset;

	/* Process the record immediately if we don't have a time sample. */
	if (!obsv->event.sample.time)
		ptobsv->tick.limit = 0ull;

	obsv->current = obsv->pos;
	obsv->pos += size;

	return 0;
}

struct pt_observer *
ptdump_obsv_pevent_alloc(const struct ptdump_pevent_config *config)
{
	struct ptdump_obsv_pevent *obsv;
	struct pt_observer *ptobsv;
	int errcode;

	if (!config)
		return NULL;

	obsv = malloc(sizeof(*obsv));
	if (!obsv)
		return NULL;

	memset(obsv, 0, sizeof(*obsv));
	obsv->config = *config;
	obsv->pos = config->begin;

	ptobsv = &obsv->obsv;
	pt_obsv_init(ptobsv);
	ptobsv->context = obsv;

	errcode = ptdump_obsv_pevent_setup(obsv);
	if (errcode < 0) {
		free(obsv);
		return NULL;
	}

	return ptobsv;
}

void ptdump_obsv_pevent_free(struct pt_observer *obsv)
{
	free(obsv);
}
