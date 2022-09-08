// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_builder.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace vm_tools {
namespace concierge {
namespace {
class VmBuilderTest : public ::testing::Test {};

}  // namespace

TEST_F(VmBuilderTest, DefaultValuesSucceeds) {
  VmBuilder builder;
  EXPECT_FALSE(builder.BuildVmArgs().empty());
}

TEST_F(VmBuilderTest, WaylandSocketIsValid) {
  // Concierge will only accept the default socket, or a socket at a path
  // specified by go/secure-vm-ids.
  EXPECT_FALSE(VmBuilder().SetWaylandSocket().BuildVmArgs().empty());
  EXPECT_FALSE(VmBuilder().SetWaylandSocket("").BuildVmArgs().empty());
  EXPECT_FALSE(VmBuilder()
                   .SetWaylandSocket("/run/chrome/wayland-0")
                   .BuildVmArgs()
                   .empty());
  EXPECT_FALSE(VmBuilder()
                   .SetWaylandSocket("/run/wayland/concierge/test/wayland-0")
                   .BuildVmArgs()
                   .empty());

  // Any variation of the path is disallowed
  EXPECT_TRUE(VmBuilder()
                  .SetWaylandSocket("./run/wayland/concierge/test/wayland-0")
                  .BuildVmArgs()
                  .empty());
  EXPECT_TRUE(VmBuilder()
                  .SetWaylandSocket("/var/wayland/concierge/test/wayland-0")
                  .BuildVmArgs()
                  .empty());
  EXPECT_TRUE(VmBuilder()
                  .SetWaylandSocket("/run/chrome/concierge/test/wayland-0")
                  .BuildVmArgs()
                  .empty());
  EXPECT_TRUE(VmBuilder()
                  .SetWaylandSocket("/run/wayland/cicerone/test/wayland-0")
                  .BuildVmArgs()
                  .empty());
  EXPECT_TRUE(VmBuilder()
                  .SetWaylandSocket("/run/wayland/concierge/wayland-0")
                  .BuildVmArgs()
                  .empty());
  EXPECT_TRUE(VmBuilder()
                  .SetWaylandSocket("/run/wayland/concierge/test/wayland-1")
                  .BuildVmArgs()
                  .empty());
}

}  // namespace concierge
}  // namespace vm_tools
