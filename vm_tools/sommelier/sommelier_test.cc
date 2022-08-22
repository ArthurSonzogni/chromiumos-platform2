// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sys/socket.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <xcb/xproto.h>

#include "sommelier.h"                       // NOLINT(build/include_directory)
#include "sommelier-util.h"                  // NOLINT(build/include_directory)
#include "virtualization/wayland_channel.h"  // NOLINT(build/include_directory)
#include "xcb/mock-xcb-shim.h"

#include "aura-shell-client-protocol.h"  // NOLINT(build/include_directory)
#include "xdg-shell-client-protocol.h"   // NOLINT(build/include_directory)

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
       << message_size_in_bytes;

    uint16_t size = MIN(message_size_in_bytes, w.data_size - i);
    if (size > sizeof(uint32_t) * 2) {
      os << ", args=[";
      for (int j = sizeof(uint32_t) * 2; j < size; ++j) {
        char byte = w.data[i + j];
        if (isprint(byte)) {
          os << byte;
        } else {
          os << "\\" << static_cast<int>(byte);
        }
      }
      os << "]";
    }
    os << "]";
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
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::PrintToString;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace {
uint32_t XdgToplevelId(sl_window* window) {
  assert(window->xdg_toplevel);
  return wl_proxy_get_id(reinterpret_cast<wl_proxy*>(window->xdg_toplevel));
}

uint32_t AuraSurfaceId(sl_window* window) {
  assert(window->aura_surface);
  return wl_proxy_get_id(reinterpret_cast<wl_proxy*>(window->aura_surface));
}

uint32_t AuraToplevelId(sl_window* window) {
  assert(window->aura_toplevel);
  return wl_proxy_get_id(reinterpret_cast<wl_proxy*>(window->aura_toplevel));
}

// This family of functions retrieves Sommelier's listeners for events received
// from the host, so we can call them directly in the test rather than
// (a) exporting the actual functions (which are typically static), or (b)
// creating a fake host compositor to dispatch events via libwayland
// (unnecessarily complicated).
const zaura_toplevel_listener* HostEventHandler(
    struct zaura_toplevel* aura_toplevel) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(aura_toplevel));
  EXPECT_NE(listener, nullptr);
  return static_cast<const zaura_toplevel_listener*>(listener);
}

const xdg_surface_listener* HostEventHandler(struct xdg_surface* xdg_surface) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(xdg_surface));
  EXPECT_NE(listener, nullptr);
  return static_cast<const xdg_surface_listener*>(listener);
}

const xdg_toplevel_listener* HostEventHandler(
    struct xdg_toplevel* xdg_toplevel) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(xdg_toplevel));
  EXPECT_NE(listener, nullptr);
  return static_cast<const xdg_toplevel_listener*>(listener);
}

const wl_output_listener* HostEventHandler(struct wl_output* output) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(output));
  EXPECT_NE(listener, nullptr);
  return static_cast<const wl_output_listener*>(listener);
}

const zaura_output_listener* HostEventHandler(struct zaura_output* output) {
  const void* listener =
      wl_proxy_get_listener(reinterpret_cast<wl_proxy*>(output));
  EXPECT_NE(listener, nullptr);
  return static_cast<const zaura_output_listener*>(listener);
}

}  // namespace

// Mock of Sommelier's Wayland connection to the host compositor.
class MockWaylandChannel : public WaylandChannel {
 public:
  MockWaylandChannel() {}

  MOCK_METHOD(int32_t, init, (), (override));
  MOCK_METHOD(bool, supports_dmabuf, (), (override));
  MOCK_METHOD(int32_t,
              create_context,
              (int& out_socket_fd),
              (override));  // NOLINT(runtime/references)
  MOCK_METHOD(int32_t,
              create_pipe,
              (int& out_pipe_fd),
              (override));  // NOLINT(runtime/references)
  MOCK_METHOD(int32_t,
              send,
              (const struct WaylandSendReceive& send),
              (override));
  MOCK_METHOD(
      int32_t,
      handle_channel_event,
      (enum WaylandChannelEvent & event_type,  // NOLINT(runtime/references)
       struct WaylandSendReceive& receive,     // NOLINT(runtime/references)
       int& out_read_pipe),                    // NOLINT(runtime/references)
      (override));

