/* $Id$ */
/** @file
 * IPRT - Save and Restore Error Variables, POSIX Ring-3.
 */

/*
 * Copyright (C) 2011-2015 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <netdb.h>
#include <errno.h>

#include <iprt/err.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include "internal/magics.h"



RTDECL(PRTERRVARS) RTErrVarsSave(PRTERRVARS pVars)
{
    pVars->ai32Vars[0] = RTERRVARS_MAGIC;
    pVars->ai32Vars[1] = errno;
    pVars->ai32Vars[2] = h_errno;
    return pVars;
}


RTDECL(void) RTErrVarsRestore(PCRTERRVARS pVars)
{
    AssertReturnVoid(pVars->ai32Vars[0] == RTERRVARS_MAGIC);
    h_errno = pVars->ai32Vars[2];
    errno = pVars->ai32Vars[1];
}


RTDECL(bool) RTErrVarsAreEqual(PCRTERRVARS pVars1, PCRTERRVARS pVars2)
{
    Assert(pVars1->ai32Vars[0] == RTERRVARS_MAGIC);
    Assert(pVars2->ai32Vars[0] == RTERRVARS_MAGIC);

    return pVars1->ai32Vars[0] == pVars2->ai32Vars[0]
        && pVars1->ai32Vars[1] == pVars2->ai32Vars[1]
        && pVars1->ai32Vars[2] == pVars2->ai32Vars[2];
}


RTDECL(bool) RTErrVarsHaveChanged(PCRTERRVARS pVars)
{
    Assert(pVars->ai32Vars[0] == RTERRVARS_MAGIC);

    return pVars->ai32Vars[0] != RTERRVARS_MAGIC
        || pVars->ai32Vars[1] != errno
        || pVars->ai32Vars[2] != h_errno;
}

