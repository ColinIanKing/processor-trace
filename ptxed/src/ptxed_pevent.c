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

/* For mkstemps(). */
#define _GNU_SOURCE 1

#include "ptxed_pevent.h"
#if defined(FEATURE_ELF)
# include "load_elf.h"
#endif /* defined(FEATURE_ELF) */

#include "intel-pt.h"

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>


/* A process context. */
struct ptxed_context {
	/* The next context in a linear list. */
	struct ptxed_context *next;

	/* The memory image for this process including user and kernel.
	 *
	 * The image is never NULL.
	 */
	struct pt_image *image;

	/* The process id. */
	uint32_t pid;

	/* The use count - last user frees context. */
	uint32_t ucount;
};

/* A perf event sideband decoder. */
struct ptxed_obsv_pevent {
	/* The decode observer. */
	struct pt_observer obsv;

	/* The configuration. */
	struct ptxed_pevent_config config;

	/* The current event. */
	struct pev_event event;

	/* The position of the next event in the sideband stream. */
	const uint8_t *pos;

	/* The current context. */
	struct ptxed_context *context;

	/* The current decode state. */
	enum pt_decode_state state;

	/* The context to switch to.
	 *
	 * This is set to the next context when processing context switch
	 * sideband until the exact switch location can be determined.
	 *
	 * It is NULL otherwise.
	 */
	struct ptxed_context *next_context;
};


/* The process contexts encountered so far.
 *
 * The list is used and maintained by multiple perf event sideband decoders.
 */
static struct ptxed_context *ptxed_contexts;

/* The kernel image sections.
 *
 * The kernel is mapped into every process.  We store the kernel image
 * separately for two reasons:
 *
 *   - we can populate the image of new processes
 *   - it allows sharing image sections
 */
static struct pt_image *kernel_image;

/* The number of observers using the above data.
 *
 * The last observer to be freed also frees the contexts and kernel image.
 */
static uint32_t ucount;


static void ptxed_obsv_pevent_log_image_init(const struct pt_image *image,
					     const struct pt_image *parent)
{
	const char *iname, *pname;

	iname = pt_image_name(image);
	if (!iname)
		iname = "none";

	pname = pt_image_name(parent);
	if (!pname)
		pname = "none";

	printf("[image: %s - init %s]\n", iname, pname);
}

static void ptxed_obsv_pevent_log_image_fini(const struct pt_image *image)
{
	const char *name;

	name = pt_image_name(image);
	if (!name)
		name = "none";

	printf("[image: %s - exit]\n", name);
}

static void ptxed_obsv_pevent_log_image_exec(const struct pt_image *image)
{
	const char *name;

	name = pt_image_name(image);
	if (!name)
		name = "none";

	printf("[image: %s - exec]\n", name);
}

static void ptxed_obsv_pevent_log_image_switch(const struct pt_image *image)
{
	const char *name;

	name = pt_image_name(image);
	if (!name)
		name = "none";

	printf("[image: %s]\n", name);
}

static void ptxed_obsv_pevent_log_image_add(const struct pt_image *image,
					    const char *filename,
					    uint64_t vaddr, uint64_t size)
{
	const char *name;

	name = pt_image_name(image);
	if (!name)
		name = "none";

	if (!filename)
		filename = "none";

	printf("[image: %s - [%" PRIx64 "; %" PRIx64 "[ (%s)]\n",
	       name, vaddr, vaddr + size, filename);
}

static const char *ptxed_pevent_name(enum perf_event_type type)
{
	switch (type) {
	case PERF_RECORD_ITRACE_START:
		return "PERF_RECORD_ITRACE_START";

	case PERF_RECORD_FORK:
		return "PERF_RECORD_FORK";

	case PERF_RECORD_COMM:
		return "PERF_RECORD_COMM";

	case PERF_RECORD_SWITCH:
		return "PERF_RECORD_SWITCH";

	case PERF_RECORD_SWITCH_CPU_WIDE:
		return "PERF_RECORD_SWITCH_CPU_WIDE";

	case PERF_RECORD_MMAP:
		return "PERF_RECORD_MMAP";

	case PERF_RECORD_MMAP2:
		return "PERF_RECORD_MMAP2";

	default:
		return "<unknown>";
	}
}

static struct ptxed_context *ptxed_context_alloc(uint32_t pid)
{
	struct ptxed_context *context;
	struct pt_image *image;
	char iname[16];

