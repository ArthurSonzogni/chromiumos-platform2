// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "testing/sommelier-test-util.h"
#include "testing/wayland-test-base.h"

#include "mock-xdg-shell-shim.h"  // NOLINT(build/include_directory)
#include "sommelier.h"            // NOLINT(build/include_directory)
#include "sommelier-ctx.h"        // NOLINT(build/include_directory)
#include "sommelier-xdg-shell.h"  // NOLINT(build/include_directory)
#include "xdg-shell-shim.h"       // NOLINT(build/include_directory)

namespace vm_tools {
namespace sommelier {

using ::testing::NiceMock;

class XdgShellTest : public WaylandTestBase {
 public:
  void Connect() override {
    // Setup all the shims.
    set_xdg_positioner_shim(&mock_xdg_positioner_shim_);
    set_xdg_popup_shim(&mock_xdg_popup_shim_);
    set_xdg_toplevel_shim(&mock_xdg_toplevel_shim_);
    set_xdg_surface_shim(&mock_xdg_surface_shim_);
    set_xdg_wm_base_shim(&mock_xdg_wm_base_shim_);

    WaylandTestBase::Connect();

    ctx.use_direct_scale = false;
    client = std::make_unique<FakeWaylandClient>(&ctx);
    ctx.client = client->client;
  }

  void TearDown() override {
    client->Flush();
    WaylandTestBase::TearDown();
  }

 protected:
  NiceMock<MockXdgWmBaseShim> mock_xdg_wm_base_shim_;
  NiceMock<MockXdgPositionerShim> mock_xdg_positioner_shim_;
  NiceMock<MockXdgPopupShim> mock_xdg_popup_shim_;
  NiceMock<MockXdgToplevelShim> mock_xdg_toplevel_shim_;
  NiceMock<MockXdgSurfaceShim> mock_xdg_surface_shim_;

  std::unique_ptr<FakeWaylandClient> client;
};

class XdgPositionerTest : public XdgShellTest {
 public:
  void SetUp() override {
    WaylandTestBase::SetUp();

    captured_proxy = nullptr;

    EXPECT_CALL(mock_xdg_wm_base_shim_, create_positioner(_))
        .WillOnce([](struct xdg_wm_base* xdg_wm_base) {
          return xdg_wm_base_create_positioner(xdg_wm_base);
        });

    EXPECT_CALL(mock_xdg_positioner_shim_, set_user_data(_, _))
        .WillOnce(
            [this](struct xdg_positioner* xdg_positioner, void* user_data) {
              // Capture the object pointers so we can verify below.
              captured_proxy = xdg_positioner;
              xdg_positioner_set_user_data(xdg_positioner, user_data);
            });

    positioner = client->CreatePositioner();

    Pump();
  }

 protected:
  struct xdg_positioner* captured_proxy;
  struct xdg_positioner* positioner;
};

TEST_F(XdgPositionerTest, SetSize_ForwardsUnscaled) {
  EXPECT_CALL(mock_xdg_positioner_shim_, set_size(captured_proxy, 100, 100));
  xdg_positioner_set_size(positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetSize_AppliesCtxScale) {
  ctx.scale = 2.0;
  EXPECT_CALL(mock_xdg_positioner_shim_, set_size(captured_proxy, 50, 50));

  xdg_positioner_set_size(positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetSize_UnscaledWithDirectScale) {
  ctx.use_direct_scale = true;
  EXPECT_CALL(mock_xdg_positioner_shim_, set_size(captured_proxy, 100, 100));

  xdg_positioner_set_size(positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetSize_AppliesXdgScaleWithDirectScale) {
  ctx.use_direct_scale = true;
  ctx.xdg_scale_x = 2.0;
  ctx.xdg_scale_y = 4.0;
  EXPECT_CALL(mock_xdg_positioner_shim_, set_size(captured_proxy, 50, 25));

  xdg_positioner_set_size(positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetAnchorRect_ForwardsUnscaled) {
  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_anchor_rect(captured_proxy, 0, 0, 100, 100));

  xdg_positioner_set_anchor_rect(positioner, 0, 0, 100, 100);
}

TEST_F(XdgPositionerTest, SetAnchorRect_AppliesCtxScale) {
  ctx.scale = 2.0;
  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_anchor_rect(captured_proxy, 0, 0, 50, 50));

  xdg_positioner_set_anchor_rect(positioner, 0, 0, 100, 100);
}

TEST_F(XdgPositionerTest, SetAnchorRect_UnscaledWithDirectScale) {
  ctx.use_direct_scale = true;
  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_anchor_rect(captured_proxy, 0, 0, 100, 100));

  xdg_positioner_set_anchor_rect(positioner, 0, 0, 100, 100);
}

TEST_F(XdgPositionerTest, SetAnchorRect_AppliesXdgScaleWithDirectScale) {
  ctx.use_direct_scale = true;
  ctx.xdg_scale_x = 2.0;
  ctx.xdg_scale_y = 4.0;

  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_anchor_rect(captured_proxy, 0, 0, 50, 25));

  xdg_positioner_set_anchor_rect(positioner, 0, 0, 100, 100);
}

TEST_F(XdgPositionerTest, SetOffset_ForwardsUnscaled) {
  EXPECT_CALL(mock_xdg_positioner_shim_, set_offset(captured_proxy, 100, 100));

  xdg_positioner_set_offset(positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetOffset_AppliesCtxScale) {
  ctx.scale = 2.0;

  EXPECT_CALL(mock_xdg_positioner_shim_, set_offset(captured_proxy, 50, 50));

  xdg_positioner_set_offset(positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetOffset_UnscaledWithDirectScale) {
  ctx.use_direct_scale = true;

  EXPECT_CALL(mock_xdg_positioner_shim_, set_offset(captured_proxy, 100, 100));

  xdg_positioner_set_offset(positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetOffset_AppliesXdgScaleWithDirectScale) {
  ctx.use_direct_scale = true;
  ctx.xdg_scale_x = 2.0;
  ctx.xdg_scale_y = 4.0;

  EXPECT_CALL(mock_xdg_positioner_shim_, set_offset(captured_proxy, 50, 25));

  xdg_positioner_set_offset(positioner, 100, 100);
}

}  // namespace sommelier
}  // namespace vm_tools
