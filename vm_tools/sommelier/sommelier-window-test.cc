// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "testing/x11-test-base.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

namespace vm_tools {
namespace sommelier {

using ::testing::_;
using ::testing::AllOf;
using ::testing::PrintToString;

using X11Test = X11TestBase;

TEST_F(X11Test, TogglesFullscreenOnWmStateFullscreen) {
  // Arrange: Create an xdg_toplevel surface. Initially it's not fullscreen.
  AdvertiseOutputs(xwayland.get(), {OutputConfig()});
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
  // This test is testing cases where sl_lookup_window returns nullptr

  // sl_handle_destroy_notify
  xcb_destroy_notify_event_t destroy_event;
  // Arrange: Use a window that does not exist.
  destroy_event.window = 123;
  // Act/Assert: Sommelier does not crash.
  sl_handle_destroy_notify(&ctx, &destroy_event);

  // sl_handle_client_message
  xcb_client_message_event_t message_event;
  message_event.window = 123;
  message_event.data.data32[0] = WM_STATE_ICONIC;
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
  unmap_event.response_type = 0;
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
  // (output width/height - width/height) / 2
  int x = 448;
  int y = 156;
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

TEST_F(X11Test, XdgToplevelConfigureCentersWindowOnRotatedOutput) {
  // Arrange
  AdvertiseOutputs(xwayland.get(), {{.transform = WL_OUTPUT_TRANSFORM_90}});
  sl_window* window = CreateToplevelWindow();
  window->managed = 1;     // pretend window is mapped
  window->size_flags = 0;  // no hinted position or size
  const int width = 1024;
  const int height = 768;

  // Assert: Set up expectations for Sommelier to send appropriate X11 requests.
  // (rotated output width/height - width/height) / 2
  int x = 28;
  int y = 576;
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

TEST_F(X11Test,
       XdgToplevelConfigureCentersWindowCorrectlyWhenMultipleOutputsExist) {
  // Arrange
  AdvertiseOutputs(
      xwayland.get(),
      {{.x = 0, .y = 0, .width_pixels = 1920, .height_pixels = 1080},
       {.x = 1920, .y = 500}});
  sl_window* window = CreateToplevelWindow();
  window->managed = 1;     // pretend window is mapped
  window->size_flags = 0;  // no hinted position or size
  const int width = 1024;
  const int height = 768;
  struct sl_host_output* output = nullptr;
  output = ctx.host_outputs[1];
  HostEventHandler(window->paired_surface->proxy)
      ->enter(nullptr, window->paired_surface->proxy, output->proxy);

  // Assert: Set up expectations for Sommelier to send appropriate X11 requests.
  int x = 1920 + 448;
  int y = 156;
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

TEST_F(X11DirectScaleTest, AuraToplevelConfigureTriggersX11Configure) {
  // Arrange
  ctx.enable_x11_move_windows = true;
  AdvertiseOutputs(xwayland.get(), {OutputConfig()});
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

TEST_F(X11Test,
       X11ConfigureRequestPositionForwardingIgnoresStaleAuraToplevelConfigure) {
  // Arrange
  ctx.enable_x11_move_windows = true;
  AdvertiseOutputs(xwayland.get(), {OutputConfig()});
  sl_window* window = CreateToplevelWindow();
  window->managed = 1;  // pretend window is mapped
  const int width = 300;
  const int height = 400;

  // Position requested by the client.
  const int client_requested_x = 10;
  const int client_requested_y = 0;

  // Stale position received from the host compositor.
  const int stale_x = 50;
  const int stale_y = 60;

  // Host compositor's adjusted response to the client's request.
  // (In this scenario, it moved the window down so its server-side
  // decorations wouldn't be offscreen.)
  const int granted_x = client_requested_x;
  const int granted_y = 32;

  //
  // Assert
  //

  // Barrier should prevent forwarding the host's stale coords to the X server.
  EXPECT_CALL(
      xcb, configure_window(testing::_, window->frame_id, testing::_,
                            ValueListMatches(std::vector({stale_x, stale_y}))))
      .Times(0);

  // Do forward the correct coordinates to the X server.
  EXPECT_CALL(xcb, configure_window(
                       testing::_, window->frame_id,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                           XCB_CONFIG_WINDOW_BORDER_WIDTH,
                       ValueListMatches(std::vector(
                           {granted_x, granted_y, width, height, 0}))))
      .Times(1);

  // The reparented child window may also get configured.
  // The details are not important for this test case.
  EXPECT_CALL(xcb,
              configure_window(testing::_, window->id, testing::_, testing::_))
      .Times(testing::AtLeast(0));

  //
  // Act
  //

  // An incoming ConfigureRequest sends set_window_bounds(), and sets up the
  // event barrier.
  xcb_configure_request_event_t configure = {
      .response_type = XCB_CONFIGURE_REQUEST,
      .sequence = 123,
      .parent = window->frame_id,
      .window = window->id,
      .x = client_requested_x,
      .y = client_requested_y,
      .width = width,
      .height = height,
      .value_mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                    XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT};
  sl_handle_configure_request(&ctx, &configure);
  EXPECT_NE(window->configure_event_barrier, nullptr);

  // Meanwhile, host compositor is sending stale position data, both via the
  // regular configure sequence and via origin_change events.
  wl_array states;
  wl_array_init(&states);
  uint32_t* state =
      static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t)));
  *state = XDG_TOPLEVEL_STATE_ACTIVATED;

  uint32_t serial = 120;

  HostEventHandler(window->aura_toplevel)
      ->configure(nullptr, window->aura_toplevel, stale_x, stale_y, width,
                  height, &states);
  HostEventHandler(window->xdg_surface)
      ->configure(nullptr, window->xdg_surface, serial++);

  HostEventHandler(window->aura_toplevel)
      ->origin_change(nullptr, window->aura_toplevel, stale_x, stale_y);
  Pump();

  // Exo catches up to the set_window_bounds() request. It modifies the
  // requested coords slightly and returns them in a fresh configure sequence.
  HostEventHandler(window->aura_toplevel)
      ->configure(nullptr, window->aura_toplevel, granted_x, granted_y, width,
                  height, &states);
  HostEventHandler(window->xdg_surface)
      ->configure(nullptr, window->xdg_surface, serial++);

  // Exo catches up to the event barrier.
  HostEventHandler(window->configure_event_barrier)
      ->done(nullptr, window->configure_event_barrier, serial++);

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
