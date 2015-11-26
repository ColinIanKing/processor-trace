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

#include "ptunit.h"

#include "pt_observer.h"


/* The test observer context. */
struct obsv_context {
	/* The number of calls. */
	uint64_t calls;

	/* The tick configuration - not all fields are used by all callbacks. */
	struct {
		/* The last time. */
		uint64_t last;

		/* The lost MTCs and CYCs last time. */
		uint32_t lost_mtc;
		uint32_t lost_cyc;

		/* The updated callback. */
		int (*callback)(struct pt_observer *self, uint64_t tsc,
				uint32_t lost_mtc, uint32_t lost_cyc);

		/* The updated limit. */
		uint64_t limit;
	} tick;

	/* The state configuration - not all fields are used by all callbacks. */
	struct {
		/* The last state. */
		enum pt_decode_state last;

		/* The updated callback. */
		int (*callback)(struct pt_observer *, enum pt_decode_state);
	} state;

	/* The ip configuration - not all fields are used by all callbacks. */
	struct {
		/* The last IP. */
		uint64_t last;

		/* The updated callback. */
		int (*callback)(struct pt_observer *, uint64_t);
	} ip;
};

/* Update the observer state based on its test observer context. */
static int obsv_update(struct pt_observer *self)
{
	struct obsv_context *context;

	if (!self)
		return -pte_internal;

	context = self->context;
	if (!context)
		return -pte_internal;

	self->tick.callback = context->tick.callback;
	self->tick.limit = context->tick.limit;
	self->state.callback = context->state.callback;
	self->ip.callback = context->ip.callback;

	return 0;
}

/* A test tick callback that remembers the last time. */
static int obsv_tick(struct pt_observer *self, uint64_t tsc,
		     uint32_t lost_mtc, uint32_t lost_cyc)
{
	struct obsv_context *context;

	if (!self)
		return -pte_internal;

	context = self->context;
	if (!context)
		return -pte_internal;

	context->calls += 1;
	context->tick.last = tsc;
	context->tick.lost_mtc = lost_mtc;
	context->tick.lost_cyc = lost_cyc;

	return 0;
}

/* A test tick callback that fails. */
static int obsv_tick_fail(struct pt_observer *self, uint64_t tsc,
			  uint32_t lost_mtc, uint32_t lost_cyc)
{
	int errcode;

	errcode = obsv_tick(self, tsc, lost_mtc, lost_cyc);
	if (errcode < 0)
		return errcode;

	return -pte_bad_config;
}

/* A test tick callback that updates the configuration. */
static int obsv_tick_update(struct pt_observer *self, uint64_t tsc,
			    uint32_t lost_mtc, uint32_t lost_cyc)
{
	int errcode;

	errcode = obsv_tick(self, tsc, lost_mtc, lost_cyc);
	if (errcode < 0)
		return errcode;

	return obsv_update(self);
}

/* A test state callback that remembers the decode state. */
static int obsv_state(struct pt_observer *self, enum pt_decode_state state)
{
	struct obsv_context *context;

	if (!self)
		return -pte_internal;

	context = self->context;
	if (!context)
		return -pte_internal;

	context->calls += 1;
	context->state.last = state;

	return 0;
}

/* A test state callback that fails. */
static int obsv_state_fail(struct pt_observer *self, enum pt_decode_state state)
{
	int errcode;

	errcode = obsv_state(self, state);
	if (errcode < 0)
		return errcode;

	return -pte_bad_config;
}

/* A test state callback that updates the configuration. */
static int obsv_state_update(struct pt_observer *self,
			     enum pt_decode_state state)
{
	int errcode;

	errcode = obsv_state(self, state);
	if (errcode < 0)
		return errcode;

	return obsv_update(self);
}

/* A test ip callback that remembers the last IP. */
static int obsv_ip(struct pt_observer *self, uint64_t ip)
{
	struct obsv_context *context;

	if (!self)
		return -pte_internal;

	context = self->context;
	if (!context)
		return -pte_internal;

	context->calls += 1;
	context->ip.last = ip;

	return 0;
}