  MOCK_METHOD(int32_t,
              allocate,
              (const struct WaylandBufferCreateInfo& create_info,
               struct WaylandBufferCreateOutput&
                   create_output),  // NOLINT(runtime/references)
              (override));
  MOCK_METHOD(int32_t, sync, (int dmabuf_fd, uint64_t flags), (override));
  MOCK_METHOD(int32_t,
              handle_pipe,
              (int read_fd,
               bool readable,
               bool& hang_up),  // NOLINT(runtime/references)
              (override));
  MOCK_METHOD(size_t, max_send_size, (), (override));

 protected:
  ~MockWaylandChannel() override {}
};

// Match a WaylandSendReceive buffer containing exactly one Wayland message
// with given object ID and opcode.
MATCHER_P2(ExactlyOneMessage,
           object_id,
           opcode,
           std::string(negation ? "not " : "") +
               "exactly one Wayland message for object ID " +
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

// Match a WaylandSendReceive buffer containing at least one Wayland message
// with given object ID and opcode.
MATCHER_P2(AtLeastOneMessage,
           object_id,
           opcode,
           std::string(negation ? "no Wayland messages "
                                : "at least one Wayland message ") +
               "for object ID " + PrintToString(object_id) + ", opcode " +
               PrintToString(opcode)) {
  const struct WaylandSendReceive& send = arg;
  if (send.data_size < sizeof(uint32_t) * 2) {
    // Malformed packet (too short)
    return false;
  }
  for (uint32_t i = 0; i < send.data_size;) {
    uint32_t actual_object_id = *reinterpret_cast<uint32_t*>(send.data + i);
    uint32_t second_word = *reinterpret_cast<uint32_t*>(send.data + i + 4);
    uint16_t message_size_in_bytes = second_word >> 16;
    uint16_t actual_opcode = second_word & 0xffff;
    if (i + message_size_in_bytes > send.data_size) {
      // Malformed packet (stated message size overflows buffer)
      break;
    }
    if (object_id == actual_object_id && opcode == actual_opcode) {
      return true;
    }
    i += message_size_in_bytes;
  }
  return false;
}

// Match a WaylandSendReceive buffer containing a string.
// TODO(cpelling): This is currently very naive; it doesn't respect
// boundaries between messages or their arguments. Fix me.
MATCHER_P(AnyMessageContainsString,
          str,
          std::string("a Wayland message containing string ") + str) {
  const struct WaylandSendReceive& send = arg;
  size_t prefix_len = sizeof(uint32_t) * 2;
  std::string data_as_str(reinterpret_cast<char*>(send.data + prefix_len),
                          send.data_size - prefix_len);

  return data_as_str.find(str) != std::string::npos;
}

// Create a Wayland client and connect it to Sommelier's Wayland server.
//
// Sets up an actual Wayland client which connects over a Unix socket,
// and can make Wayland requests in the same way as a regular client.
// However, it has no event loop so doesn't process events.
class FakeWaylandClient {
 public:
  explicit FakeWaylandClient(struct sl_context* ctx) {
    // Create a socket pair for libwayland-server and libwayland-client
    // to communicate over.
    int rv = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv);
    errno_assert(!rv);
    // wl_client takes ownership of its file descriptor
    client = wl_client_create(ctx->host_display, sv[0]);
    errno_assert(!!client);
    sl_set_display_implementation(ctx, client);
    client_display = wl_display_connect_to_fd(sv[1]);
    EXPECT_NE(client_display, nullptr);

    client_registry = wl_display_get_registry(client_display);
    compositor = static_cast<wl_compositor*>(wl_registry_bind(
        client_registry, GlobalName(ctx, &wl_compositor_interface),
        &wl_compositor_interface, WL_COMPOSITOR_CREATE_SURFACE_SINCE_VERSION));
    wl_display_flush(client_display);
  }

  ~FakeWaylandClient() {
    wl_display_disconnect(client_display);
    client_display = nullptr;
    wl_client_destroy(client);
    client = nullptr;
  }

  // Bind to every advertised wl_output and return how many were bound.
  unsigned int BindToWlOutputs(struct sl_context* ctx) {
    unsigned int bound = 0;
    struct sl_global* global;
    wl_list_for_each(global, &ctx->globals, link) {
      if (global->interface == &wl_output_interface) {
        outputs.push_back(static_cast<wl_output*>(
            wl_registry_bind(client_registry, global->name, global->interface,
                             WL_OUTPUT_DONE_SINCE_VERSION)));
        bound++;
      }
    }
    wl_display_flush(client_display);
    return bound;
  }

  // Create a surface and return its ID
  uint32_t CreateSurface() {
    struct wl_surface* surface = wl_compositor_create_surface(compositor);
    wl_display_flush(client_display);
    return wl_proxy_get_id(reinterpret_cast<wl_proxy*>(surface));
  }

  // Represents the client from the server's (Sommelier's) end.
  struct wl_client* client = nullptr;

  std::vector<wl_output*> outputs;

 protected:
  // Find the "name" of Sommelier's global for a particular interface,
  // so our fake client can bind to it. This is cheating (normally
  // these names would come from wl_registry.global events) but
  // easier than setting up a proper event loop for this fake client.
  uint32_t GlobalName(struct sl_context* ctx,
                      const struct wl_interface* for_interface) {
    struct sl_global* global;
    wl_list_for_each(global, &ctx->globals, link) {
      if (global->interface == for_interface) {
        return global->name;
      }
    }
    assert(false);
    return 0;
  }

  int sv[2];

  // Represents the server (Sommelier) from the client end.
  struct wl_display* client_display = nullptr;
  struct wl_registry* client_registry = nullptr;
  struct wl_compositor* compositor = nullptr;
};