	memset(iname, 0, sizeof(iname));

	(void) snprintf(iname, sizeof(iname), "img-%x", pid);

	image = pt_image_alloc(iname);
	if (!image)
		return NULL;

	context = malloc(sizeof(*context));
	if (!context) {
		pt_image_free(image);
		return NULL;
	}

	memset(context, 0, sizeof(*context));
	context->image = image;
	context->pid = pid;
	context->ucount = 1;

	return context;
}

static void ptxed_context_free(struct ptxed_context *context)
{
	if (!context)
		return;

	pt_image_free(context->image);
	free(context);
}

static int ptxed_context_get(struct ptxed_context *context)
{
	uint32_t ucount;

	if (!context)
		return -pte_internal;

	ucount = context->ucount;
	if (!ucount)
		return -pte_internal;

	ucount += 1;
	if (!ucount)
		return -pte_internal;

	context->ucount = ucount;
	return 0;
}

static int ptxed_context_put(struct ptxed_context *context)
{
	uint32_t ucount;

	if (!context)
		return -pte_internal;

	ucount = context->ucount;
	if (!ucount)
		return -pte_internal;

	ucount = context->ucount = ucount - 1;
	if (!ucount)
		ptxed_context_free(context);

	return 0;
}

static struct ptxed_context *ptxed_context_by_pid(uint32_t pid)
{
	struct ptxed_context *context;

	for (context = ptxed_contexts; context; context = context->next) {
		if (context->pid == pid)
			return context;
	}

	return NULL;
}

static void ptxed_context_remove_by_pid(uint32_t pid, uint32_t flags)
{
	struct ptxed_context **list;

	list = &ptxed_contexts;
	while (*list) {
		struct ptxed_context *context;

		context = *list;
		if (context->pid != pid)
			list = &context->next;
		else {
			const struct pt_image *image;
			int errcode;

			*list = context->next;
			image = context->image;

			if (flags & ppf_log_image)
				ptxed_obsv_pevent_log_image_fini(image);

			errcode = ptxed_context_put(context);
			if (errcode < 0)
				printf("[?, ?: pevent error: %s]\n",
				       pt_errstr(pt_errcode(errcode)));
		}
	}
}

static void ptxed_context_clear(void)
{
	while (ptxed_contexts) {
		struct ptxed_context *trash;
		int errcode;

		trash = ptxed_contexts;
		ptxed_contexts = trash->next;

		errcode = ptxed_context_put(trash);
		if (errcode < 0)
			printf("[?, ?: pevent error: %s]\n",
			       pt_errstr(pt_errcode(errcode)));
	}
}

static int ptxed_obsv_is_kernel_addr(uint64_t vaddr,
				     const struct ptxed_pevent_config *config)
{
	if (!config)
		return -pte_internal;

	return config->kernel_start <= vaddr;
}

static struct ptxed_context *
ptxed_obsv_pid_context(struct ptxed_obsv_pevent *obsv, uint32_t pid)
{
	struct ptxed_context *context;
	int errcode;

	if (!obsv)
		return NULL;

	/* Try to find the context in our global context list. */
	context = ptxed_context_by_pid(pid);
	if (context)
		return context;

	/* We do not have a context for @pid, yet.  Create one. */
	context = ptxed_context_alloc(pid);
	if (!context)
		return NULL;

	/* Populate the image with kernel sections. */
	if (kernel_image) {
		if (obsv->config.flags & ppf_log_image)
			ptxed_obsv_pevent_log_image_init(context->image,
							 kernel_image);

		errcode = pt_image_copy(context->image, kernel_image);
		if (errcode < 0) {
			ptxed_context_free(context);
			return NULL;
		}
	}

	/* Add the context to the global context list. */
	context->next = ptxed_contexts;
	ptxed_contexts = context;

	return context;
}

static int ptxed_obsv_pevent_switch_image(struct ptxed_obsv_pevent *obsv,
					  struct ptxed_context *next)
{
	const struct ptxed_decoder *decoder;
	struct ptxed_context *prev;
	int errcode;

	if (!obsv || !next)
		return -pte_internal;

	decoder = &obsv->config.decoder;

	/* We must have a decoder. */
	if (!ptxed_have_decoder(decoder))
		return -pte_internal;

	/* There is nothing to do if we're just switching threads. */
	prev = obsv->context;
	if (next == prev)
		return 0;

