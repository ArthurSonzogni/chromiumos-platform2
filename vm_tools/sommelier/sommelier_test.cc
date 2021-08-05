// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sys/socket.h>
#include <wayland-client.h>

#include "sommelier.h"  // NOLINT(build/include_directory)
#include "virtualization/wayland_channel.h"  // NOLINT(build/include_directory)

namespace vm_tools {
namespace sommelier {

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
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

class SommelierTest : public ::testing::Test {
 public:
  bool sl_context_init_for_testing(sl_context* ctx) {
    sl_context_init_default(ctx);
    ctx->host_display = wl_display_create();
    assert(ctx->host_display);

    ctx->channel = &mock_wayland_channel_;
    return sl_context_init_wayland_channel(
        ctx, wl_display_get_event_loop(ctx->host_display), false);
  }

  void SetUp() override {
    int mws[2];
    // Connection to virtwl channel.
    int rv = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, mws);
    errno_assert(!rv);

    ON_CALL(mock_wayland_channel_, create_context(_)).WillByDefault(Return(0));
  }

 protected:
  NiceMock<MockWaylandChannel> mock_wayland_channel_;
};

TEST_F(SommelierTest, TestEmptySurface) {
  sl_context ctx;
  EXPECT_CALL(mock_wayland_channel_, init).Times(1);
  EXPECT_TRUE(sl_context_init_for_testing(&ctx));

  ctx.display = wl_display_connect_to_fd(ctx.virtwl_display_fd);
  wl_registry* registry = wl_display_get_registry(ctx.display);

  sl_compositor_init_context(&ctx, registry, 0, kMinHostWlCompositorVersion);
  EXPECT_NE(ctx.compositor, nullptr);

  wl_surface* surface = wl_compositor_create_surface(ctx.compositor->internal);
  wl_surface_commit(surface);
}

}  // namespace sommelier
}  // namespace vm_tools

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::GTEST_FLAG(throw_on_failure) = true;
  // TODO(nverne): set up logging?
  return RUN_ALL_TESTS();
}
