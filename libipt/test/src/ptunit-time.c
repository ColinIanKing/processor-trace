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

#include "pt_time.h"
#include "pt_observer.h"

#include "intel-pt.h"

#include "ptunit.h"


/* A time unit test fixture. */

struct time_fixture {
	/* The configuration to use. */
	struct pt_config config;

	/* The calibration to use. */
	struct pt_time_cal tcal;

	/* The time struct to update. */
	struct pt_time time;

	/* A test observer. */
	struct pt_observer obsv;

	/* An obserer collection. */
	struct pt_obsv_collection obsvc;

	/* The number of observer calls. */
	uint32_t obsv_calls;

	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct time_fixture *);
	struct ptunit_result (*fini)(struct time_fixture *);
};

static struct ptunit_result tfix_init(struct time_fixture *tfix)
{
	memset(&tfix->config, 0, sizeof(tfix->config));
	tfix->config.size = sizeof(tfix->config);
	tfix->config.cpuid_0x15_eax = 2;
	tfix->config.cpuid_0x15_ebx = 1;
	tfix->config.mtc_freq = 4;

	pt_tcal_init(&tfix->tcal);
	pt_tcal_set_fcr(&tfix->tcal, 0x2ull << pt_tcal_fcr_shr);

	memset(&tfix->obsvc, 0xcd, sizeof(tfix->obsvc));
	memset(&tfix->obsv, 0xcd, sizeof(tfix->obsv));

	tfix->obsv_calls = 0;

	pt_time_init(&tfix->time);

	return ptu_passed();
}

static struct ptunit_result tfix_fini(struct time_fixture *tfix)
{
	pt_time_fini(&tfix->time);

	return ptu_passed();
}

static int obsv_tick(struct pt_observer *obsv, uint64_t tsc, uint32_t lost_mtc,
		     uint32_t lost_cyc)
{
	struct time_fixture *tfix;
	uint64_t exp_tsc;
	uint32_t exp_lost_mtc, exp_lost_cyc;
	int errcode;

	if (!obsv)
		return -pte_invalid;

	tfix = obsv->context;
	if (!tfix)
		return -pte_invalid;

	tfix->obsv_calls += 1;

	errcode = pt_time_query_tsc(&exp_tsc, &exp_lost_mtc, &exp_lost_cyc,
				    &tfix->time);
	if (errcode < 0)
		return -pte_internal;

	if (exp_tsc != tsc)
		return -pte_internal;

	if (exp_lost_mtc != lost_mtc)
		return -pte_internal;

	if (exp_lost_cyc != lost_cyc)
		return -pte_internal;

	return 0;
}

static struct ptunit_result tfix_obsv_init(struct time_fixture *tfix)
{
	int errcode;

	ptu_check(tfix_init, tfix);

	pt_obsv_init(&tfix->obsv);
	pt_obsvc_init(&tfix->obsvc);

	tfix->obsv.context = tfix;
	tfix->obsv.tick.callback = obsv_tick;

	errcode = pt_obsvc_add(&tfix->obsvc, &tfix->obsv);
	ptu_int_eq(errcode, 0);

	errcode = pt_time_attach_obsvc(&tfix->time, &tfix->obsvc);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result tfix_obsv_fini(struct time_fixture *tfix)
{
	pt_obsvc_fini(&tfix->obsvc);

	ptu_check(tfix_fini, tfix);

	return ptu_passed();
}

static struct ptunit_result tsc_null(struct time_fixture *tfix)
{
	struct pt_packet_tsc packet;
	int errcode;