	errcode = ptxed_context_get(next);
	if (errcode < 0)
		return errcode;

	if (obsv->config.flags & ppf_log_switch)
		ptxed_obsv_pevent_log_image_switch(next->image);

	switch (decoder->type) {
	case pdt_insn_decoder:
		errcode = pt_insn_set_image(decoder->variant.insn, next->image);
		if (errcode < 0)
			return errcode;
		break;

	case pdt_block_decoder:
		errcode = pt_blk_set_image(decoder->variant.block, next->image);
		if (errcode < 0)
			return errcode;
		break;
	}

	obsv->context = next;

	if (!prev)
		return 0;

	return ptxed_context_put(prev);
}

static int ptxed_obsv_pevent_clear_next_context(struct ptxed_obsv_pevent *obsv)
{
	struct ptxed_context *old;

	if (!obsv)
		return -pte_internal;

	old = obsv->next_context;
	if (!old)
		return 0;

	obsv->next_context = NULL;
	obsv->obsv.ip.callback = NULL;

	return ptxed_context_put(old);
}

static int ptxed_obsv_pevent_ip(struct pt_observer *self, uint64_t ip)
{
	struct ptxed_obsv_pevent *obsv;
	struct ptxed_context *context;
	int errcode;

	if (!self)
		return -pte_internal;

	obsv = self->context;
	if (!obsv)
		return -pte_internal;

	/* Since the kernel is mapped into every process, we may switch
	 * processes anywhere in the kernel.
	 *
	 * We could try to map the context switch to the __switch_to function
	 * but this is rather sensitive to the precision of our timing
	 * information.  We also don't want instruction based observation always
	 * on to work around timing imprecisions.
	 *
	 * Since we want to allow decode to start from any position in the
	 * trace, there's the additional problem that we can't really tell
	 * whether we started right after the context switch that is due
	 * according to sideband.
	 *
	 * And it isn't really necessary as long as we're not interested in
	 * detecting threads in the trace.
	 */
	errcode = ptxed_obsv_is_kernel_addr(ip, &obsv->config);
	if (errcode <= 0)
		return errcode;

	/* We do this only for ring-0 - we shouldn't get here otherwise. */
	if (!obsv->config.ring_0)
		return -pte_internal;

	/* We must maintain a decoder - we shouldn't get here otherwise. */
	if (!ptxed_have_decoder(&obsv->config.decoder))
		return -pte_internal;

	/* We must have a context - we shouldn't get here otherwise. */
	context = obsv->next_context;
	if (!context)
		return -pte_internal;

	errcode = ptxed_obsv_pevent_switch_image(obsv, context);
	if (errcode < 0)
		return errcode;

	return ptxed_obsv_pevent_clear_next_context(obsv);
}

static int ptxed_obsv_pevent_switch_context(struct ptxed_obsv_pevent *obsv,
					    struct ptxed_context *context)
{
	int errcode;

	if (!obsv || !context)
		return -pte_internal;

	/* We shouldn't get here if we don't maintain a decoder. */
	if (!ptxed_have_decoder(&obsv->config.decoder))
		return -pte_internal;

	/* This switch overwrites any previously pending switch.
	 *
	 * We may skip switches due to imprecise timing or due to
	 * re-synchronization after an error.
	 */
	errcode = ptxed_obsv_pevent_clear_next_context(obsv);
	if (errcode < 0)
		return errcode;

	/* We first need to navigate to a suitable location in the trace.
	 *
	 * Provided we have precise enough timing information, we'll be able
	 * to find the context switch location.
	 *
	 * If we're tracing ring-0, the location can be determined based on
	 * the IP.  Since the kernel is mapped into every process, any IP
	 * inside the kernel should do.
	 *
	 * If we're not tracing ring-0, the location will be determined based
	 * on the decode state.  The actual switch happens in ring-0 so tracing
	 * will be disabled when entering ring-0 in order to switch.
	 *
	 * If timing information is too coarse, we may switch too early or too
	 * late.  Both usually result in decode errors.
	 */
	if (obsv->config.ring_0) {
		/* We apply image switches immediately as long as we don't know
		 * the decode state - i.e. as long as decode has not really
		 * started, yet.
		 */
		if (obsv->state == ptds_unknown)
			return ptxed_obsv_pevent_switch_image(obsv, context);

		errcode = ptxed_context_get(context);
		if (errcode < 0)
			return errcode;

		obsv->next_context = context;
		obsv->obsv.ip.callback = ptxed_obsv_pevent_ip;

		return 0;
	} else {
		/* If we're already disabled, we can apply the image switch
		 * immediately.
		 *
		 * Let's also apply it now if we're not tracing ring-3, either,
		 * which may just mean that we don't know what we traced.
		 *
		 * If we are currently tracing and we're tracing ring-3 but
		 * not ring-0, we delay the context switch until tracing is
		 * disabled.  The state observer will take care of that.
		 */
		switch (obsv->state) {
		case ptds_enabled:
			if (obsv->config.ring_3) {
				errcode = ptxed_context_get(context);
				if (errcode < 0)
					return errcode;

				obsv->next_context = context;
				return 0;
			}

			/* Fall through. */
		case ptds_disabled:
		case ptds_unknown:
			return ptxed_obsv_pevent_switch_image(obsv, context);
		}
	}

