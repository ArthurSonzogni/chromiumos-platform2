// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xcb/xproto.h>

#include "testing/x11-test-base.h"
#include "xcb/fake-xcb-shim.h"

namespace vm_tools {
namespace sommelier {

using X11EventTest = X11TestBase;

TEST_F(X11EventTest, MapRequestCreatesFrameWindow) {
  sl_window* window = CreateWindowWithoutRole();
  xcb_map_request_event_t event;
  event.response_type = XCB_MAP_REQUEST;
  event.window = window->id;
  EXPECT_EQ(window->frame_id, XCB_WINDOW_NONE);

  EXPECT_CALL(xcb, generate_id).WillOnce(testing::Return(456));
  sl_handle_map_request(&ctx, &event);

  EXPECT_EQ(window->frame_id, 456u);
}

TEST_F(X11EventTest, MapRequestIssuesMapWindow) {
  sl_window* window = CreateWindowWithoutRole();
  xcb_map_request_event_t event;
  event.response_type = XCB_MAP_REQUEST;
  event.window = window->id;

  EXPECT_CALL(xcb, generate_id).WillOnce(testing::Return(456));
  EXPECT_CALL(xcb, map_window(testing::_, window->id)).Times(1);
  EXPECT_CALL(xcb, map_window(testing::_, 456u)).Times(1);

  sl_handle_map_request(&ctx, &event);
}

TEST_F(X11EventTest, MapRequestGetsWmName) {
  std::string windowName("Fred");
  xcb.DelegateToFake();
  sl_window* window = CreateWindowWithoutRole();
  xcb.create_window(nullptr, 32, window->id, XCB_WINDOW_NONE, 0, 0, 800, 600, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0,
                    nullptr);
  xcb.change_property(nullptr, XCB_PROP_MODE_REPLACE, window->id,
                      XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, windowName.size(),
                      windowName.c_str());
  EXPECT_EQ(window->name, nullptr);

  xcb_map_request_event_t event;
  event.response_type = XCB_MAP_REQUEST;
  event.window = window->id;
  sl_handle_map_request(&ctx, &event);

  EXPECT_EQ(window->name, windowName);
}

}  // namespace sommelier
}  // namespace vm_tools
