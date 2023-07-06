// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootsplash/paths.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <brillo/strings/string_utils.h>

#include "bootsplash/utils.h"

namespace paths {

namespace {

// The path prefix that'll be used for testing.
const base::FilePath* g_test_prefix;

}  // namespace

void SetPrefixForTesting(const base::FilePath& prefix) {
  if (g_test_prefix) {
    delete g_test_prefix;
    g_test_prefix = nullptr;
  }
  if (!prefix.empty())
    g_test_prefix = new base::FilePath(prefix);
}

base::FilePath Get(base::StringPiece file_path) {
  if (g_test_prefix) {
    if (base::StartsWith(file_path, "/"))
      file_path.remove_prefix(1);
    return g_test_prefix->Append(file_path);
  }

  if (base::IsLink(base::FilePath(file_path))) {
    // Expand symlinks
    return base::MakeAbsoluteFilePath(base::FilePath(file_path));
  }
  return base::FilePath(file_path);
}

base::FilePath GetBootSplashAssetsDir(bool feature_simon_enabled) {
  base::FilePath boot_splash_assets_dir;
  bool is_hi_res_display = utils::IsHiResDisplay();

  /* Determine which assets to use for boot splash screen. */
  boot_splash_assets_dir = paths::Get(paths::kChromeOsAssetsDir);
  if (feature_simon_enabled) {
    boot_splash_assets_dir =
        boot_splash_assets_dir.Append(paths::kFeatureOobeSimonDir);
    if (is_hi_res_display) {
      boot_splash_assets_dir =
          boot_splash_assets_dir.Append(paths::kSplash200PercentDir);
    } else {
      boot_splash_assets_dir =
          boot_splash_assets_dir.Append(paths::kSplash100PercentDir);
    }
  } else {
    if (is_hi_res_display) {
      boot_splash_assets_dir =
          boot_splash_assets_dir.Append(paths::kImages200PercentDir);
    } else {
      boot_splash_assets_dir =
          boot_splash_assets_dir.Append(paths::kImages100PercentDir);
    }
  }

  LOG(INFO) << "Boot splash assets directory: '" << boot_splash_assets_dir
            << "'";

  return boot_splash_assets_dir;
}

}  // namespace paths
