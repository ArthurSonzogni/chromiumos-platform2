/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Private header file for mount-encrypted helper tool.
 */
#ifndef INIT_MOUNT_ENCRYPTED_MOUNT_ENCRYPTED_H_
#define INIT_MOUNT_ENCRYPTED_MOUNT_ENCRYPTED_H_

#include <sys/types.h>
#include <unistd.h>

#include <base/files/file_path.h>
#include <openssl/err.h>
#include <openssl/sha.h>

#define DIGEST_LENGTH SHA256_DIGEST_LENGTH

enum result_code {
  RESULT_SUCCESS = 0,
  RESULT_FAIL_FATAL = 1,
};

#endif  // INIT_MOUNT_ENCRYPTED_MOUNT_ENCRYPTED_H_
