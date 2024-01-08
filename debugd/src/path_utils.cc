// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/path_utils.h"

#include <base/strings/string_util.h>

namespace {

// The path prefix that'll be used for testing. The caller will be responsible
// for its lifecycle management.
// Use |path_utils::testing::SetPrefixForTesting(base::FilePath())| to reset.
const base::FilePath* g_test_prefix;

}  // namespace

namespace debugd {
namespace path_utils {

base::FilePath GetFilePath(std::string_view file_path) {
  if (g_test_prefix) {
    if (base::StartsWith(file_path, "/"))
      file_path.remove_prefix(1);
    return g_test_prefix->Append(file_path);
  }
  return base::FilePath(file_path);
}

namespace testing {

void SetPrefixForTesting(const base::FilePath& prefix) {
  if (g_test_prefix) {
    delete g_test_prefix;
    g_test_prefix = nullptr;
  }
  if (!prefix.empty())
    g_test_prefix = new base::FilePath(prefix);
}

}  // namespace testing
}  // namespace path_utils
}  // namespace debugd