// Properties of a fake output (monitor) to advertise.
struct OutputConfig {
  int32_t x = 0;
  int32_t y = 0;
  int32_t physical_width_mm = 400;
  int32_t physical_height_mm = 225;
  int32_t width_pixels = 1920;
  int32_t height_pixels = 1080;
  int32_t transform = WL_OUTPUT_TRANSFORM_NORMAL;
  int32_t scale = 1;
  int32_t output_scale = 1000;
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

    // TODO(cpelling): Destroy context and any created windows?
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

    // Fake the host compositor advertising globals.
    sl_registry_handler(&ctx, registry, next_server_id++, "wl_compositor",
                        kMinHostWlCompositorVersion);
    EXPECT_NE(ctx.compositor, nullptr);
    sl_registry_handler(&ctx, registry, next_server_id++, "xdg_wm_base",
                        XDG_WM_BASE_GET_XDG_SURFACE_SINCE_VERSION);
    sl_registry_handler(&ctx, registry, next_server_id++, "zaura_shell",
                        ZAURA_TOPLEVEL_SET_WINDOW_BOUNDS_SINCE_VERSION);
  }

  // Set up one or more fake outputs for the test.
  void AdvertiseOutputs(FakeWaylandClient* client,
                        std::vector<OutputConfig> outputs) {
    // The host compositor should advertise a wl_output global for each output.
    // Sommelier will handle this by forwarding the globals to its client.
    for (const auto& output : outputs) {
      UNUSED(output);  // suppress -Wunused-variable
      uint32_t output_id = next_server_id++;
      sl_registry_handler(&ctx, wl_display_get_registry(ctx.display), output_id,
                          "wl_output", WL_OUTPUT_DONE_SINCE_VERSION);
    }

    // host_outputs populates when Sommelier's client binds to those globals.
    EXPECT_EQ(client->BindToWlOutputs(&ctx), outputs.size());
    Pump();  // process the bind requests

    // Now the outputs are populated, we can advertise their settings.
    sl_host_output* host_output;
    uint32_t i = 0;
    wl_list_for_each(host_output, &ctx.host_outputs, link) {
      ConfigureOutput(host_output, outputs[i]);
      i++;
    }
    // host_outputs should be the requested length.
    EXPECT_EQ(i, outputs.size());
  }

  void ConfigureOutput(sl_host_output* host_output,
                       const OutputConfig& config) {
    // This is mimicking components/exo/wayland/output_metrics.cc
    uint32_t flags = ZAURA_OUTPUT_SCALE_PROPERTY_CURRENT;
    if (config.output_scale == 1000) {
      flags |= ZAURA_OUTPUT_SCALE_PROPERTY_PREFERRED;
    }
    HostEventHandler(host_output->aura_output)
        ->scale(nullptr, host_output->aura_output, flags, config.output_scale);
    HostEventHandler(host_output->proxy)
        ->geometry(nullptr, host_output->proxy, config.x, config.y,
                   config.physical_width_mm, config.physical_height_mm,
                   WL_OUTPUT_SUBPIXEL_NONE, "ACME Corp", "Generic Monitor",
                   config.transform);
    HostEventHandler(host_output->proxy)
        ->mode(nullptr, host_output->proxy,
               WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
               config.width_pixels, config.height_pixels, 60);
    HostEventHandler(host_output->proxy)
        ->scale(nullptr, host_output->proxy, config.scale);
    HostEventHandler(host_output->proxy)->done(nullptr, host_output->proxy);
    Pump();
  }

  testing::NiceMock<MockWaylandChannel> mock_wayland_channel_;
  sl_context ctx;

  // IDs allocated by the server are in the range [0xff000000, 0xffffffff].
  uint32_t next_server_id = 0xff000000;
};