	/* We shouldn't get here. */
	return -pte_internal;
}

static int ptxed_obsv_pevent_switch(struct ptxed_obsv_pevent *obsv,
				    uint32_t pid)
{
	struct ptxed_context *context;

	if (!obsv)
		return -pte_internal;

	/* Switch to or create a new context for @pid. */
	context = ptxed_obsv_pid_context(obsv, pid);
	if (!context)
		return -pte_nomem;

	return ptxed_obsv_pevent_switch_context(obsv, context);
}

static int ptxed_obsv_pevent_fork(struct ptxed_obsv_pevent *obsv, uint32_t pid,
				  uint32_t tid, uint32_t ppid)
{
	struct ptxed_context *context, *parent;

	if (!obsv)
		return -pte_internal;

	/* If this creates a new process and we already have a context for @pid
	 * it must be for an old process for which we ignored the exit.
	 *
	 * Let's remove it now.
	 */
	if (pid == tid)
		ptxed_context_remove_by_pid(pid, obsv->config.flags);

	/* Get the context for this process.
	 *
	 * In case of a new process, this will create a new context for it.
	 *
	 * In case of a new thread, this will return the process' context,
	 * if we have already seen another thread, or create a new context
	 * for the process in case we have not.
	 */
	context = ptxed_obsv_pid_context(obsv, pid);
	if (!context)
		return -pte_nomem;

	/* If this is just creating a new thread, we're done. */
	if (pid == ppid)
		return 0;

	/* Otherwise, let's initialize the child's image with its parent's
	 * image sections.
	 *
	 * Provided we've seen the parent.
	 */
	parent = ptxed_context_by_pid(ppid);
	if (!parent)
		return 0;

	/* Parent and child have different process identifiers - they must have
	 * different contexts, as well.
	 */
	if (parent == context)
		return -pte_internal;

	if (obsv->config.flags & ppf_log_image)
		ptxed_obsv_pevent_log_image_init(context->image, parent->image);

	return pt_image_copy(context->image, parent->image);
}

static int ptxed_obsv_pevent_exec(struct ptxed_obsv_pevent *obsv, uint32_t pid)
{
	struct ptxed_context *context;

	if (!obsv)
		return -pte_internal;

	/* We suppress logging the exit below and instead log the exec here. */
	if (obsv->config.flags & ppf_log_image) {
		context = ptxed_context_by_pid(pid);
		if (context)
			ptxed_obsv_pevent_log_image_exec(context->image);
	}

	/* Instead of replacing an existing context's image, we replace the
	 * entire context.
	 *
	 * This allows us to keep the old image around until we're ready to
	 * switch.  We might still need it to navigate to an appropriate switch
	 * location.
	 *
	 * Pass zero flags to suppress the exit log.
	 */
	ptxed_context_remove_by_pid(pid, 0);

	/* This creates a new context and a new image.
	 *
	 * This new image will already be initialized with kernel sections.
	 */
	context = ptxed_obsv_pid_context(obsv, pid);
	if (!context)
		return -pte_nomem;

	/* If we're not maintaining a decoder, we're done. */
	if (!ptxed_have_decoder(&obsv->config.decoder))
		return 0;

	/* We removed the previous context (for the same process).  Let's
	 * switch to the new one.
	 */
	return ptxed_obsv_pevent_switch_context(obsv, context);
}

