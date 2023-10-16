// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_TESTING_X11_TEST_BASE_H_
#define VM_TOOLS_SOMMELIER_TESTING_X11_TEST_BASE_H_

#include <xcb/xproto.h>
#include <memory>
#include <string>

#include "../xcb/mock-xcb-shim.h"
#include "wayland-test-base.h"  // NOLINT(build/include_directory)

namespace vm_tools {
namespace sommelier {

// Fixture for unit tests which exercise both Wayland and X11 functionality.
class X11TestBase : public WaylandTestBase {
 public:
  void InitContext() override {
    WaylandTestBase::InitContext();
    ctx.xwayland = 1;

    // Always delegate ID generation to the fake XCB shim, even for test cases
    // that never use the fake for anything else. This prevents ID collisions.
    xcb.DelegateIdGenerationToFake();

    // Create a fake screen with somewhat plausible values.
    // Some of these are not realistic because they refer to things not present
    // in the mocked X environment (such as specifying a root window with ID 0).
    ctx.screen = static_cast<xcb_screen_t*>(malloc(sizeof(xcb_screen_t)));
    ctx.screen->root = 0x0;
    ctx.screen->default_colormap = 0x0;
    ctx.screen->white_pixel = 0x00ffffff;
    ctx.screen->black_pixel = 0x00000000;
    ctx.screen->current_input_masks = 0x005a0000;
    ctx.screen->width_in_pixels = 1920;
    ctx.screen->height_in_pixels = 1080;
    ctx.screen->width_in_millimeters = 508;
    ctx.screen->height_in_millimeters = 285;
    ctx.screen->min_installed_maps = 1;
    ctx.screen->max_installed_maps = 1;
    ctx.screen->root_visual = 0x0;
    ctx.screen->backing_stores = 0x01;
    ctx.screen->save_unders = 0;
    ctx.screen->root_depth = 24;
    ctx.screen->allowed_depths_len = 0;
  }

  void Connect() override {
    set_xcb_shim(&xcb);
    WaylandTestBase::Connect();

    // Pretend Xwayland has connected to Sommelier as a Wayland client.
    xwayland = std::make_unique<FakeWaylandClient>(&ctx);
    ctx.client = xwayland->client;

    // TODO(cpelling): mock out more of xcb so this isn't needed
    ctx.connection = xcb_connect(nullptr, nullptr);
  }

  ~X11TestBase() override { set_xcb_shim(nullptr); }

  virtual sl_window* CreateWindowWithoutRole() {
    xcb_window_t window_id = xcb.generate_id(ctx.connection);
    sl_create_window(&ctx, window_id, 0, 0, 800, 600, 0);
    sl_window* window = sl_lookup_window(&ctx, window_id);
    EXPECT_NE(window, nullptr);
    return window;
  }

  virtual sl_window* CreateToplevelWindow() {
    sl_window* window = CreateWindowWithoutRole();

    // Pretend we created a frame window too
    window->frame_id = xcb.generate_id(ctx.connection);

    window->host_surface_id = SurfaceId(xwayland->CreateSurface());
    sl_window_update(window);
    Pump();
    // Default to the first output if any exist.
    if (!ctx.host_outputs.empty()) {
      struct sl_host_output* output = nullptr;
      output = ctx.host_outputs[0];
      HostEventHandler(window->paired_surface->proxy)
          ->enter(nullptr, window->paired_surface->proxy, output->proxy);
    }
    Pump();
    return window;
  }

  std::string StringPropertyForTesting(xcb_window_t window_id,
                                       xcb_atom_t property_name) {
    xcb_get_property_cookie_t cookie = xcb.get_property(
        nullptr, 0, window_id, property_name, XCB_ATOM_STRING, 0, 1024);
    xcb_get_property_reply_t* reply =
        xcb.get_property_reply(nullptr, cookie, nullptr);
    EXPECT_TRUE(reply) << "get_property_reply() returned null. Try calling "
                          "xcb.DelegateToFake().";
    std::string result;
    if (reply->format != 8) {
      result = "error: expected X11 property format 8, got " +
               std::to_string(reply->format);
    } else if (reply->type != XCB_ATOM_STRING) {
      result = "error: expected X11 property type XCB_ATOM_STRING";
    } else {
      void* value = xcb.get_property_value(reply);
      result = std::string(static_cast<char*>(value), reply->length);
      free(value);
    }
    free(reply);
    return result;
  }

 protected:
  NiceMock<MockXcbShim> xcb;
  std::unique_ptr<FakeWaylandClient> xwayland;
};

// Fixture for unit tests which use direct scale.
class X11DirectScaleTest : public X11TestBase {
 public:
  void InitContext() override {
    X11TestBase::InitContext();
    ctx.use_direct_scale = true;
  }

  void Connect() override {
    X11TestBase::Connect();
    wl_registry* registry = wl_display_get_registry(ctx.display);
    sl_registry_handler(&ctx, registry, next_server_id++,
                        "zxdg_output_manager_v1",
                        WP_VIEWPORTER_DESTROY_SINCE_VERSION);
  }
};

}  // namespace sommelier
}  // namespace vm_tools

#endif  // VM_TOOLS_SOMMELIER_TESTING_X11_TEST_BASE_H_