// Fixture for unit tests which exercise both Wayland and X11 functionality.
class X11Test : public WaylandTest {
 public:
  void InitContext() override {
    WaylandTest::InitContext();
    ctx.xwayland = 1;

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
    WaylandTest::Connect();

    // Pretend Xwayland has connected to Sommelier as a Wayland client.
    xwayland = std::make_unique<FakeWaylandClient>(&ctx);
    ctx.client = xwayland->client;

    // TODO(cpelling): mock out more of xcb so this isn't needed
    ctx.connection = xcb_connect(nullptr, nullptr);
  }

  ~X11Test() override { set_xcb_shim(nullptr); }

  uint32_t GenerateId() {
    static uint32_t id = 0;
    return ++id;
  }

  virtual sl_window* CreateWindowWithoutRole() {
    xcb_window_t window_id = GenerateId();
    sl_create_window(&ctx, window_id, 0, 0, 800, 600, 0);
    sl_window* window = sl_lookup_window(&ctx, window_id);
    EXPECT_NE(window, nullptr);
    return window;
  }

  virtual sl_window* CreateToplevelWindow() {
    sl_window* window = CreateWindowWithoutRole();

    // Pretend we created a frame window too
    window->frame_id = GenerateId();

    window->host_surface_id = xwayland->CreateSurface();
    sl_window_update(window);
    Pump();
    return window;
  }

 protected:
  NiceMock<MockXcbShim> xcb;
  std::unique_ptr<FakeWaylandClient> xwayland;
};

TEST_F(WaylandTest, CanCommitToEmptySurface) {
  wl_surface* surface = wl_compositor_create_surface(ctx.compositor->internal);
  wl_surface_commit(surface);
}

