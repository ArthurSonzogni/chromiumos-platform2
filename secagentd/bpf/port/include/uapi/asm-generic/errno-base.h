// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_BPF_PORT_INCLUDE_UAPI_ASM_GENERIC_ERRNO_BASE_H_
#define SECAGENTD_BPF_PORT_INCLUDE_UAPI_ASM_GENERIC_ERRNO_BASE_H_

#define CROS_EPERM 1    /* Operation not permitted */
#define CROS_ENOENT 2   /* No such file or directory */
#define CROS_ESRCH 3    /* No such process */
#define CROS_EINTR 4    /* Interrupted system call */
#define CROS_EIO 5      /* I/O error */
#define CROS_ENXIO 6    /* No such device or address */
#define CROS_E2BIG 7    /* Argument list too long */
#define CROS_ENOEXEC 8  /* Exec format error */
#define CROS_EBADF 9    /* Bad file number */
#define CROS_ECHILD 10  /* No child processes */
#define CROS_EAGAIN 11  /* Try again */
#define CROS_ENOMEM 12  /* Out of memory */
#define CROS_EACCES 13  /* Permission denied */
#define CROS_EFAULT 14  /* Bad address */
#define CROS_ENOTBLK 15 /* Block device required */
#define CROS_EBUSY 16   /* Device or resource busy */
#define CROS_EEXIST 17  /* File exists */
#define CROS_EXDEV 18   /* Cross-device link */
#define CROS_ENODEV 19  /* No such device */
#define CROS_ENOTDIR 20 /* Not a directory */
#define CROS_EISDIR 21  /* Is a directory */
#define CROS_EINVAL 22  /* Invalid argument */
#define CROS_ENFILE 23  /* File table overflow */
#define CROS_EMFILE 24  /* Too many open files */
#define CROS_ENOTTY 25  /* Not a typewriter */
#define CROS_ETXTBSY 26 /* Text file busy */
#define CROS_EFBIG 27   /* File too large */
#define CROS_ENOSPC 28  /* No space left on device */
#define CROS_ESPIPE 29  /* Illegal seek */
#define CROS_EROFS 30   /* Read-only file system */
#define CROS_EMLINK 31  /* Too many links */
#define CROS_EPIPE 32   /* Broken pipe */
#define CROS_EDOM 33    /* Math argument out of domain of func */
#define CROS_ERANGE 34  /* Math result not representable */

#endif  // SECAGENTD_BPF_PORT_INCLUDE_UAPI_ASM_GENERIC_ERRNO_BASE_H_