	errcode = pt_time_update_tsc(NULL, &packet, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_time_update_tsc(&tfix->time, NULL, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result cbr_null(struct time_fixture *tfix)
{
	struct pt_packet_cbr packet;
	int errcode;

	errcode = pt_time_update_cbr(NULL, &packet, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_time_update_cbr(&tfix->time, NULL, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result tma_null(struct time_fixture *tfix)
{
	struct pt_packet_tma packet;
	int errcode;

	errcode = pt_time_update_tma(NULL, &packet, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_time_update_tma(&tfix->time, NULL, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_time_update_tma(&tfix->time, &packet, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result mtc_null(struct time_fixture *tfix)
{
	struct pt_packet_mtc packet;
	int errcode;

	errcode = pt_time_update_mtc(NULL, &packet, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_time_update_mtc(&tfix->time, NULL, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_time_update_mtc(&tfix->time, &packet, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result cyc_null(struct time_fixture *tfix)
{
	struct pt_packet_cyc packet;
	int errcode;

	errcode = pt_time_update_cyc(NULL, &packet, &tfix->config, 0ull);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_time_update_cyc(&tfix->time, NULL, &tfix->config, 0ull);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_time_update_cyc(&tfix->time, &packet, NULL, 0ull);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result query_tsc_null(struct time_fixture *tfix)
{
	uint64_t tsc;
	int errcode;

	errcode = pt_time_query_tsc(NULL, NULL, NULL, &tfix->time);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_time_query_tsc(&tsc, NULL, NULL, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result query_tsc_none(struct time_fixture *tfix)
{
	uint64_t tsc;
	int errcode;

	errcode = pt_time_query_tsc(&tsc, NULL, NULL, &tfix->time);
	ptu_int_eq(errcode, -pte_no_time);

	return ptu_passed();
}

static struct ptunit_result query_cbr_null(struct time_fixture *tfix)
{
	uint32_t cbr;
	int errcode;

	errcode = pt_time_query_cbr(NULL, &tfix->time);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_time_query_cbr(&cbr, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result query_cbr_none(struct time_fixture *tfix)
{
	uint32_t cbr;
	int errcode;

	errcode = pt_time_query_cbr(&cbr, &tfix->time);
	ptu_int_eq(errcode, -pte_no_cbr);

	return ptu_passed();
}

static struct ptunit_result tcal_cbr_null(struct time_fixture *tfix)
{
	struct pt_packet_cbr packet;
	int errcode;

	errcode = pt_tcal_update_cbr(NULL, &packet, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result tcal_mtc_null(struct time_fixture *tfix)
{
	struct pt_packet_mtc packet;
	int errcode;

	errcode = pt_tcal_update_mtc(NULL, &packet, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_tcal_update_mtc(&tfix->tcal, NULL, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_tcal_update_mtc(&tfix->tcal, &packet, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result tcal_cyc_null(struct time_fixture *tfix)
{
	struct pt_packet_cyc packet;
	int errcode;

	errcode = pt_tcal_update_cyc(NULL, &packet, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_tcal_update_cyc(&tfix->tcal, NULL, &tfix->config);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result tsc(struct time_fixture *tfix, uint32_t obsv_calls)
{
	struct pt_packet_tsc packet;
	uint64_t tsc;
	uint32_t lost_mtc, lost_cyc;
	int errcode;

	packet.tsc = 0xdedededeull;

	errcode = pt_time_update_tsc(&tfix->time, &packet, &tfix->config);
	ptu_int_eq(errcode, 0);

	errcode = pt_time_query_tsc(&tsc, &lost_mtc, &lost_cyc, &tfix->time);
	ptu_int_eq(errcode, 0);

	ptu_uint_eq(tsc, 0xdedededeull);
	ptu_uint_eq(lost_mtc, 0);
	ptu_uint_eq(lost_cyc, 0);

	ptu_uint_eq(tfix->obsv_calls, obsv_calls);

	return ptu_passed();
}

static struct ptunit_result cbr(struct time_fixture *tfix, uint32_t obsv_calls)
{
	struct pt_packet_cbr packet;
	uint32_t cbr;
	int errcode;

	packet.ratio = 0x38;

	errcode = pt_time_update_cbr(&tfix->time, &packet, &tfix->config);
	ptu_int_eq(errcode, 0);

	errcode = pt_time_query_cbr(&cbr, &tfix->time);
	ptu_int_eq(errcode, 0);

	ptu_uint_eq(cbr, 0x38);

	ptu_uint_eq(tfix->obsv_calls, obsv_calls);

	return ptu_passed();
}

static struct ptunit_result tma(struct time_fixture *tfix, uint32_t obsv_calls)
{
	struct pt_packet_tma packet;
	int errcode;

	packet.ctc = 0xdc;
	packet.fc = 0xf;

	errcode = pt_time_update_tma(&tfix->time, &packet, &tfix->config);
	ptu_int_eq(errcode, -pte_bad_context);

	ptu_uint_eq(tfix->obsv_calls, obsv_calls);

	return ptu_passed();
}

static struct ptunit_result mtc(struct time_fixture *tfix, uint32_t obsv_calls)
{
	struct pt_packet_mtc packet;
	uint64_t tsc;
	int errcode;

	packet.ctc = 0xdc;

	errcode = pt_time_update_mtc(&tfix->time, &packet, &tfix->config);
	ptu_int_eq(errcode, 0);

	errcode = pt_time_query_tsc(&tsc, NULL, NULL, &tfix->time);
	ptu_int_eq(errcode, -pte_no_time);

	ptu_uint_eq(tfix->obsv_calls, obsv_calls);

	return ptu_passed();
}

static struct ptunit_result cyc(struct time_fixture *tfix, uint32_t obsv_calls)
{
	struct pt_packet_cyc packet;
	uint64_t fcr, tsc;
	int errcode;

	errcode = pt_tcal_fcr(&fcr, &tfix->tcal);
	ptu_int_eq(errcode, 0);

	packet.value = 0xdc;

	errcode = pt_time_update_cyc(&tfix->time, &packet, &tfix->config, fcr);
	ptu_int_eq(errcode, 0);

	errcode = pt_time_query_tsc(&tsc, NULL, NULL, &tfix->time);
	ptu_int_eq(errcode, -pte_no_time);

	ptu_uint_eq(tfix->obsv_calls, obsv_calls);

	return ptu_passed();
}

static struct ptunit_result tsc_cyc(struct time_fixture *tfix,
				    uint64_t fcr, uint32_t obsv_calls)
{
	struct pt_packet_tsc p_tsc;
	struct pt_packet_cyc p_cyc;
	uint64_t tsc;
	uint32_t lost_mtc, lost_cyc;
	int errcode;

	p_tsc.tsc = 0xdedededeull;
	p_cyc.value = 8ull;

	errcode = pt_time_update_tsc(&tfix->time, &p_tsc, &tfix->config);
	ptu_int_eq(errcode, 0);

	errcode = pt_time_update_cyc(&tfix->time, &p_cyc, &tfix->config, fcr);
	ptu_int_eq(errcode, 0);

	errcode = pt_time_query_tsc(&tsc, &lost_mtc, &lost_cyc, &tfix->time);
	ptu_int_eq(errcode, 0);

	ptu_uint_eq(tsc, 0xdedededeull);
	ptu_uint_eq(lost_mtc, 0);
	if (fcr)
		ptu_uint_eq(lost_cyc, 0);
	else
		ptu_uint_eq(lost_cyc, 1);

	ptu_uint_eq(tfix->obsv_calls, obsv_calls);

	return ptu_passed();
}


int main(int argc, char **argv)
{
	struct ptunit_suite suite;
	struct time_fixture tfix, ofix;

	suite = ptunit_mk_suite(argc, argv);

	tfix.init = tfix_init;
	tfix.fini = tfix_fini;

	ofix.init = tfix_obsv_init;
	ofix.fini = tfix_obsv_fini;

	ptu_run_f(suite, tsc_null, tfix);
	ptu_run_f(suite, cbr_null, tfix);
	ptu_run_f(suite, tma_null, tfix);
	ptu_run_f(suite, mtc_null, tfix);
	ptu_run_f(suite, cyc_null, tfix);

	ptu_run_f(suite, query_tsc_null, tfix);
	ptu_run_f(suite, query_tsc_none, tfix);
	ptu_run_f(suite, query_cbr_null, tfix);
	ptu_run_f(suite, query_cbr_none, tfix);

	ptu_run_f(suite, tcal_cbr_null, tfix);
	ptu_run_f(suite, tcal_mtc_null, tfix);
	ptu_run_f(suite, tcal_cyc_null, tfix);

	ptu_run_fp(suite, tsc, tfix, 0);
	ptu_run_fp(suite, cbr, tfix, 0);
	ptu_run_fp(suite, tma, tfix, 0);
	ptu_run_fp(suite, mtc, tfix, 0);
	ptu_run_fp(suite, cyc, tfix, 0);

	/* The bulk is covered in ptt tests. */

	ptu_run_fp(suite, tsc, ofix, 1);
	ptu_run_fp(suite, tsc_cyc, ofix, 0ull, 1);
	ptu_run_fp(suite, tsc_cyc, ofix, 1ull, 2);

	ptu_run_fp(suite, cbr, ofix, 0);

	ptunit_report(&suite);
	return suite.nr_fails;
}
