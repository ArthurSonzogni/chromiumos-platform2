// NOLINT(legal/copyright)

/* Duplicated under the terms of the GPLv2 from
 * linux-kernel:/include/asm-generic/bitops/fls.h
 */
#ifndef VERITY_INCLUDE_ASM_GENERIC_BITOPS_FLS_H_
#define VERITY_INCLUDE_ASM_GENERIC_BITOPS_FLS_H_

#include <linux/stddef.h>

/**
 * fls - find last (most-significant) bit set
 * @x: the word to search
 *
 * This is defined the same way as ffs.
 * Note fls(0) = 0, fls(1) = 1, fls(0x80000000) = 32.
 */

static __always_inline int fls(int x) {
  int r = 32;

  if (!x)
    return 0;
  if (!(x & 0xffff0000u)) {
    x <<= 16;
    r -= 16;
  }
  if (!(x & 0xff000000u)) {
    x <<= 8;
    r -= 8;
  }
  if (!(x & 0xf0000000u)) {
    x <<= 4;
    r -= 4;
  }
  if (!(x & 0xc0000000u)) {
    x <<= 2;
    r -= 2;
  }
  if (!(x & 0x80000000u)) {
    x <<= 1;
    r -= 1;
  }
  return r;
}

#endif  // VERITY_INCLUDE_ASM_GENERIC_BITOPS_FLS_H_