TEST_F(X11Test, TogglesFullscreenOnWmStateFullscreen) {
  // Arrange: Create an xdg_toplevel surface. Initially it's not fullscreen.
  sl_window* window = CreateToplevelWindow();
  uint32_t xdg_toplevel_id = XdgToplevelId(window);
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
  uint32_t xdg_toplevel_id = XdgToplevelId(window);
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
  uint32_t xdg_toplevel_id = XdgToplevelId(window);
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

TEST_F(X11Test, UpdatesApplicationIdFromContext) {
  sl_window* window = CreateToplevelWindow();
  Pump();

  window->managed = 1;  // pretend window is mapped
  // Should be ignored; global app id from context takes priority.
  window->app_id_property = "org.chromium.guest_os.termina.appid.from.window";

  ctx.application_id = "org.chromium.guest_os.termina.appid.from.context";
  sl_update_application_id(&ctx, window);
  EXPECT_CALL(mock_wayland_channel_,
              send(AllOf(ExactlyOneMessage(AuraSurfaceId(window),
                                           ZAURA_SURFACE_SET_APPLICATION_ID),
                         AnyMessageContainsString(ctx.application_id))))
      .RetiresOnSaturation();
  Pump();
}

TEST_F(X11Test, UpdatesApplicationIdFromWindow) {
  sl_window* window = CreateToplevelWindow();
  Pump();

  window->managed = 1;  // pretend window is mapped
  window->app_id_property = "org.chromium.guest_os.termina.appid.from.window";
  sl_update_application_id(&ctx, window);
  EXPECT_CALL(mock_wayland_channel_,
              send(AllOf(ExactlyOneMessage(AuraSurfaceId(window),
                                           ZAURA_SURFACE_SET_APPLICATION_ID),
                         AnyMessageContainsString(window->app_id_property))))
      .RetiresOnSaturation();
  Pump();
}

TEST_F(X11Test, UpdatesApplicationIdFromWindowClass) {
  sl_window* window = CreateToplevelWindow();
  Pump();

  window->managed = 1;                    // pretend window is mapped
  window->clazz = strdup("very_classy");  // not const, can't use a literal
  ctx.vm_id = "testvm";
  sl_update_application_id(&ctx, window);
  EXPECT_CALL(
      mock_wayland_channel_,
      send(AllOf(ExactlyOneMessage(AuraSurfaceId(window),
                                   ZAURA_SURFACE_SET_APPLICATION_ID),
                 AnyMessageContainsString(
                     "org.chromium.guest_os.testvm.wmclass.very_classy"))))
      .RetiresOnSaturation();
  Pump();
  free(window->clazz);
}

TEST_F(X11Test, UpdatesApplicationIdFromClientLeader) {
  sl_window* window = CreateToplevelWindow();
  Pump();

  window->managed = 1;  // pretend window is mapped
  window->client_leader = window->id;
  ctx.vm_id = "testvm";
  sl_update_application_id(&ctx, window);
  EXPECT_CALL(mock_wayland_channel_,
              send(AllOf(ExactlyOneMessage(AuraSurfaceId(window),
                                           ZAURA_SURFACE_SET_APPLICATION_ID),
                         AnyMessageContainsString(
                             "org.chromium.guest_os.testvm.wmclientleader."))))
      .RetiresOnSaturation();
  Pump();
}

TEST_F(X11Test, UpdatesApplicationIdFromXid) {
  sl_window* window = CreateToplevelWindow();
  Pump();

  window->managed = 1;  // pretend window is mapped
  ctx.vm_id = "testvm";
  sl_update_application_id(&ctx, window);
  EXPECT_CALL(mock_wayland_channel_,
              send(AllOf(ExactlyOneMessage(AuraSurfaceId(window),
                                           ZAURA_SURFACE_SET_APPLICATION_ID),
                         AnyMessageContainsString(
                             "org.chromium.guest_os.testvm.xid."))))
      .RetiresOnSaturation();
  Pump();
}

TEST_F(X11Test, NonExistentWindowDoesNotCrash) {
  // This test is testing cases where sl_lookup_window returns NULL

  // sl_handle_destroy_notify
  xcb_destroy_notify_event_t destroy_event;
  // Arrange: Use a window that does not exist.
  destroy_event.window = 123;
  // Act/Assert: Sommelier does not crash.
  sl_handle_destroy_notify(&ctx, &destroy_event);

  // sl_handle_client_message
  xcb_client_message_event_t message_event;
  message_event.window = 123;
  message_event.type = ctx.atoms[ATOM_WL_SURFACE_ID].value;
  sl_handle_client_message(&ctx, &message_event);
  message_event.type = ctx.atoms[ATOM_NET_ACTIVE_WINDOW].value;
  sl_handle_client_message(&ctx, &message_event);
  message_event.type = ctx.atoms[ATOM_NET_WM_MOVERESIZE].value;
  sl_handle_client_message(&ctx, &message_event);
  message_event.type = ctx.atoms[ATOM_NET_WM_STATE].value;
  sl_handle_client_message(&ctx, &message_event);
  message_event.type = ctx.atoms[ATOM_WM_CHANGE_STATE].value;
  sl_handle_client_message(&ctx, &message_event);

  // sl_handle_map_request
  xcb_map_request_event_t map_event;
  map_event.window = 123;
  sl_handle_map_request(&ctx, &map_event);

  // sl_handle_unmap_notify
  xcb_unmap_notify_event_t unmap_event;
  unmap_event.window = 123;
  sl_handle_unmap_notify(&ctx, &unmap_event);

  // sl_handle_configure_request
  xcb_configure_request_event_t configure_event;
  configure_event.window = 123;
  sl_handle_configure_request(&ctx, &configure_event);

  // sl_handle_focus_in
  xcb_focus_in_event_t focus_event;
  focus_event.event = 123;
  sl_handle_focus_in(&ctx, &focus_event);

  // sl_handle_property_notify
  xcb_property_notify_event_t notify_event;
  notify_event.window = 123;
  notify_event.atom = XCB_ATOM_WM_NAME;
  sl_handle_property_notify(&ctx, &notify_event);
  notify_event.atom = XCB_ATOM_WM_CLASS;
  sl_handle_property_notify(&ctx, &notify_event);
  notify_event.atom = ctx.application_id_property_atom;
  sl_handle_property_notify(&ctx, &notify_event);
  notify_event.atom = XCB_ATOM_WM_NORMAL_HINTS;
  sl_handle_property_notify(&ctx, &notify_event);
  notify_event.atom = XCB_ATOM_WM_HINTS;
  sl_handle_property_notify(&ctx, &notify_event);
  notify_event.atom = ATOM_MOTIF_WM_HINTS;
  sl_handle_property_notify(&ctx, &notify_event);
  notify_event.atom = ATOM_GTK_THEME_VARIANT;
  sl_handle_property_notify(&ctx, &notify_event);

  // sl_handle_reparent_notify
  // Put this one last and used a different window id as it creates a window.
  xcb_reparent_notify_event_t reparent_event;
  reparent_event.window = 1234;
  xcb_screen_t screen;
  screen.root = 1234;
  ctx.screen = &screen;
  reparent_event.parent = ctx.screen->root;
  reparent_event.x = 0;
  reparent_event.y = 0;
  sl_handle_reparent_notify(&ctx, &reparent_event);
}

#ifdef BLACK_SCREEN_FIX
TEST_F(X11Test, IconifySuppressesFullscreen) {
  // Arrange: Create an xdg_toplevel surface. Initially it's not iconified.
  sl_window* window = CreateToplevelWindow();
  uint32_t xdg_toplevel_id = XdgToplevelId(window);
  EXPECT_EQ(window->iconified, 0);

  // Act: Pretend an X11 client owns the surface, and requests to iconify it.
  xcb_client_message_event_t event;
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = window->id;
  event.type = ctx.atoms[ATOM_WM_CHANGE_STATE].value;
  event.data.data32[0] = WM_STATE_ICONIC;
  sl_handle_client_message(&ctx, &event);
  Pump();

  // Assert: Sommelier records the iconified state.
  EXPECT_EQ(window->iconified, 1);

  // Act: Pretend the surface is requested to be fullscreened.
  event.type = ctx.atoms[ATOM_NET_WM_STATE].value;
  event.data.data32[0] = NET_WM_STATE_ADD;
  event.data.data32[1] = ctx.atoms[ATOM_NET_WM_STATE_FULLSCREEN].value;
  event.data.data32[2] = 0;
  event.data.data32[3] = 0;
  event.data.data32[4] = 0;
  sl_handle_client_message(&ctx, &event);

  // Assert: Sommelier should not send the fullscreen call as we are iconified.
  EXPECT_CALL(
      mock_wayland_channel_,
      send((ExactlyOneMessage(xdg_toplevel_id, XDG_TOPLEVEL_SET_FULLSCREEN))))
      .Times(0);
  Pump();

  // Act: Pretend the surface receives focus.
  xcb_focus_in_event_t focus_event;
  focus_event.response_type = XCB_FOCUS_IN;
  focus_event.event = window->id;
  sl_handle_focus_in(&ctx, &focus_event);

  // Assert: The window is deiconified.
  EXPECT_EQ(window->iconified, 0);

  // Assert: Sommelier should now send the fullscreen call.
  EXPECT_CALL(
      mock_wayland_channel_,
      send((ExactlyOneMessage(xdg_toplevel_id, XDG_TOPLEVEL_SET_FULLSCREEN))))
      .Times(1);
  Pump();
}

TEST_F(X11Test, IconifySuppressesUnmaximize) {
  // Arrange: Create an xdg_toplevel surface. Initially it's not iconified.
  sl_window* window = CreateToplevelWindow();
  uint32_t xdg_toplevel_id = XdgToplevelId(window);
  EXPECT_EQ(window->iconified, 0);

  // Arrange: Maximize it.
  xcb_client_message_event_t event;
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = window->id;
  event.type = ctx.atoms[ATOM_NET_WM_STATE].value;
  event.data.data32[0] = NET_WM_STATE_ADD;
  event.data.data32[1] = ctx.atoms[ATOM_NET_WM_STATE_MAXIMIZED_VERT].value;
  event.data.data32[2] = ctx.atoms[ATOM_NET_WM_STATE_MAXIMIZED_HORZ].value;
  event.data.data32[3] = 0;
  event.data.data32[4] = 0;
  sl_handle_client_message(&ctx, &event);
  EXPECT_EQ(window->maximized, 1);

  // Act: Pretend an X11 client owns the surface, and requests to iconify it.
  event.type = ctx.atoms[ATOM_WM_CHANGE_STATE].value;
  event.data.data32[0] = WM_STATE_ICONIC;
  sl_handle_client_message(&ctx, &event);
  Pump();

  // Assert: Sommelier records the iconified state.
  EXPECT_EQ(window->iconified, 1);

  // Act: Pretend the surface is requested to be unmaximized.
  event.type = ctx.atoms[ATOM_NET_WM_STATE].value;
  event.data.data32[0] = NET_WM_STATE_REMOVE;
  event.data.data32[1] = ctx.atoms[ATOM_NET_WM_STATE_MAXIMIZED_VERT].value;
  event.data.data32[2] = ctx.atoms[ATOM_NET_WM_STATE_MAXIMIZED_HORZ].value;
  event.data.data32[3] = 0;
  event.data.data32[4] = 0;
  sl_handle_client_message(&ctx, &event);

  // Assert: Sommelier should not send the unmiximize call as we are iconified.
  EXPECT_CALL(
      mock_wayland_channel_,
      send((ExactlyOneMessage(xdg_toplevel_id, XDG_TOPLEVEL_UNSET_MAXIMIZED))))
      .Times(0);
  Pump();

  // Act: Pretend the surface receives focus.
  xcb_focus_in_event_t focus_event;
  focus_event.response_type = XCB_FOCUS_IN;
  focus_event.event = window->id;
  sl_handle_focus_in(&ctx, &focus_event);

  // Assert: The window is deiconified.
  EXPECT_EQ(window->iconified, 0);

  // Assert: Sommelier should now send the unmiximize call.
  EXPECT_CALL(
      mock_wayland_channel_,
      send((ExactlyOneMessage(xdg_toplevel_id, XDG_TOPLEVEL_UNSET_MAXIMIZED))))
      .Times(1);
  Pump();
}
#endif  // BLACK_SCREEN_FIX

// Matcher for the value_list argument of an X11 ConfigureWindow request,
// which is a const void* pointing to an int array whose size is implied by
// the flags argument.
MATCHER_P(ValueListMatches, expected, "") {
  const int* value_ptr = static_cast<const int*>(arg);
  std::vector<int> values;
  for (std::vector<int>::size_type i = 0; i < expected.size(); i++) {
    values.push_back(value_ptr[i]);
  }
  *result_listener << PrintToString(values);
  return values == expected;
}

TEST_F(X11Test, XdgToplevelConfigureTriggersX11Configure) {
  // Arrange
  AdvertiseOutputs(xwayland.get(), {OutputConfig()});
  sl_window* window = CreateToplevelWindow();
  window->managed = 1;     // pretend window is mapped
  window->size_flags = 0;  // no hinted position or size
  const int width = 1024;
  const int height = 768;

  // Assert: Set up expectations for Sommelier to send appropriate X11 requests.
  int x = (ctx.screen->width_in_pixels - width) / 2;
  int y = (ctx.screen->height_in_pixels - height) / 2;
  EXPECT_CALL(xcb, configure_window(
                       testing::_, window->frame_id,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                           XCB_CONFIG_WINDOW_BORDER_WIDTH,
                       ValueListMatches(std::vector({x, y, width, height, 0}))))
      .Times(1);
  EXPECT_CALL(xcb, configure_window(
                       testing::_, window->id,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                           XCB_CONFIG_WINDOW_BORDER_WIDTH,
                       ValueListMatches(std::vector({0, 0, width, height, 0}))))
      .Times(1);

  // Act: Pretend the host compositor sends us some xdg configure events.
  wl_array states;
  wl_array_init(&states);
  uint32_t* state =
      static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t)));
  *state = XDG_TOPLEVEL_STATE_ACTIVATED;

  HostEventHandler(window->xdg_toplevel)
      ->configure(nullptr, window->xdg_toplevel, width, height, &states);
  HostEventHandler(window->xdg_surface)
      ->configure(nullptr, window->xdg_surface, 123 /* serial */);
}