/* A test ip callback that fails. */
static int obsv_ip_fail(struct pt_observer *self, uint64_t ip)
{
	int errcode;

	errcode = obsv_ip(self, ip);
	if (errcode < 0)
		return errcode;

	return -pte_bad_config;
}

/* A test ip callback that updates the configuration. */
static int obsv_ip_update(struct pt_observer *self, uint64_t ip)
{
	int errcode;

	errcode = obsv_ip(self, ip);
	if (errcode < 0)
		return errcode;

	return obsv_update(self);
}

/* A test fixture providing an observer collection and initialized observers. */
struct obsv_fixture {
	/* An observer collection. */
	struct pt_obsv_collection obsvc;

	/* Initialized observers. */
	struct pt_observer obsv[2];

	/* The contexts for the above observers. */
	struct obsv_context context[2];

	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct obsv_fixture *);
	struct ptunit_result (*fini)(struct obsv_fixture *);
};

static struct ptunit_result ofix_init(struct obsv_fixture *ofix)
{
	pt_obsvc_init(&ofix->obsvc);

	pt_obsv_init(&ofix->obsv[0]);
	pt_obsv_init(&ofix->obsv[1]);

	memset(&ofix->context[0], 0, sizeof(ofix->context[0]));
	memset(&ofix->context[1], 0, sizeof(ofix->context[1]));

	ofix->obsv[0].context = &ofix->context[0];
	ofix->obsv[1].context = &ofix->context[1];

	return ptu_passed();
}

static struct ptunit_result ofix_fini(struct obsv_fixture *ofix)
{
	pt_obsvc_fini(&ofix->obsvc);

	return ptu_passed();
}

static struct ptunit_result ptu_obsvc_add(struct pt_obsv_collection *obsvc,
					  struct pt_observer *obsv)
{
	int errcode;

	errcode = pt_obsvc_add(obsvc, obsv);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result obsv_init(void)
{
	struct pt_observer obsv;

	pt_obsv_init(&obsv);

	ptu_uint_eq(obsv.size, sizeof(obsv));
	ptu_null(obsv.context);
	ptu_null(obsv.tick.next);
	ptu_null((void *)(uintptr_t) obsv.tick.callback);
	ptu_uint_eq(obsv.tick.limit, 0ull);
	ptu_null((void *)(uintptr_t) obsv.state.callback);
	ptu_null((void *)(uintptr_t) obsv.ip.callback);

	return ptu_passed();
}

static struct ptunit_result obsvc_init_null(void)
{
	pt_obsvc_init(NULL);

	return ptu_passed();
}

static struct ptunit_result obsvc_fini_null(void)
{
	pt_obsvc_fini(NULL);

	return ptu_passed();
}

static struct ptunit_result obsvc_init(struct obsv_fixture *ofix)
{
	ptu_null(ofix->obsvc.tick.obsv);
	ptu_uint_eq(ofix->obsvc.tick.limit, UINT64_MAX);
	ptu_null(ofix->obsvc.state.obsv);

	return ptu_passed();
}

static struct ptunit_result obsvc_add_null(struct obsv_fixture *ofix)
{
	int errcode;

	errcode = pt_obsvc_add(&ofix->obsvc, NULL);
	ptu_int_eq(errcode, -pte_invalid);

	errcode = pt_obsvc_add(NULL, &ofix->obsv[0]);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result obsvc_add_none(struct obsv_fixture *ofix)
{
	int errcode;

	errcode = pt_obsvc_add(&ofix->obsvc, &ofix->obsv[0]);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result obsvc_add_twice(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick;
	ofix->obsv[1].state.callback = obsv_state;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);
	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[1]);

	errcode = pt_obsvc_add(&ofix->obsvc, &ofix->obsv[0]);
	ptu_int_eq(errcode, -pte_invalid);

