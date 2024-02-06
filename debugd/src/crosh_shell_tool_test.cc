// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "debugd/src/crosh_shell_tool.h"

class CroshShellToolTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
};

TEST_F(CroshShellToolTest, Run) {
  debugd::CroshShellTool crosh_tool;

  const base::ScopedFD shell_lifeline_fd;
  const base::ScopedFD infd;
  const base::ScopedFD outfd;
  std::string id;
  brillo::ErrorPtr error;
  EXPECT_TRUE(crosh_tool.Run(shell_lifeline_fd, infd, outfd, &id, &error));
}
