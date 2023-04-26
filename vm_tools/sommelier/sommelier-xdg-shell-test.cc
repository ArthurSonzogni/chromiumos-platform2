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

// Fake constant serial number to be used in the tests below.
const uint32_t kFakeSerial = 721077;

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

class XdgWmBaseTest : public XdgShellTest {
 public:
  void SetUp() override {
    sommelier_xdg_wm_base = nullptr;
    EXPECT_CALL(mock_xdg_wm_base_shim_, add_listener(_, _, _))
        .WillOnce([this](struct xdg_wm_base* xdg_wm_base,
                         const xdg_wm_base_listener* listener,
                         void* user_data) {
          sommelier_xdg_wm_base =
              static_cast<struct sl_host_xdg_shell*>(user_data);
          xdg_wm_base_add_listener(xdg_wm_base, listener, user_data);
          return 0;
        });
    WaylandTestBase::SetUp();

    client_surface = client->CreateSurface();
    Pump();
  }

 protected:
  // Sommelier's xdg_wm_base instance.
  struct sl_host_xdg_shell* sommelier_xdg_wm_base;
  wl_surface* client_surface;
};

TEST_F(XdgWmBaseTest, CreatePositioner_ForwardsCorrectly) {
  EXPECT_CALL(mock_xdg_wm_base_shim_,
              create_positioner(sommelier_xdg_wm_base->proxy));
  xdg_wm_base_create_positioner(client->GetXdgWmBase());
}

TEST_F(XdgWmBaseTest, GetXdgSurface_ForwardsCorrectly) {
  EXPECT_CALL(mock_xdg_wm_base_shim_,
              get_xdg_surface(sommelier_xdg_wm_base->proxy, _));
  xdg_wm_base_get_xdg_surface(client->GetXdgWmBase(), client_surface);
}

TEST_F(XdgWmBaseTest, Ping_SendsCorrectly) {
  EXPECT_CALL(mock_xdg_wm_base_shim_,
              get_user_data(sommelier_xdg_wm_base->proxy))
      .WillOnce([](struct xdg_wm_base* xdg_wm_base) {
        return xdg_wm_base_get_user_data(xdg_wm_base);
      });
  EXPECT_CALL(mock_xdg_wm_base_shim_, send_ping(_, kFakeSerial));

  HostEventHandler(sommelier_xdg_wm_base->proxy)
      ->ping(nullptr, sommelier_xdg_wm_base->proxy, kFakeSerial);
}

TEST_F(XdgWmBaseTest, Pong_ForwardsCorrectly) {
  EXPECT_CALL(mock_xdg_wm_base_shim_, pong(_, kFakeSerial));
  xdg_wm_base_pong(client->GetXdgWmBase(), kFakeSerial);
}

class XdgPositionerTest : public XdgShellTest {
 public:
  void SetUp() override {
    WaylandTestBase::SetUp();

    sommelier_positioner = nullptr;

    EXPECT_CALL(mock_xdg_wm_base_shim_, create_positioner(_))
        .WillOnce([](struct xdg_wm_base* xdg_wm_base) {
          return xdg_wm_base_create_positioner(xdg_wm_base);
        });

    EXPECT_CALL(mock_xdg_positioner_shim_, set_user_data(_, _))
        .WillOnce(
            [this](struct xdg_positioner* xdg_positioner, void* user_data) {
              // Capture the object pointers so we can verify below.
              sommelier_positioner = xdg_positioner;
              xdg_positioner_set_user_data(xdg_positioner, user_data);
            });

    client_positioner = client->CreatePositioner();

    Pump();
  }

 protected:
  struct xdg_positioner* sommelier_positioner;
  struct xdg_positioner* client_positioner;
};

TEST_F(XdgPositionerTest, SetSize_ForwardsUnscaled) {
  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_size(sommelier_positioner, 100, 100));
  xdg_positioner_set_size(client_positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetSize_AppliesCtxScale) {
  ctx.scale = 2.0;
  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_size(sommelier_positioner, 50, 50));

  xdg_positioner_set_size(client_positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetSize_UnscaledWithDirectScale) {
  ctx.use_direct_scale = true;
  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_size(sommelier_positioner, 100, 100));

  xdg_positioner_set_size(client_positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetSize_AppliesXdgScaleWithDirectScale) {
  ctx.use_direct_scale = true;
  ctx.xdg_scale_x = 2.0;
  ctx.xdg_scale_y = 4.0;
  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_size(sommelier_positioner, 50, 25));

  xdg_positioner_set_size(client_positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetAnchorRect_ForwardsUnscaled) {
  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_anchor_rect(sommelier_positioner, 0, 0, 100, 100));

  xdg_positioner_set_anchor_rect(client_positioner, 0, 0, 100, 100);
}

TEST_F(XdgPositionerTest, SetAnchorRect_AppliesCtxScale) {
  ctx.scale = 2.0;
  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_anchor_rect(sommelier_positioner, 0, 0, 50, 50));

  xdg_positioner_set_anchor_rect(client_positioner, 0, 0, 100, 100);
}

TEST_F(XdgPositionerTest, SetAnchorRect_UnscaledWithDirectScale) {
  ctx.use_direct_scale = true;
  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_anchor_rect(sommelier_positioner, 0, 0, 100, 100));

  xdg_positioner_set_anchor_rect(client_positioner, 0, 0, 100, 100);
}

TEST_F(XdgPositionerTest, SetAnchorRect_AppliesXdgScaleWithDirectScale) {
  ctx.use_direct_scale = true;
  ctx.xdg_scale_x = 2.0;
  ctx.xdg_scale_y = 4.0;

  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_anchor_rect(sommelier_positioner, 0, 0, 50, 25));

  xdg_positioner_set_anchor_rect(client_positioner, 0, 0, 100, 100);
}

TEST_F(XdgPositionerTest, SetOffset_ForwardsUnscaled) {
  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_offset(sommelier_positioner, 100, 100));

  xdg_positioner_set_offset(client_positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetOffset_AppliesCtxScale) {
  ctx.scale = 2.0;

  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_offset(sommelier_positioner, 50, 50));

  xdg_positioner_set_offset(client_positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetOffset_UnscaledWithDirectScale) {
  ctx.use_direct_scale = true;

  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_offset(sommelier_positioner, 100, 100));

  xdg_positioner_set_offset(client_positioner, 100, 100);
}

TEST_F(XdgPositionerTest, SetOffset_AppliesXdgScaleWithDirectScale) {
  ctx.use_direct_scale = true;
  ctx.xdg_scale_x = 2.0;
  ctx.xdg_scale_y = 4.0;

  EXPECT_CALL(mock_xdg_positioner_shim_,
              set_offset(sommelier_positioner, 50, 25));

  xdg_positioner_set_offset(client_positioner, 100, 100);
}

}  // namespace sommelier
}  // namespace vm_tools