	errcode = pt_obsvc_add(&ofix->obsvc, &ofix->obsv[1]);
	ptu_int_eq(errcode, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick;
	ofix->obsv[0].tick.limit = 2ull;

	ofix->obsv[1].tick.callback = obsv_tick;
	ofix->obsv[1].tick.limit = 3ull;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);
	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[1]);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 1, 1);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);
	ptu_uint_eq(ofix->context[1].calls, 0ull);
	ptu_uint_eq(ofix->context[0].tick.last, 0ull);
	ptu_uint_eq(ofix->context[1].tick.last, 0ull);
	ptu_uint_eq(ofix->context[0].tick.lost_mtc, 0);
	ptu_uint_eq(ofix->context[1].tick.lost_mtc, 0);
	ptu_uint_eq(ofix->context[0].tick.lost_cyc, 0);
	ptu_uint_eq(ofix->context[1].tick.lost_cyc, 0);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 2, 1);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[1].calls, 0ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);
	ptu_uint_eq(ofix->context[1].tick.last, 0ull);
	ptu_uint_eq(ofix->context[0].tick.lost_mtc, 2);
	ptu_uint_eq(ofix->context[1].tick.lost_mtc, 0);
	ptu_uint_eq(ofix->context[0].tick.lost_cyc, 1);
	ptu_uint_eq(ofix->context[1].tick.lost_cyc, 0);

	errcode = pt_obsvc_tick(&ofix->obsvc, 4ull, 1, 2);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[1].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 4ull);
	ptu_uint_eq(ofix->context[1].tick.last, 4ull);
	ptu_uint_eq(ofix->context[0].tick.lost_mtc, 1);
	ptu_uint_eq(ofix->context[1].tick.lost_mtc, 1);
	ptu_uint_eq(ofix->context[0].tick.lost_cyc, 2);
	ptu_uint_eq(ofix->context[1].tick.lost_cyc, 2);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick_fail(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_fail;
	ofix->obsv[1].tick.callback = obsv_tick_fail;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);
	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[1]);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, -pte_bad_config);
	ptu_uint_eq(ofix->context[0].calls + ofix->context[1].calls, 1ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick_postpone_one(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->obsv[0].tick.limit = 2ull;
	ofix->context[0].tick.callback = obsv_tick;
	ofix->context[0].tick.limit = 3ull;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	ptu_uint_eq(ofix->obsvc.tick.limit, 2ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);

	ptu_uint_eq(ofix->obsvc.tick.limit, 3ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 4ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 4ull);

	return ptu_passed();
}

static struct ptunit_result
obsvc_tick_postpone_ordered(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->obsv[0].tick.limit = 2ull;
	ofix->context[0].tick.callback = obsv_tick;
	ofix->context[0].tick.limit = 3ull;

	ofix->obsv[1].tick.callback = obsv_tick;
	ofix->obsv[1].tick.limit = 4ull;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);
	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[1]);

	ptu_uint_eq(ofix->obsvc.tick.limit, 2ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[1].calls, 0ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);
	ptu_uint_eq(ofix->context[1].tick.last, 0ull);

	ptu_uint_eq(ofix->obsvc.tick.limit, 3ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 3ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[1].calls, 0ull);
	ptu_uint_eq(ofix->context[0].tick.last, 3ull);
	ptu_uint_eq(ofix->context[1].tick.last, 0ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 4ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_uint_eq(ofix->context[1].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 4ull);
	ptu_uint_eq(ofix->context[1].tick.last, 4ull);

	return ptu_passed();
}

