// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "debugd/src/crosh_shell_tool.h"

TEST(CroshShellTool, Run) {
  debugd::CroshShellTool crosh_tool;

  const base::ScopedFD infd;
  const base::ScopedFD outfd;
  std::string id;
  brillo::ErrorPtr error;
  EXPECT_TRUE(crosh_tool.Run(infd, outfd, &id, &error));
}
