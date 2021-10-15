// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <string>

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sys/socket.h>
#include <wayland-client.h>
#include <wayland-util.h>

#include "sommelier.h"  // NOLINT(build/include_directory)
#include "virtualization/wayland_channel.h"  // NOLINT(build/include_directory)
#include "xdg-shell-client-protocol.h"       // NOLINT(build/include_directory)

// Help gtest print Wayland message streams on expectation failure.
//
// This is defined in the test file mostly to avoid the main program depending
// on <iostream> and <string> merely for testing purposes. Also, it doesn't
// print the entire struct, just the data buffer, so it's not a complete
// representation of the object.
std::ostream& operator<<(std::ostream& os, const WaylandSendReceive& w) {
  // Partially decode the data buffer. The content of messages is not decoded,
  // except their object ID and opcode.
  size_t i = 0;
  while (i < w.data_size) {
    uint32_t object_id = *reinterpret_cast<uint32_t*>(w.data + i);
    uint32_t second_word = *reinterpret_cast<uint32_t*>(w.data + i + 4);
    uint16_t message_size_in_bytes = second_word >> 16;
    uint16_t opcode = second_word & 0xffff;
    os << "[object ID " << object_id << ", opcode " << opcode << ", length "
       << message_size_in_bytes << "] ";
    i += message_size_in_bytes;
  }
  if (i != w.data_size) {
    os << "[WARNING: " << (w.data_size - i) << "undecoded trailing bytes]";
  }

  return os;
}

namespace vm_tools {
namespace sommelier {

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::PrintToString;
using ::testing::Return;
using ::testing::SetArgPointee;

class MockWaylandChannel : public WaylandChannel {
 public:
  MockWaylandChannel() {}

  MOCK_METHOD(int32_t, init, ());
  MOCK_METHOD(bool, supports_dmabuf, ());
  MOCK_METHOD(int32_t, create_context, (int& out_socket_fd));
  MOCK_METHOD(int32_t, create_pipe, (int& out_pipe_fd));
  MOCK_METHOD(int32_t, send, (const struct WaylandSendReceive& send));
  MOCK_METHOD(
      int32_t,
      handle_channel_event,
      (enum WaylandChannelEvent & event_type,
       struct WaylandSendReceive& receive,  // NOLINT(runtime/references)
       int& out_read_pipe));

  MOCK_METHOD(int32_t,
              allocate,
              (const struct WaylandBufferCreateInfo& create_info,
               struct WaylandBufferCreateOutput&
                   create_output));  // NOLINT(runtime/references)
  MOCK_METHOD(int32_t, sync, (int dmabuf_fd, uint64_t flags));
  MOCK_METHOD(int32_t,
              handle_pipe,
              (int read_fd, bool readable, bool& hang_up));
  MOCK_METHOD(size_t, max_send_size, ());

 protected:
  ~MockWaylandChannel() override {}
};

// Match a WaylandSendReceive buffer containing exactly one Wayland message
// with given object ID and opcode.
MATCHER_P2(ExactlyOneMessage,
           object_id,
           opcode,
           std::string("exactly one Wayland message ") +
               (negation ? "not for" : "for") + " object ID " +
               PrintToString(object_id) + ", opcode " + PrintToString(opcode)) {
  const struct WaylandSendReceive& send = arg;
  if (send.data_size < sizeof(uint32_t) * 2) {
    // Malformed packet (too short)
    return false;
  }

  uint32_t actual_object_id = *reinterpret_cast<uint32_t*>(send.data);
  uint32_t second_word = *reinterpret_cast<uint32_t*>(send.data + 4);
  uint16_t message_size_in_bytes = second_word >> 16;
  uint16_t actual_opcode = second_word & 0xffff;

  // ID and opcode must match expectation, and we must see exactly one message
  // with the indicated length.
  return object_id == actual_object_id && opcode == actual_opcode &&
         message_size_in_bytes == send.data_size;
};

// Fixture for tests which exercise only Wayland functionality.
class WaylandTest : public ::testing::Test {
 public:
  void SetUp() override {
    ON_CALL(mock_wayland_channel_, create_context(_)).WillByDefault(Return(0));
    ON_CALL(mock_wayland_channel_, max_send_size())
        .WillByDefault(Return(DEFAULT_BUFFER_SIZE));
    EXPECT_CALL(mock_wayland_channel_, init).Times(1);
    sl_context_init_default(&ctx);
    ctx.host_display = wl_display_create();
    assert(ctx.host_display);

    ctx.channel = &mock_wayland_channel_;
    EXPECT_TRUE(sl_context_init_wayland_channel(
        &ctx, wl_display_get_event_loop(ctx.host_display), false));

    InitContext();
    Connect();
  }

  void TearDown() override {
    // Process any pending messages before the test exits.
    Pump();
  }