static struct ptunit_result
obsvc_tick_postpone_interleaved(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->obsv[0].tick.limit = 2ull;
	ofix->context[0].tick.callback = obsv_tick;
	ofix->context[0].tick.limit = 5ull;

	ofix->obsv[1].tick.callback = obsv_tick;
	ofix->obsv[1].tick.limit = 3ull;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);
	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[1]);

	ptu_uint_eq(ofix->obsvc.tick.limit, 2ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[1].calls, 0ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);
	ptu_uint_eq(ofix->context[1].tick.last, 0ull);

	ptu_uint_eq(ofix->obsvc.tick.limit, 3ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 4ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[1].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);
	ptu_uint_eq(ofix->context[1].tick.last, 4ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 5ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[1].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 5ull);
	ptu_uint_eq(ofix->context[1].tick.last, 5ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick_twice(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->obsv[0].tick.limit = 1ull;
	ofix->context[0].tick.callback = obsv_tick;
	ofix->context[0].tick.limit = 3ull;

	ofix->obsv[1].tick.callback = obsv_tick_update;
	ofix->obsv[1].tick.limit = 2ull;
	ofix->context[1].tick.callback = obsv_tick;
	ofix->context[1].tick.limit = 3ull;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);
	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[1]);

	ptu_uint_eq(ofix->obsvc.tick.limit, 1ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 3ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[1].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 3ull);
	ptu_uint_eq(ofix->context[1].tick.last, 3ull);

	ptu_uint_eq(ofix->obsvc.tick.limit, 3ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick_remove(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->obsv[0].tick.limit = 2ull;
	ofix->context[0].tick.callback = NULL;
	ofix->context[0].tick.limit = 2ull;

	ofix->obsv[1].tick.callback = obsv_tick;
	ofix->obsv[1].tick.limit = 3ull;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);
	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[1]);

	ptu_uint_eq(ofix->obsvc.tick.limit, 2ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[1].calls, 0ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);
	ptu_uint_eq(ofix->context[1].tick.last, 0ull);

	ptu_uint_eq(ofix->obsvc.tick.limit, 3ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 3ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[1].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);
	ptu_uint_eq(ofix->context[1].tick.last, 3ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick_update(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->obsv[0].tick.limit = 3;
	ofix->context[0].tick.callback = obsv_tick_update;
	ofix->context[0].tick.limit = 0;

	ofix->obsv[1].tick.callback = obsv_tick;
	ofix->obsv[1].tick.limit = 2;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);
	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[1]);

	ptu_uint_eq(ofix->obsvc.tick.limit, 2ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 3ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[1].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 3ull);
	ptu_uint_eq(ofix->context[1].tick.last, 3ull);

	ptu_uint_eq(ofix->obsvc.tick.limit, 0ull);

	ofix->context[0].tick.limit = 5;

	errcode = pt_obsvc_tick(&ofix->obsvc, 4ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[1].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 4ull);
	ptu_uint_eq(ofix->context[1].tick.last, 4ull);

	ptu_uint_eq(ofix->obsvc.tick.limit, 2ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick_add_state(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->context[0].tick.callback = obsv_tick_update;
	ofix->context[0].state.callback = obsv_state;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_enabled);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick_remove_state(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->obsv[0].state.callback = obsv_state;
	ofix->context[0].tick.callback = obsv_tick;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick_move_to_state(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->context[0].state.callback = obsv_state;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_enabled);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick_add_ip(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->context[0].tick.callback = obsv_tick_update;
	ofix->context[0].ip.callback = obsv_ip;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa001ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick_remove_ip(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->obsv[0].ip.callback = obsv_ip;
	ofix->context[0].tick.callback = obsv_tick;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_tick_move_to_ip(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].tick.callback = obsv_tick_update;
	ofix->context[0].ip.callback = obsv_ip;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa001ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_state(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].state.callback = obsv_state;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_enabled);

	return ptu_passed();
}

static struct ptunit_result obsvc_state_fail(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].state.callback = obsv_state_fail;
	ofix->obsv[1].state.callback = obsv_state_fail;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);
	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[1]);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, -pte_bad_config);
	ptu_uint_eq(ofix->context[0].calls + ofix->context[1].calls, 1ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_state_remove(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].state.callback = obsv_state_update;
	ofix->context[0].state.callback = NULL;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	return ptu_passed();
}