TEST_F(X11Test, AuraToplevelConfigureTriggersX11Configure) {
  // Arrange
  ctx.enable_x11_move_windows = true;
  AdvertiseOutputs(xwayland.get(), {OutputConfig()});
  ctx.use_direct_scale = 1;
  sl_window* window = CreateToplevelWindow();
  window->managed = 1;     // pretend window is mapped
  window->size_flags = 0;  // no hinted position or size
  const int x = 50;
  const int y = 60;
  const int width = 1024;
  const int height = 768;

  // Assert: Set up expectations for Sommelier to send appropriate X11 requests.
  EXPECT_CALL(xcb, configure_window(
                       testing::_, window->frame_id,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                           XCB_CONFIG_WINDOW_BORDER_WIDTH,
                       ValueListMatches(std::vector({x, y, width, height, 0}))))
      .Times(1);
  EXPECT_CALL(xcb, configure_window(
                       testing::_, window->id,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                           XCB_CONFIG_WINDOW_BORDER_WIDTH,
                       ValueListMatches(std::vector({0, 0, width, height, 0}))))
      .Times(1);

  // Act: Pretend the host compositor sends us some configure events.
  wl_array states;
  wl_array_init(&states);
  uint32_t* state =
      static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t)));
  *state = XDG_TOPLEVEL_STATE_ACTIVATED;

  HostEventHandler(window->aura_toplevel)
      ->configure(nullptr, window->aura_toplevel, x, y, width, height, &states);
  HostEventHandler(window->xdg_surface)
      ->configure(nullptr, window->xdg_surface, 123 /* serial */);
}

