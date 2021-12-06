// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/mock_text_input.h"

#include "backend/test/backend_test.h"
#include "backend/test/request.h"

using cros_im::test::Request;

const wl_interface zwp_text_input_manager_v1_interface = {};

namespace {

// TODO(timloh): Support multiple text inputs.
static zwp_text_input_v1 text_input;

void HandleRequest(Request::RequestType type) {
  cros_im::test::BackendTest::GetInstance()->ProcessRequest(Request(type));
}

}  // namespace

zwp_text_input_v1* zwp_text_input_manager_v1_create_text_input(
    zwp_text_input_manager_v1*) {
  return &text_input;
}

void zwp_text_input_v1_set_user_data(zwp_text_input_v1*, void*) {
  // Not needed currently.
}

void zwp_text_input_v1_add_listener(zwp_text_input_v1* text_input,
                                    const zwp_text_input_v1_listener* listener,
                                    void* listener_data) {
  text_input->listener = listener;
  text_input->listener_data = listener_data;
}

void zwp_text_input_v1_destroy(zwp_text_input_v1*) {
  // TODO(timloh): This probably should destroy the text_input object.
  HandleRequest(Request::kDestroy);
}

void zwp_text_input_v1_activate(zwp_text_input_v1*, wl_seat*, wl_surface*) {
  HandleRequest(Request::kActivate);
}

void zwp_text_input_v1_deactivate(zwp_text_input_v1*, wl_seat*) {
  HandleRequest(Request::kDeactivate);
}

void zwp_text_input_v1_hide_input_panel(zwp_text_input_v1*) {
  HandleRequest(Request::kHideInputPanel);
}

void zwp_text_input_v1_reset(zwp_text_input_v1*) {
  HandleRequest(Request::kReset);
}

void zwp_text_input_v1_set_surrounding_text(zwp_text_input_v1*,
                                            const char* text,
                                            uint32_t cursor,
                                            uint32_t anchor) {
  HandleRequest(Request::kSetSurroundingText);
}

void zwp_text_input_v1_set_cursor_rectangle(
    zwp_text_input_v1*, int32_t x, int32_t y, int32_t width, int32_t height) {
  HandleRequest(Request::kSetCursorRectangle);
}

namespace cros_im {
namespace test {

zwp_text_input_v1* GetTextInput() {
  return &text_input;
}

}  // namespace test
}  // namespace cros_im