static int ptxed_get_self_mmap_range(void **begin, void **end, const char *symbol)
{
	FILE *file;
	int errcode;

	if (!begin || !end || !symbol)
		return -pte_internal;

	file = fopen("/proc/self/maps", "r");
	if (!file) {
		perror("ptxed: failed to open /proc/self/maps");
		return -pte_bad_image;
	}

	errcode = -pte_bad_image;
	for (;;) {
		char buffer[256], *line, filename[64];
		int match;

		line = fgets(buffer, sizeof(buffer), file);
		if (!line)
			break;

		match = sscanf(line, "%p-%p r-xp %*x %*x:%*x %*d %63s",
			       begin, end, filename);
		if (match != 3)
			continue;

		if (strcmp(filename, symbol) != 0)
			continue;

		errcode = 0;
		break;
	}

	fclose(file);
	return errcode;
}

static int ptxed_obsv_pevent_get_vdso(const char **pfilename,
				      const struct ptxed_obsv_pevent *obsv)
{
	static char filename[] = "ptxed-vdso-XXXXXX.so";
	static int fd;
	void *begin, *end;
	ssize_t size, written;
	int errcode;

	if (!pfilename || !obsv)
		return -pte_internal;

	/* Let's see if the user provided a vdso file. */
	if (obsv->config.vdso) {
		*pfilename = obsv->config.vdso;
		return 0;
	}

	/* We approximate the VDSO by making a copy of our own.
	 *
	 * This works, at least today, as long as the trace is decoded on
	 * the same system on which it had been recorded.
	 */

	/* We cache the file. */
	if (fd) {
		*pfilename = filename;
		return 0;
	}

	errcode = ptxed_get_self_mmap_range(&begin, &end, "[vdso]");
	if (errcode < 0)
		return errcode;

	if (end < begin)
		return -pte_internal;

	size = (ssize_t) ((char *) end - (char *) begin);

	/* The VDSO shouldn't be very big.
	 *
	 * On today's systems it is two pages.  Let's allow for some growth.
	 */
	if (0x10000 < size) {
		fprintf(stderr,
			"ptxed: suspicious vdso size: begin=%p, end=%p.\n",
			begin, end);
		return -pte_bad_image;
	}

	fd = mkstemps(filename, /* suffix length = */ 3);
	if (!fd) {
		perror("ptxed: failed to create temporary file");
		return -pte_bad_image;
	}

	written = write(fd, begin, (size_t) size);
	if (written != size) {
		if (written < 0)
			perror("ptxed: error creating temporary vdso file");
		else
			fprintf(stderr, "ptxed: temporary vdso file (%s) "
				"truncated.\n", filename);
	}

	fsync(fd);

	/* Leave the file open. */

	*pfilename = filename;
	return 0;
}

static int ptxed_obsv_pevent_mmap(struct ptxed_obsv_pevent *obsv, uint32_t pid,
				  const char *filename, uint64_t offset,
				  uint64_t size, uint64_t vaddr,
				  uint16_t cpu_mode)
{
	struct pt_image_section_cache *iscache;
	struct ptxed_context *context;
	struct pt_image *image;
	char buffer[4096];
	int errcode, isid;

	if (!obsv || !filename)
		return -pte_internal;

	/* We rely on the kernel core file for ring-0 decode.
	 *
	 * Both kernel and kernel modules are modified during boot and insmod
	 * respectively.  We can't decode from the respective files on disk.
	 *
	 * Ignore kernel MMAP events so we don't overwrite useful data from
	 * kcore with useless data from binary files.
	 */
	switch (cpu_mode) {
	case PERF_RECORD_MISC_KERNEL:
		return 0;

	default:
		break;
	}

	/* Some filenames do not represent actual files on disk.  We handle
	 * some of those and ignore the rest.
	 *
	 * For kernel code we rely on the kcore file and therefore may ignore
	 * [kernel.kallsyms] filenames.
	 */
	if (filename[0] == '[') {
		/* The [vdso] file represents the vdso that is mapped into
		 * every process.
		 *
		 * We allow the user to provide the vdso file for remote
		 * decode and use our own if the user didn't provide any.
		 *
		 * This does not work when tracing 32-bit or x32 compatibility
		 * mode.
		 */
		if (strcmp(filename, "[vdso]") == 0) {
			errcode = ptxed_obsv_pevent_get_vdso(&filename, obsv);
			if (errcode < 0)
				return errcode;
		} else
			return 0;
	} else if (strcmp(filename, "//anon") == 0) {
		/* Not sure if we need to handle //anon.
		 *
		 * So far we seem to be doing OK by just ignoring them.
		 */
		return 0;
	} else if (obsv->config.sysroot) {
		int written;

		/* Prepend the sysroot to normal files. */
		written = snprintf(buffer, sizeof(buffer), "%s%s",
				   obsv->config.sysroot, filename);
		if (written < 0) {
			perror("ptxed: failed to create filename");
			return -pte_bad_image;
		}

		filename = buffer;
	}

	/* Get the context for this process. */
	context = ptxed_obsv_pid_context(obsv, pid);
	if (!context)
		return -pte_nomem;

	iscache = obsv->config.iscache;
	image = context->image;

	/* We add the new section to @pid's image.
	 *
	 * We don't really care whether we're maintaining a decoder.  If a
	 * decoder uses @pid's image, the update will be visible immediately.
	 *
	 * The new section may evict overlapping sections but those shouldn't
	 * currently be in use.  If they are, our timing information must be way
	 * off.
	 */
	if (obsv->config.flags & ppf_log_image)
		ptxed_obsv_pevent_log_image_add(image, filename, vaddr, size);

	isid = pt_iscache_add_file(iscache, filename, offset, size, vaddr);
	if (isid < 0) {
		printf("[image: error adding '%s': %s]\n", filename,
		       pt_errstr(pt_errcode(isid)));
		return isid;
	}

	return pt_image_add_cached(image, iscache, isid, NULL);
}