TEST_F(X11Test, AuraToplevelOriginChangeTriggersX11Configure) {
  // Arrange
  ctx.enable_x11_move_windows = true;
  AdvertiseOutputs(xwayland.get(), {OutputConfig()});
  sl_window* window = CreateToplevelWindow();
  window->managed = 1;     // pretend window is mapped
  window->size_flags = 0;  // no hinted position or size
  const int x = 50;
  const int y = 60;

  // Assert
  EXPECT_CALL(xcb, configure_window(testing::_, window->frame_id,
                                    XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                                    ValueListMatches(std::vector({x, y}))))
      .Times(1);

  // Act
  HostEventHandler(window->aura_toplevel)
      ->origin_change(nullptr, window->aura_toplevel, x, y);
}

// When the host compositor sends a window position, make sure we don't send
// a bounds request back. Otherwise we get glitching due to rounding and race
// conditions.
TEST_F(X11Test, AuraToplevelOriginChangeDoesNotRoundtrip) {
  // Arrange
  ctx.enable_x11_move_windows = true;
  AdvertiseOutputs(xwayland.get(), {OutputConfig()});
  sl_window* window = CreateToplevelWindow();
  window->managed = 1;     // pretend window is mapped
  window->size_flags = 0;  // no hinted position or size
  const int x = 50;
  const int y = 60;

  // Assert: set_window_bounds() never sent.
  EXPECT_CALL(mock_wayland_channel_,
              send(testing::Not(AtLeastOneMessage(
                  AuraToplevelId(window), ZAURA_TOPLEVEL_SET_WINDOW_BOUNDS))))
      .Times(testing::AtLeast(0));

  // Act
  HostEventHandler(window->aura_toplevel)
      ->origin_change(nullptr, window->aura_toplevel, x, y);
  Pump();
}

