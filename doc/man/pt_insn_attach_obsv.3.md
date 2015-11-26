% PT_INSN_ATTACH_OBSV(3)

<!---
 ! Copyright (c) 2015-2016, Intel Corporation
 !
 ! Redistribution and use in source and binary forms, with or without
 ! modification, are permitted provided that the following conditions are met:
 !
 !  * Redistributions of source code must retain the above copyright notice,
 !    this list of conditions and the following disclaimer.
 !  * Redistributions in binary form must reproduce the above copyright notice,
 !    this list of conditions and the following disclaimer in the documentation
 !    and/or other materials provided with the distribution.
 !  * Neither the name of Intel Corporation nor the names of its contributors
 !    may be used to endorse or promote products derived from this software
 !    without specific prior written permission.
 !
 ! THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 ! AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 ! IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ! ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 ! LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 ! CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 ! SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 ! INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 ! CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ! ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 ! POSSIBILITY OF SUCH DAMAGE.
 !-->

# NAME

pt_insn_attach_obsv, pt_blk_attach_obsv - attach an Intel(R) Processor Trace
decode observer


# SYNOPSIS

| **\#include `<intel-pt.h>`**
|
| **int pt_insn_attach_obsv(struct pt_insn_decoder \**decoder*,**
|                         **struct pt_observer \**obsv*);**
| **int pt_blk_attach_obsv(struct pt_blk_decoder \**decoder*,**
|                        **struct pt_observer \**obsv*);**

Link with *-lipt*.


# DESCRIPTION

**pt_insn_attach_obsv**() attaches a decode observer to an instruction flow
decoder.  The *decoder* argument points to the decoder to be observed and the
*obsv* argument points to the observer to be attached.

**pt_blk_attach_obsv**() attaches a decode observer to a block decoder.  The
*decoder* argument points to the decoder to be observed and the *obsv* argument
points to the observer to be attached.

The *pt_observer* structure is delcared as:

~~~{.c}
struct pt_observer {
    /** The size of this object - set to sizeof(struct pt_observer). */
    size_t size;

    /** A user-defined context for this observer. */
    void *context;

    /** An optional callback for time-based observation. */
    struct {
        /* PRIVATE: The next observer. */
        struct pt_observer *next;

        /** The callback function.
         *
         * If non-NULL, the callback will be called when time changes.
         *
         * It shall return zero on success; a negative pt_error_code
         * otherwise.
         */
        int (*callback)(struct pt_observer *self, uint64_t tsc,
                uint32_t lost_mtc, uint32_t lost_cyc);

        /** An optional time limit.
         *
         * The above \@callback will only be called when the current
         * time is greater than or equal to \@limit.
         *
         * Set to zero to be called for all time changes.
         */
        uint64_t limit;
    } tick;

    /** An optional callback for decode state-based observation. */
    struct {
        /* PRIVATE: The next observer. */
        struct pt_observer *next;

        /** The callback function.
         *
         * If non-NULL, the callback will be called when the decode
         * state changes.
         *
         * It shall return zero on success; a negative pt_error_code
         * otherwise.
         */
        int (*callback)(struct pt_observer *self,
                enum pt_decode_state state);
    } state;

    /** An optional callback for instruction pointer-based observation. */
    struct {
        /* PRIVATE: The next observer. */
        struct pt_observer *next;

        /** The callback function.
         *
         * If non-NULL, the callback will be called for new instruction
         * addresses depending on the decoder.
         *
         * When attached to an instruction flow decoder, it will be
         * called for every instruction.
         *
         * When attached to a block decoder, it will be called once for
         * a block of instructions with the IP of the first instruction
         * in that block.
         *
         * It shall return zero on success; a negative pt_error_code
         * otherwise.
         */
        int (*callback)(struct pt_observer *self, uint64_t ip);
    } ip;
};
~~~

The decoder will notify attached observers about changes in the decode state by
calling the respective callback functions in each obserer.  If the respective
callback function is NULL, that observer will be skipped.  The order in which
observers are notified is undefined and may change over time.

A decode observer can only be attached to one decoder at a time.

A decode observer can subscribe to individual notifications by setting the
respective callback function to point to a function to be called in the
observer.  It can unsubsribe from individual notifications by setting the
respective callback function to NULL.

Changes to the observer object are allowed when the observer is not attached to
a decoder or inside a callback function.  In the latter case, changes take
effect after returning from the callback function and before entering another
callback function in the same decoder or after returning from the decoder
function that triggered the callback.

A decode observer is implicitly detached when it sets all callback functions to
NULL.  A detached observer may be re-attached or destroyed after returning from
the decoder function that triggered the callback in which the decode observer
unsubscribed from all notifications.

Inside a callback function, a decode observer can call decoder functions for the
decoder that triggered the callback.  For example, it may replace the decoder's
traced memory image by calling **pt_insn_set_image**(3) or
**pt_blk_set_image**(3) respectively; or it may obtain the decoder's image in
order to modify it by calling **pt_insn_get_image**(3) or
**pt_blk_get_image**(3).

A decode observer may thus be used to decode and correlate sideband information
that describes how the traced memory image changes over time and to adjust a
trace decoder's image to reflect those changes.

A decode observer's callback functions must not call decoder functions that
might trigger observer notifications or that might change the decoder's state
with respect to its position in the decoded trace, e.g. **pt_insn_next**(3) and
**pt_blk_next**(3) or **pt_insn_sync_forward**(3) and
**pt_blk_sync_forward**(3).


# RETURN VALUE

Both functions return zero on success or a negative *pt_error_code* enumeration
constant in case of an error.


# ERRORS

pte_invalid
:   The *decoder* or *obsv* argument is NULL or *obsv* is already attached to
    a decoder.


# SEE ALSO

**pt_insn_alloc_decoder**(3), **pt_insn_free_decoder**(3),
**pt_insn_sync_forward**(3), **pt_insn_next**(3),
**pt_blk_alloc_decoder**(3), **pt_blk_free_decoder**(3),
**pt_blk_sync_forward**(3), **pt_blk_next**(3)