static int ptxed_obsv_pevent_apply(struct ptxed_obsv_pevent *obsv)
{
	if (!obsv)
		return -pte_internal;

	switch (obsv->event.type) {
	case PERF_RECORD_ITRACE_START: {
		const struct pev_record_itrace_start *itrace_start;

		/* We don't care about trace starts that are not directly
		 * connected to the trace.
		 */
		if (!ptxed_have_decoder(&obsv->config.decoder))
			return 0;

		itrace_start = obsv->event.record.itrace_start;
		if (!itrace_start)
			return -pte_internal;

		return ptxed_obsv_pevent_switch(obsv, itrace_start->pid);
	}

	case PERF_RECORD_FORK: {
		const struct pev_record_fork *fork;

		fork = obsv->event.record.fork;
		if (!fork)
			return -pte_internal;

		return ptxed_obsv_pevent_fork(obsv, fork->pid, fork->tid,
					      fork->ppid);
	}

	case PERF_RECORD_COMM: {
		const struct pev_record_comm *comm;

		comm = obsv->event.record.comm;
		if (!comm)
			return -pte_internal;

		if (!(obsv->event.misc & PERF_RECORD_MISC_COMM_EXEC))
			return 0;

		return ptxed_obsv_pevent_exec(obsv, comm->pid);
	}

	case PERF_RECORD_SWITCH: {
		uint32_t *pid;

		/* We don't care about context switches that are not directly
		 * connected to the trace.
		 */
		if (!ptxed_have_decoder(&obsv->config.decoder))
			return 0;

		/* Without a pid sample, the event is useless. */
		pid = obsv->event.sample.pid;
		if (!pid)
			return -pte_bad_config;

		/* Ignore switch out events. */
		if (obsv->event.misc & PERF_RECORD_MISC_SWITCH_OUT)
			return 0;

		return ptxed_obsv_pevent_switch(obsv, *pid);
	}

	case PERF_RECORD_SWITCH_CPU_WIDE: {
		const struct pev_record_switch_cpu_wide *switch_cpu_wide;

		/* We don't care about context switches that are not directly
		 * connected to the trace.
		 */
		if (!ptxed_have_decoder(&obsv->config.decoder))
			return 0;

		/* Let's use the next_pid payload on switch out.
		 *
		 * This way, we don't rely on the sample configuration.
		 */
		if (!(obsv->event.misc & PERF_RECORD_MISC_SWITCH_OUT))
			return 0;

		switch_cpu_wide = obsv->event.record.switch_cpu_wide;
		if (!switch_cpu_wide)
			return -pte_internal;

		return ptxed_obsv_pevent_switch(obsv,
						switch_cpu_wide->next_prev_pid);
	}

	case PERF_RECORD_MMAP: {
		const struct pev_record_mmap *mmap;
		uint16_t cpu_mode;

		cpu_mode = obsv->event.misc & PERF_RECORD_MISC_CPUMODE_MASK;

		mmap = obsv->event.record.mmap;
		if (!mmap)
			return -pte_internal;

		return ptxed_obsv_pevent_mmap(obsv, mmap->pid, mmap->filename,
					      mmap->pgoff, mmap->len,
					      mmap->addr, cpu_mode);
	}

	case PERF_RECORD_MMAP2: {
		const struct pev_record_mmap2 *mmap2;
		uint16_t cpu_mode;

		cpu_mode = obsv->event.misc & PERF_RECORD_MISC_CPUMODE_MASK;

		mmap2 = obsv->event.record.mmap2;
		if (!mmap2)
			return -pte_internal;

		return ptxed_obsv_pevent_mmap(obsv, mmap2->pid, mmap2->filename,
					      mmap2->pgoff, mmap2->len,
					      mmap2->addr, cpu_mode);
	}

	default:
		/* We should not apply unknown events. */
		return -pte_internal;

	}

	/* We should not get here. */
	return -pte_internal;
}