  // Flush and dispatch Wayland client calls to the mock host.
  //
  // Called by default in TearDown(), but you can also trigger it midway
  // through the test.
  //
  // If you call `EXPECT_CALL(mock_wayland_channel_, send)` before Pump(), the
  // expectations won't trigger until the Pump() call.
  //
  // Conversely, calling Pump() before
  // `EXPECT_CALL(mock_wayland_channel_, send)` is useful to flush out
  // init messages not relevant to your test case.
  void Pump() {
    wl_display_flush(ctx.display);
    wl_event_loop_dispatch(wl_display_get_event_loop(ctx.host_display), 0);
  }

 protected:
  // Allow subclasses to customize the context prior to Connect().
  virtual void InitContext() {}

  // Set up the Wayland connection, compositor and registry.
  virtual void Connect() {
    ctx.display = wl_display_connect_to_fd(ctx.virtwl_display_fd);
    wl_registry* registry = wl_display_get_registry(ctx.display);

    sl_compositor_init_context(&ctx, registry, 0, kMinHostWlCompositorVersion);
    EXPECT_NE(ctx.compositor, nullptr);

    // Fake the Wayland server advertising globals.
    uint32_t id = 1;
    sl_registry_handler(&ctx, registry, id++, "xdg_wm_base", 1);
  }

  virtual sl_window* CreateWindowWithoutRole() {
    xcb_window_t window_id = 1;
    sl_create_window(&ctx, window_id, 0, 0, 800, 600, 0);
    sl_window* window = sl_lookup_window(&ctx, window_id);
    EXPECT_NE(window, nullptr);
    return window;
  }

  virtual sl_window* CreateToplevelWindow() {
    sl_window* window = CreateWindowWithoutRole();
    wl_surface* surface =
        wl_compositor_create_surface(ctx.compositor->internal);
    window->host_surface_id =
        wl_proxy_get_id(reinterpret_cast<wl_proxy*>(surface));
    window->xdg_surface =
        xdg_wm_base_get_xdg_surface(ctx.xdg_shell->internal, surface);
    window->xdg_toplevel = xdg_surface_get_toplevel(window->xdg_surface);
    return window;
  }

  testing::NiceMock<MockWaylandChannel> mock_wayland_channel_;
  sl_context ctx;
};

// Fixture for unit tests which exercise both Wayland and X11 functionality.
class X11Test : public WaylandTest {
 public:
  void InitContext() override {
    WaylandTest::InitContext();
    ctx.xwayland = 1;
  }

