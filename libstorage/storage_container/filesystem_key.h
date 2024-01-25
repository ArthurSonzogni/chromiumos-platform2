// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_FILESYSTEM_KEY_H_
#define LIBSTORAGE_STORAGE_CONTAINER_FILESYSTEM_KEY_H_

#include <brillo/secure_blob.h>

namespace libstorage {

struct FileSystemKey {
  brillo::SecureBlob fek;
  brillo::SecureBlob fnek;
  brillo::SecureBlob fek_salt;
  brillo::SecureBlob fnek_salt;

  friend bool operator==(const FileSystemKey& lhs,
                         const FileSystemKey& rhs) = default;
};

struct FileSystemKeyReference {
  brillo::SecureBlob fek_sig;
  brillo::SecureBlob fnek_sig;

  friend bool operator==(const FileSystemKeyReference& lhs,
                         const FileSystemKeyReference& rhs) = default;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_FILESYSTEM_KEY_H_
