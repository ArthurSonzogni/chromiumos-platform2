// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <memory>

#include <base/files/file_path.h>
#include <brillo/process/process.h>
#include <gtest/gtest.h>

namespace {
constexpr char kScriptPath[] = "src/helpers/capture_utility_test.sh";
}

TEST(CaptureUtilityHelperTest, ShellWrapper) {
  base::FilePath script(base::FilePath(getenv("SRC")).Append(kScriptPath));
  brillo::ProcessImpl p;
  p.AddArg(script.value());
  ASSERT_EQ(0, p.Run());
}