  void Connect() override {
    WaylandTest::Connect();
    ctx.connection = xcb_connect(NULL, NULL);
  }
};

TEST_F(WaylandTest, CanCommitToEmptySurface) {
  wl_surface* surface = wl_compositor_create_surface(ctx.compositor->internal);
  wl_surface_commit(surface);
}

TEST_F(X11Test, TogglesFullscreenOnWmStateFullscreen) {
  // Arrange: Create an xdg_toplevel surface. Initially it's not fullscreen.
  sl_window* window = CreateToplevelWindow();
  uint32_t xdg_toplevel_id =
      wl_proxy_get_id(reinterpret_cast<wl_proxy*>(window->xdg_toplevel));
  EXPECT_EQ(window->fullscreen, 0);
  Pump();  // exclude pending messages from EXPECT_CALL()s below

  // Act: Pretend the window is owned by an X11 client requesting fullscreen.
  // Sommelier receives the XCB_CLIENT_MESSAGE request due to its role as the
  // X11 window manager. For test purposes, we skip creating a real X11
  // connection and just call directly into the relevant handler.
  xcb_client_message_event_t event;
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = window->id;
  event.type = ctx.atoms[ATOM_NET_WM_STATE].value;
  event.data.data32[0] = NET_WM_STATE_ADD;
  event.data.data32[1] = ctx.atoms[ATOM_NET_WM_STATE_FULLSCREEN].value;
  event.data.data32[2] = 0;
  event.data.data32[3] = 0;
  event.data.data32[4] = 0;
  sl_handle_client_message(&ctx, &event);

  // Assert: Sommelier records the fullscreen state.
  EXPECT_EQ(window->fullscreen, 1);
  // Assert: Sommelier forwards the fullscreen request to Exo.
  EXPECT_CALL(
      mock_wayland_channel_,
      send(ExactlyOneMessage(xdg_toplevel_id, XDG_TOPLEVEL_SET_FULLSCREEN)))
      .RetiresOnSaturation();
  Pump();

  // Act: Pretend the fictitious X11 client requests non-fullscreen.
  event.data.data32[0] = NET_WM_STATE_REMOVE;
  sl_handle_client_message(&ctx, &event);

  // Assert: Sommelier records the fullscreen state.
  EXPECT_EQ(window->fullscreen, 0);
  // Assert: Sommelier forwards the unfullscreen request to Exo.
  EXPECT_CALL(
      mock_wayland_channel_,
      send(ExactlyOneMessage(xdg_toplevel_id, XDG_TOPLEVEL_UNSET_FULLSCREEN)))
      .RetiresOnSaturation();
}

TEST_F(X11Test, TogglesMaximizeOnWmStateMaximize) {
  // Arrange: Create an xdg_toplevel surface. Initially it's not maximized.
  sl_window* window = CreateToplevelWindow();
  uint32_t xdg_toplevel_id =
      wl_proxy_get_id(reinterpret_cast<wl_proxy*>(window->xdg_toplevel));
  EXPECT_EQ(window->maximized, 0);
  Pump();  // exclude pending messages from EXPECT_CALL()s below

  // Act: Pretend an X11 client owns the surface, and requests to maximize it.
  xcb_client_message_event_t event;
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = window->id;
  event.type = ctx.atoms[ATOM_NET_WM_STATE].value;
  event.data.data32[0] = NET_WM_STATE_ADD;
  event.data.data32[1] = ctx.atoms[ATOM_NET_WM_STATE_MAXIMIZED_HORZ].value;
  event.data.data32[2] = ctx.atoms[ATOM_NET_WM_STATE_MAXIMIZED_VERT].value;
  event.data.data32[3] = 0;
  event.data.data32[4] = 0;
  sl_handle_client_message(&ctx, &event);

  // Assert: Sommelier records the maximized state + forwards to Exo.
  EXPECT_EQ(window->maximized, 1);
  EXPECT_CALL(
      mock_wayland_channel_,
      send(ExactlyOneMessage(xdg_toplevel_id, XDG_TOPLEVEL_SET_MAXIMIZED)))
      .RetiresOnSaturation();
  Pump();

  // Act: Pretend the fictitious X11 client requests to unmaximize.
  event.data.data32[0] = NET_WM_STATE_REMOVE;
  sl_handle_client_message(&ctx, &event);

  // Assert: Sommelier records the unmaximized state + forwards to Exo.
  EXPECT_EQ(window->maximized, 0);
  EXPECT_CALL(
      mock_wayland_channel_,
      send(ExactlyOneMessage(xdg_toplevel_id, XDG_TOPLEVEL_UNSET_MAXIMIZED)))
      .RetiresOnSaturation();
  Pump();
}

TEST_F(X11Test, CanEnterFullscreenIfAlreadyMaximized) {
  // Arrange
  sl_window* window = CreateToplevelWindow();
  uint32_t xdg_toplevel_id =
      wl_proxy_get_id(reinterpret_cast<wl_proxy*>(window->xdg_toplevel));
  Pump();  // exclude pending messages from EXPECT_CALL()s below

  // Act: Pretend an X11 client owns the surface, and requests to maximize it.
  xcb_client_message_event_t event;
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = window->id;
  event.type = ctx.atoms[ATOM_NET_WM_STATE].value;
  event.data.data32[0] = NET_WM_STATE_ADD;
  event.data.data32[1] = ctx.atoms[ATOM_NET_WM_STATE_MAXIMIZED_HORZ].value;
  event.data.data32[2] = ctx.atoms[ATOM_NET_WM_STATE_MAXIMIZED_VERT].value;
  event.data.data32[3] = 0;
  event.data.data32[4] = 0;
  sl_handle_client_message(&ctx, &event);

  // Assert: Sommelier records the maximized state + forwards to Exo.
  EXPECT_EQ(window->maximized, 1);
  EXPECT_CALL(
      mock_wayland_channel_,
      send(ExactlyOneMessage(xdg_toplevel_id, XDG_TOPLEVEL_SET_MAXIMIZED)))
      .RetiresOnSaturation();
  Pump();

  // Act: Pretend the X11 client requests fullscreen.
  xcb_client_message_event_t fsevent;
  fsevent.response_type = XCB_CLIENT_MESSAGE;
  fsevent.format = 32;
  fsevent.window = window->id;
  fsevent.type = ctx.atoms[ATOM_NET_WM_STATE].value;
  fsevent.data.data32[0] = NET_WM_STATE_ADD;
  fsevent.data.data32[1] = 0;
  fsevent.data.data32[2] = ctx.atoms[ATOM_NET_WM_STATE_FULLSCREEN].value;
  fsevent.data.data32[3] = 0;
  fsevent.data.data32[4] = 0;
  sl_handle_client_message(&ctx, &fsevent);

  // Assert: Sommelier records the fullscreen state + forwards to Exo.
  EXPECT_EQ(window->fullscreen, 1);
  EXPECT_CALL(
      mock_wayland_channel_,
      send(ExactlyOneMessage(xdg_toplevel_id, XDG_TOPLEVEL_SET_FULLSCREEN)))
      .RetiresOnSaturation();
  Pump();
}

}  // namespace sommelier
}  // namespace vm_tools

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::GTEST_FLAG(throw_on_failure) = true;
  // TODO(nverne): set up logging?
  return RUN_ALL_TESTS();
}