static int ptxed_obsv_pevent_setup(struct ptxed_obsv_pevent *obsv)
{
	struct pt_observer *ptobsv;

	if (!obsv)
		return -pte_internal;

	ptobsv = &obsv->obsv;
	for (;;) {
		struct pev_event *event;
		int size;

		event = &obsv->event;

		size = pev_read(event, obsv->pos, obsv->config.end,
				&obsv->config.pev);
		if (size < 0) {
			/* We're done when we reach the end of the sideband. */
			if (size == -pte_eos) {
				/* Unsubscribe from time-based observation. */
				ptobsv->tick.callback = NULL;
				return 0;
			}
			return size;
		}

		obsv->pos += size;

		switch (event->type) {
		case PERF_RECORD_EXIT:
			/* The kernel generates EXIT events when tracing setuid
			 * processes without actually stopping to trace that
			 * process.
			 *
			 * In that case, we see an EXIT somewhere in the middle
			 * of the trace.
			 *
			 * Since we cannot distinguish those false EXITs from
			 * real EXITs, we have to ignore EXITs.  This will leak
			 * the process image.
			 */
		default:
			/* Ignore unknown or irrelevent events. */
			break;

		case PERF_RECORD_LOST:
			/* Warn about losses.
			 *
			 * We put the warning into the output.  It is quite
			 * likely that we will run into a decode error shortly
			 * after (or ran into it already); this warning may help
			 * explain explain it.
			 */
			printf("[warning: lost perf event records]\n");
			break;

		case PERF_RECORD_AUX: {
			const struct pev_record_aux *aux;

			/* Warn about losses.
			 *
			 * Trace losses are only relevant for primary sideband
			 * files.
			 */
			if (!ptxed_have_decoder(&obsv->config.decoder))
				break;

			aux = obsv->event.record.aux;
			if (!aux)
				return -pte_internal;

			if (aux->flags & PERF_AUX_FLAG_TRUNCATED)
				printf("[warning: lost trace]\n");

			break;
		}

		case PERF_RECORD_ITRACE_START:
		case PERF_RECORD_FORK:
		case PERF_RECORD_COMM:
		case PERF_RECORD_SWITCH:
		case PERF_RECORD_SWITCH_CPU_WIDE:
		case PERF_RECORD_MMAP:
		case PERF_RECORD_MMAP2:
			/* We do need a timestamp. */
			if (!obsv->event.sample.time)
				return -pte_bad_config;

			ptobsv->tick.limit = event->sample.tsc -
				obsv->config.tsc_offset;

			return 0;
		}
	}
}

static int ptxed_obsv_pevent_state(struct pt_observer *self,
				   enum pt_decode_state state)
{
	struct ptxed_obsv_pevent *obsv;
	struct ptxed_context *context;
	int errcode;

	if (!self)
		return -pte_internal;

	obsv = self->context;
	if (!obsv)
		return -pte_internal;

	obsv->state = state;

	/* If there is no pending context, we're done. */
	context = obsv->next_context;
	if (!context)
		return 0;

