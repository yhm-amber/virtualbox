
/*============================================================================

This C source file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014, 2015, 2017 The Regents of the University of
California.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions, and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions, and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

 3. Neither the name of the University nor the names of its contributors may
    be used to endorse or promote products derived from this software without
    specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS", AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ARE
DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=============================================================================*/

#include <stdbool.h>
#include <stdint.h>
#include "platform.h"
#include "internals.h"
#include "softfloat.h"

extFloat80_t
 softfloat_roundPackToExtF80(
     bool sign,
     int_fast32_t exp,
     uint_fast64_t sig,
     uint_fast64_t sigExtra,
     uint_fast8_t roundingPrecision
     SOFTFLOAT_STATE_DECL_COMMA
 )
{
    uint_fast8_t roundingMode;
    bool roundNearEven;
    uint_fast64_t roundIncrement, roundMask, roundBits;
    bool isTiny, doIncrement;
    struct uint64_extra sig64Extra;
    union { struct extFloat80M s; extFloat80_t f; } uZ;

    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    roundingMode = softfloat_roundingMode;
    roundNearEven = (roundingMode == softfloat_round_near_even);
    if ( roundingPrecision == 80 ) goto precision80;
    if ( roundingPrecision == 64 ) {
        roundIncrement = UINT64_C( 0x0000000000000400 );
        roundMask = UINT64_C( 0x00000000000007FF );
    } else if ( roundingPrecision == 32 ) {
        roundIncrement = UINT64_C( 0x0000008000000000 );
        roundMask = UINT64_C( 0x000000FFFFFFFFFF );
    } else {
        goto precision80;
    }
    sig |= (sigExtra != 0);
    if ( ! roundNearEven && (roundingMode != softfloat_round_near_maxMag) ) {
        roundIncrement =
            (roundingMode
                 == (sign ? softfloat_round_min : softfloat_round_max))
                ? roundMask
                : 0;
    }
    roundBits = sig & roundMask;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if ( 0x7FFD <= (uint32_t) (exp - 1) ) {
        if ( exp <= 0 ) {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            isTiny =
                   (softfloat_detectTininess
                        == softfloat_tininess_beforeRounding)
                || (exp < 0)
                || (sig <= (uint64_t) (sig + roundIncrement));
            sig = softfloat_shiftRightJam64( sig, 1 - exp );
            roundBits = sig & roundMask;
            if ( roundBits ) {
                if ( isTiny ) softfloat_raiseFlags( softfloat_flag_underflow SOFTFLOAT_STATE_ARG_COMMA );
                softfloat_exceptionFlags |= softfloat_flag_inexact;
                if ( roundIncrement ) softfloat_exceptionFlags |= softfloat_flag_c1; /* VBox */
#ifdef SOFTFLOAT_ROUND_ODD
                if ( roundingMode == softfloat_round_odd ) {
                    sig |= roundMask + 1;
                }
#endif
            }
            sig += roundIncrement;
            exp = ((sig & UINT64_C( 0x8000000000000000 )) != 0);
            roundIncrement = roundMask + 1;
            if ( roundNearEven && (roundBits<<1 == roundIncrement) ) {
                roundMask |= roundIncrement;
            }
            sig &= ~roundMask;
            goto packReturn;
        }
        if (
               (0x7FFE < exp)
            || ((exp == 0x7FFE) && ((uint64_t) (sig + roundIncrement) < sig))
        ) {
            goto overflow;
        }
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if ( roundBits ) {
        softfloat_exceptionFlags |= softfloat_flag_inexact;
        if ( roundIncrement ) softfloat_exceptionFlags |= softfloat_flag_c1; /* VBox */
#ifdef SOFTFLOAT_ROUND_ODD
        if ( roundingMode == softfloat_round_odd ) {
            sig = (sig & ~roundMask) | (roundMask + 1);
            goto packReturn;
        }
#endif
    }
    sig = (uint64_t) (sig + roundIncrement);
    if ( sig < roundIncrement ) {
        ++exp;
        sig = UINT64_C( 0x8000000000000000 );
    }
    roundIncrement = roundMask + 1;
    if ( roundNearEven && (roundBits<<1 == roundIncrement) ) {
        roundMask |= roundIncrement;
    }
    sig &= ~roundMask;
    goto packReturn;
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 precision80:
    doIncrement = (UINT64_C( 0x8000000000000000 ) <= sigExtra);
    if ( ! roundNearEven && (roundingMode != softfloat_round_near_maxMag) ) {
        doIncrement =
            (roundingMode
                 == (sign ? softfloat_round_min : softfloat_round_max))
                && sigExtra;
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if ( 0x7FFD <= (uint32_t) (exp - 1) ) {
        if ( exp <= 0 ) {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            isTiny =
                   (softfloat_detectTininess
                        == softfloat_tininess_beforeRounding)
                || (exp < 0)
                || ! doIncrement
                || (sig < UINT64_C( 0xFFFFFFFFFFFFFFFF ));
            sig64Extra =
                softfloat_shiftRightJam64Extra( sig, sigExtra, 1 - exp );
            exp = 0;
            sig = sig64Extra.v;
            sigExtra = sig64Extra.extra;
            if ( sigExtra ) {
                if ( isTiny ) softfloat_raiseFlags( softfloat_flag_underflow SOFTFLOAT_STATE_ARG_COMMA );
#ifdef SOFTFLOAT_ROUND_ODD
                if ( roundingMode == softfloat_round_odd ) {
                    sig |= 1;
                    goto packReturn;
                }
#endif
            }
            doIncrement = (UINT64_C( 0x8000000000000000 ) <= sigExtra);
            if (
                ! roundNearEven
                    && (roundingMode != softfloat_round_near_maxMag)
            ) {
                doIncrement =
                    (roundingMode
                         == (sign ? softfloat_round_min : softfloat_round_max))
                        && sigExtra;
            }
            if ( doIncrement ) {
                softfloat_exceptionFlags |= softfloat_flag_c1; /* VBox */
                ++sig;
                sig &=
                    ~(uint_fast64_t)
                         (! (sigExtra & UINT64_C( 0x7FFFFFFFFFFFFFFF ))
                              & roundNearEven);
                exp = ((sig & UINT64_C( 0x8000000000000000 )) != 0);
            }
            goto packReturn;
        }
        if (
               (0x7FFE < exp)
            || ((exp == 0x7FFE) && (sig == UINT64_C( 0xFFFFFFFFFFFFFFFF ))
                    && doIncrement)
        ) {
            /*----------------------------------------------------------------
            *----------------------------------------------------------------*/
            roundMask = 0;
 overflow:
            softfloat_raiseFlags(
                softfloat_flag_overflow | softfloat_flag_inexact
                SOFTFLOAT_STATE_ARG_COMMA );
            if (
                   roundNearEven
                || (roundingMode == softfloat_round_near_maxMag)
                || (roundingMode
                        == (sign ? softfloat_round_min : softfloat_round_max))
            ) {
                exp = 0x7FFF;
                sig = UINT64_C( 0x8000000000000000 );
            } else {
                exp = 0x7FFE;
                sig = ~roundMask;
            }
            goto packReturn;
        }
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
    if ( sigExtra ) {
        softfloat_exceptionFlags |= softfloat_flag_inexact;
#ifdef SOFTFLOAT_ROUND_ODD
        if ( roundingMode == softfloat_round_odd ) {
            sig |= 1;
            goto packReturn;
        }
#endif
    }
    if ( doIncrement ) {
        softfloat_exceptionFlags |= softfloat_flag_c1; /* VBox */
        ++sig;
        if ( ! sig ) {
            ++exp;
            sig = UINT64_C( 0x8000000000000000 );
        } else {
            sig &=
                ~(uint_fast64_t)
                     (! (sigExtra & UINT64_C( 0x7FFFFFFFFFFFFFFF ))
                          & roundNearEven);
        }
    }
    /*------------------------------------------------------------------------
    *------------------------------------------------------------------------*/
 packReturn:
    uZ.s.signExp = packToExtF80UI64( sign, exp );
    uZ.s.signif = sig;
    return uZ.f;

}