static struct ptunit_result obsvc_state_add_tick(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].state.callback = obsv_state_update;
	ofix->context[0].state.callback = obsv_state;
	ofix->context[0].tick.callback = obsv_tick;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_enabled);

	return ptu_passed();
}

static struct ptunit_result obsvc_state_update_tick(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].state.callback = obsv_state_update;
	ofix->obsv[0].tick.callback = obsv_tick;
	ofix->context[0].state.callback = obsv_state;
	ofix->context[0].tick.callback = obsv_tick;
	ofix->context[0].tick.limit = 3ull;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_enabled);

	errcode = pt_obsvc_tick(&ofix->obsvc, 3ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 4ull);
	ptu_uint_eq(ofix->context[0].tick.last, 3ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_state_remove_tick(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].state.callback = obsv_state_update;
	ofix->obsv[0].tick.callback = obsv_tick;
	ofix->context[0].state.callback = obsv_state;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_enabled);

	return ptu_passed();
}

static struct ptunit_result obsvc_state_move_to_tick(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].state.callback = obsv_state_update;
	ofix->context[0].tick.callback = obsv_tick;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	return ptu_passed();
}

static struct ptunit_result obsvc_state_add_ip(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].state.callback = obsv_state_update;
	ofix->context[0].state.callback = obsv_state;
	ofix->context[0].ip.callback = obsv_ip;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].ip.last, 0xa001ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_enabled);

	return ptu_passed();
}

static struct ptunit_result obsvc_state_remove_ip(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].state.callback = obsv_state_update;
	ofix->obsv[0].ip.callback = obsv_ip;
	ofix->context[0].state.callback = obsv_state;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_enabled);

	return ptu_passed();
}

static struct ptunit_result obsvc_state_move_to_ip(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].state.callback = obsv_state_update;
	ofix->context[0].ip.callback = obsv_ip;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].ip.last, 0xa001ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].state.last, ptds_disabled);

	return ptu_passed();
}

static struct ptunit_result obsvc_ip(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].ip.callback = obsv_ip;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa001ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_ip_fail(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].ip.callback = obsv_ip_fail;
	ofix->obsv[1].ip.callback = obsv_ip_fail;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);
	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[1]);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, -pte_bad_config);
	ptu_uint_eq(ofix->context[0].calls + ofix->context[1].calls, 1ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_ip_remove(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].ip.callback = obsv_ip_update;
	ofix->context[0].ip.callback = NULL;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_ip_add_tick(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].ip.callback = obsv_ip_update;
	ofix->context[0].ip.callback = obsv_ip;
	ofix->context[0].tick.callback = obsv_tick;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa001ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_ip_update_tick(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].ip.callback = obsv_ip_update;
	ofix->obsv[0].tick.callback = obsv_tick;
	ofix->context[0].ip.callback = obsv_ip;
	ofix->context[0].tick.callback = obsv_tick;
	ofix->context[0].tick.limit = 3ull;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa001ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 3ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 4ull);
	ptu_uint_eq(ofix->context[0].tick.last, 3ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_ip_remove_tick(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].ip.callback = obsv_ip_update;
	ofix->obsv[0].tick.callback = obsv_tick;
	ofix->context[0].ip.callback = obsv_ip;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 1ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa001ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_ip_move_to_tick(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].ip.callback = obsv_ip_update;
	ofix->context[0].tick.callback = obsv_tick;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_tick(&ofix->obsvc, 1ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_tick(&ofix->obsvc, 2ull, 0, 0);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].tick.last, 2ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_ip_add_state(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].ip.callback = obsv_ip_update;
	ofix->context[0].ip.callback = obsv_ip;
	ofix->context[0].state.callback = obsv_state;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].state.last, ptds_enabled);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa001ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_ip_remove_state(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].ip.callback = obsv_ip_update;
	ofix->obsv[0].state.callback = obsv_state;
	ofix->context[0].ip.callback = obsv_ip;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_uint_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].state.last, ptds_disabled);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 3ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa001ull);

	return ptu_passed();
}