TEST_F(X11Test, X11ConfigureRequestPositionIsForwardedToAuraHost) {
  // Arrange
  ctx.enable_x11_move_windows = true;
  AdvertiseOutputs(xwayland.get(), {OutputConfig()});
  sl_window* window = CreateToplevelWindow();
  window->managed = 1;  // pretend window is mapped
  Pump();               // discard Wayland requests sent in setup

  // Assert
  EXPECT_CALL(mock_wayland_channel_,
              send(AtLeastOneMessage(AuraToplevelId(window),
                                     ZAURA_TOPLEVEL_SET_WINDOW_BOUNDS)))
      .RetiresOnSaturation();

  // Act
  xcb_configure_request_event_t configure = {
      .response_type = XCB_CONFIGURE_REQUEST,
      .sequence = 123,
      .parent = window->frame_id,
      .window = window->id,
      .x = 10,
      .y = 20,
      .width = 300,
      .height = 400,
      .value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                    XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT};
  sl_handle_configure_request(&ctx, &configure);
  Pump();
}

TEST_F(X11Test, X11ConfigureRequestWithoutPositionIsNotForwardedToAuraHost) {
  // Arrange
  ctx.enable_x11_move_windows = true;
  AdvertiseOutputs(xwayland.get(), {OutputConfig()});
  sl_window* window = CreateToplevelWindow();
  window->managed = 1;  // pretend window is mapped

  // Assert: set_window_bounds() never sent.
  EXPECT_CALL(mock_wayland_channel_,
              send(testing::Not(AtLeastOneMessage(
                  AuraToplevelId(window), ZAURA_TOPLEVEL_SET_WINDOW_BOUNDS))))
      .Times(testing::AtLeast(0));

  // Act
  xcb_configure_request_event_t configure = {
      .response_type = XCB_CONFIGURE_REQUEST,
      .sequence = 123,
      .parent = window->frame_id,
      .window = window->id,
      .width = 300,
      .height = 400,
      .value_mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT};
  sl_handle_configure_request(&ctx, &configure);
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
