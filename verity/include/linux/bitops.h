/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 * 
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_BITOPS_H_
#define VERITY_INCLUDE_LINUX_BITOPS_H_


#define BITS_PER_BYTE           8
/* For verity, this is based on the compilation target and not
 * CONFIG_64BIT. */
#define BITS_PER_LONG           (sizeof(long) * BITS_PER_BYTE)

#define BIT(nr)                 (1UL << (nr))
#define BIT_MASK(nr)            (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)            ((nr) / BITS_PER_LONG)
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/non-atomic.h>
#include <strings.h>



#endif  /* VERITY_INCLUDE_LINUX_BITOPS_H_ */
