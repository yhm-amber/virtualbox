/** @file
 * innotek Portable Runtime - stdarg.h wrapper.
 */

/*
 * Copyright (C) 2006-2007 innotek GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */

#ifndef __iprt_stdarg_h__
#define __iprt_stdarg_h__

#ifndef IPRT_NO_CRT
# include <stdarg.h>
#else
# include <iprt/types.h>
# include <iprt/nocrt/compiler/compiler.h>
#endif

/*
 * MSC doesn't implement va_copy.
 */
#ifndef va_copy
# define va_copy(dst, src) do { (dst) = (src); } while (0) /** @todo check AMD64 */
#endif

#endif

