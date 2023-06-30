// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTSPLASH_PATHS_H_
#define BOOTSPLASH_PATHS_H_

#include <base/files/file_path.h>
#include <base/strings/string_piece.h>

namespace paths {

constexpr char kFreconVt[] = "/run/frecon/vt0";

constexpr char kFreconHiRes[] = "/run/frecon/hi_res";
// Gets a FilePath from the given path. A prefix will be added if the prefix is
// set with SetPrefixForTesting().
base::FilePath Get(base::StringPiece file_path);

// Sets a prefix that'll be added when Get() is called, for unit testing.
// For example, if "/tmp" is set as the prefix, Get("/run/foo") will return
// "/tmp/run/foo". Passing "" will reset the prefix.
void SetPrefixForTesting(const base::FilePath& prefix);

}  // namespace paths

#endif  // BOOTSPLASH_PATHS_H_
