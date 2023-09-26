// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTSPLASH_PATHS_H_
#define BOOTSPLASH_PATHS_H_

#include <base/files/file_path.h>
#include <base/strings/string_piece.h>

namespace paths {

constexpr char kFreconRunDir[] = "/run/frecon/";
constexpr char kFreconVt[] = "/run/frecon/vt0";
constexpr char kFreconHiRes[] = "/run/frecon/hi_res";

constexpr char kChromeOsAssetsDir[] = "/usr/share/chromeos-assets/";

constexpr char kFeatureOobeSimonDir[] = "animated_splash_screen/";

constexpr char kImages100PercentDir[] = "images_100_percent/";
constexpr char kImages200PercentDir[] = "images_200_percent/";

constexpr char kSplash100PercentDir[] = "splash_100_percent/";
constexpr char kSplash200PercentDir[] = "splash_200_percent/";

constexpr char kBootSplashFilenamePrefix[] = "boot_splash_frame";
constexpr char kImageExtension[] = ".png";

// Gets a FilePath from the given path. A prefix will be added if the prefix is
// set with SetPrefixForTesting().
base::FilePath Get(base::StringPiece file_path);

// Sets a prefix that'll be added when Get() is called, for unit testing.
// For example, if "/tmp" is set as the prefix, Get("/run/foo") will return
// "/tmp/run/foo". Passing "" will reset the prefix.
void SetPrefixForTesting(const base::FilePath& prefix);

// Get the boot splash assets directory.
base::FilePath GetBootSplashAssetsDir(bool feature_simon_enabled);

}  // namespace paths

#endif  // BOOTSPLASH_PATHS_H_