static struct ptunit_result obsvc_ip_move_to_state(struct obsv_fixture *ofix)
{
	int errcode;

	ofix->obsv[0].ip.callback = obsv_ip_update;
	ofix->context[0].state.callback = obsv_state;

	ptu_check(ptu_obsvc_add, &ofix->obsvc, &ofix->obsv[0]);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_disabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 0ull);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa000ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 1ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	errcode = pt_obsvc_state(&ofix->obsvc, ptds_enabled);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_uint_eq(ofix->context[0].state.last, ptds_enabled);

	errcode = pt_obsvc_ip(&ofix->obsvc, 0xa001ull);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(ofix->context[0].calls, 2ull);
	ptu_int_eq(ofix->context[0].ip.last, 0xa000ull);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct obsv_fixture ofix;
	struct ptunit_suite suite;

	ofix.init = ofix_init;
	ofix.fini = ofix_fini;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, obsv_init);

	ptu_run(suite, obsvc_init_null);
	ptu_run(suite, obsvc_fini_null);

	ptu_run_f(suite, obsvc_init, ofix);

	ptu_run_f(suite, obsvc_add_null, ofix);
	ptu_run_f(suite, obsvc_add_none, ofix);
	ptu_run_f(suite, obsvc_add_twice, ofix);

	ptu_run_f(suite, obsvc_tick, ofix);
	ptu_run_f(suite, obsvc_tick_fail, ofix);
	ptu_run_f(suite, obsvc_tick_postpone_one, ofix);
	ptu_run_f(suite, obsvc_tick_postpone_ordered, ofix);
	ptu_run_f(suite, obsvc_tick_postpone_interleaved, ofix);
	ptu_run_f(suite, obsvc_tick_twice, ofix);
	ptu_run_f(suite, obsvc_tick_remove, ofix);
	ptu_run_f(suite, obsvc_tick_update, ofix);
	ptu_run_f(suite, obsvc_tick_add_state, ofix);
	ptu_run_f(suite, obsvc_tick_remove_state, ofix);
	ptu_run_f(suite, obsvc_tick_move_to_state, ofix);
	ptu_run_f(suite, obsvc_tick_add_ip, ofix);
	ptu_run_f(suite, obsvc_tick_remove_ip, ofix);
	ptu_run_f(suite, obsvc_tick_move_to_ip, ofix);

	ptu_run_f(suite, obsvc_state, ofix);
	ptu_run_f(suite, obsvc_state_fail, ofix);
	ptu_run_f(suite, obsvc_state_remove, ofix);
	ptu_run_f(suite, obsvc_state_add_tick, ofix);
	ptu_run_f(suite, obsvc_state_remove_tick, ofix);
	ptu_run_f(suite, obsvc_state_update_tick, ofix);
	ptu_run_f(suite, obsvc_state_move_to_tick, ofix);
	ptu_run_f(suite, obsvc_state_add_ip, ofix);
	ptu_run_f(suite, obsvc_state_remove_ip, ofix);
	ptu_run_f(suite, obsvc_state_move_to_ip, ofix);

	ptu_run_f(suite, obsvc_ip, ofix);
	ptu_run_f(suite, obsvc_ip_fail, ofix);
	ptu_run_f(suite, obsvc_ip_remove, ofix);
	ptu_run_f(suite, obsvc_ip_add_tick, ofix);
	ptu_run_f(suite, obsvc_ip_remove_tick, ofix);
	ptu_run_f(suite, obsvc_ip_update_tick, ofix);
	ptu_run_f(suite, obsvc_ip_move_to_tick, ofix);
	ptu_run_f(suite, obsvc_ip_add_state, ofix);
	ptu_run_f(suite, obsvc_ip_remove_state, ofix);
	ptu_run_f(suite, obsvc_ip_move_to_state, ofix);

	ptunit_report(&suite);
	return suite.nr_fails;
}