	/* Check if we should apply a pending context switch.
	 *
	 * The actual context switch happens in ring-0.  When tracing ring-0, we
	 * can apply the switch somewhere inside the kernel, determined by the
	 * IP.
	 *
	 * When not tracing ring-0, we apply it when tracing is disabled as we
	 * enter ring-0.
	 *
	 * There's a special case when the decoder was re-synchronized after a
	 * decode error.  Any pending context switch lies almost certainly in
	 * the past so, with respect to the current trace offset, it belongs to
	 * the initial setup.  The re-synchronization is indicated by a
	 * temporary switch to the unknown decode state.
	 */
	switch (state) {
	case ptds_enabled:
		return 0;

	case ptds_disabled:
		if (obsv->config.ring_0)
			return 0;

		/* Fall through. */
	case ptds_unknown:
		errcode = ptxed_obsv_pevent_switch_image(obsv, context);
		if (errcode < 0)
			return errcode;

		return ptxed_obsv_pevent_clear_next_context(obsv);
	}

	/* We shouldn't get here. */
	return -pte_internal;
}

static int ptxed_obsv_pevent_tick(struct pt_observer *self, uint64_t tsc,
				  uint32_t lost_mtc, uint32_t lost_cyc)
{
	struct ptxed_obsv_pevent *obsv;

	(void) lost_mtc;
	(void) lost_cyc;

	if (!self)
		return -pte_internal;

	obsv = self->context;
	if (!obsv)
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

		errcode = ptxed_obsv_pevent_apply(obsv);
		if (errcode < 0) {
			/* If we fail with an error, we're stuck.
			 *
			 * Report the error and ignore it.
			 */
			printf("[warning: dropping %s (%x) event (%d): %s]\n",
			       ptxed_pevent_name(obsv->event.type),
			       obsv->event.type, errcode,
			       pt_errstr(pt_errcode(errcode)));
		}

		errcode = ptxed_obsv_pevent_setup(obsv);
		if (errcode < 0)
			return errcode;

		if (!obsv->obsv.tick.callback || (tsc < obsv->obsv.tick.limit))
			break;
	}

	return 0;
}

struct pt_observer *
ptxed_obsv_pevent_alloc(const struct ptxed_pevent_config *config)
{
	struct ptxed_obsv_pevent *obsv;
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

	ptobsv->tick.callback = ptxed_obsv_pevent_tick;

	/* If we're maintaining a decoder, we need to observe decode state
	 * changes in order to find the correct location for applying context
	 * switch sideband events.
	 */
	if (ptxed_have_decoder(&config->decoder))
		ptobsv->state.callback = ptxed_obsv_pevent_state;

	errcode = ptxed_obsv_pevent_setup(obsv);
	if (errcode < 0) {
		free(obsv);

		return NULL;
	}

	/* We don't expect overflows. */
	ucount += 1;

	return ptobsv;
}

void ptxed_obsv_pevent_free(struct pt_observer *ptobsv)
{
	struct ptxed_obsv_pevent *obsv;

	if (!ptobsv)
		return;

	obsv = ptobsv->context;

	free(obsv->config.begin);
	free(obsv);

	if (!ucount)
		fprintf(stderr, "ptxed: internal error - "
			"pevent observer alloc/free mismatch");
	else {
		ucount -= 1;

		if (!ucount) {
			pt_image_free(kernel_image);
			kernel_image = NULL;

			ptxed_context_clear();
		}
	}
}

int ptxed_obsv_pevent_set_decoder(struct pt_observer *ptobsv,
				  const struct ptxed_decoder *decoder)
{
	struct ptxed_obsv_pevent *obsv;

	if (!ptobsv || !decoder)
		return -pte_internal;

	obsv = ptobsv->context;
	if (!obsv)
		return -pte_internal;

	obsv->config.decoder = *decoder;

	/* Update the state observer.
	 *
	 * We only need to track the decode state if we're maintaining
	 * a decoder.
	 */
	ptobsv->state.callback = ptxed_have_decoder(decoder)
		? ptxed_obsv_pevent_state : NULL;

	return 0;
}

int ptxed_obsv_pevent_kcore(struct pt_image_section_cache *iscache,
			    const char *filename, uint64_t base,
			    const char *prog, int verbose)
{
#if defined(FEATURE_ELF)
	/* Last --pevent:kcore wins. */
	if (kernel_image)
		pt_image_free(kernel_image);

	kernel_image = pt_image_alloc("img-kernel");
	if (!kernel_image)
		return -pte_nomem;

	return load_elf(iscache, kernel_image, filename, base, prog, verbose);
#else /* defined(FEATURE_ELF) */

	(void) base;
	(void) verbose;

	fprintf(stderr, "%s: unable to load %s.  ELF support not enabled.\n",
		prog, filename);

	return -pte_not_supported;

#endif /* defined(FEATURE_ELF) */
}
