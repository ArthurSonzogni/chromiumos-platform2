// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_PLATFORM_UTIL_GET_RANDOM_SUFFIX_H_
#define LIBSTORAGE_PLATFORM_UTIL_GET_RANDOM_SUFFIX_H_

#include <string>

#include <brillo/brillo_export.h>

namespace libstorage {

// Creates a random string suitable to append to a filename. Returns an empty
// string in case of error.
BRILLO_EXPORT std::string GetRandomSuffix();

}  // namespace libstorage

#endif  // LIBSTORAGE_PLATFORM_UTIL_GET_RANDOM_SUFFIX_H_
