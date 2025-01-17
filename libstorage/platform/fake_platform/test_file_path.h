// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_PLATFORM_FAKE_PLATFORM_TEST_FILE_PATH_H_
#define LIBSTORAGE_PLATFORM_FAKE_PLATFORM_TEST_FILE_PATH_H_

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

namespace libstorage {
namespace fake_platform {

// Appends absolute path to tmpfs prefix.
BRILLO_EXPORT base::FilePath SpliceTestFilePath(const base::FilePath& tmpfs,
                                                const base::FilePath& path);

// Removes tmpfs prefix from the path, if present.
BRILLO_EXPORT base::FilePath StripTestFilePath(const base::FilePath& tmpfs,
                                               const base::FilePath& path);

// Resolves '..' and '.' but do not resolve links. This is how the function
// differs from the "base" counterpart.
BRILLO_EXPORT base::FilePath NormalizePath(const base::FilePath& path);

}  // namespace fake_platform
}  // namespace libstorage

#endif  // LIBSTORAGE_PLATFORM_FAKE_PLATFORM_TEST_FILE_PATH_H_
