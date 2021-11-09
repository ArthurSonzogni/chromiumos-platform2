// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/im_context_backend.h"

#include <cassert>
#include <cstring>
#include <utility>
#include <wayland-client.h>

#include "backend/wayland_manager.h"
#include "text-input-unstable-v1-client-protocol.h"  // NOLINT(build/include_directory)

namespace cros_im {

namespace {

template <auto F, typename... Args>
auto Fwd(void* data, zwp_text_input_v1* text_input, Args... args) {
  // The backend object should still be alive as libwayland-client drops events
  // sent to destroyed objects.
  return (reinterpret_cast<IMContextBackend*>(data)->*F)(args...);
}

template <typename... Args>
auto DoNothing(void* data, zwp_text_input_v1* text_input, Args... args) {}

}  // namespace

const zwp_text_input_v1_listener IMContextBackend::text_input_listener_ = {
    .enter = DoNothing,
    .leave = DoNothing,
    .modifiers_map = DoNothing,
    .input_panel_state = DoNothing,
    .preedit_string = Fwd<&IMContextBackend::SetPreedit>,
    .preedit_styling = Fwd<&IMContextBackend::SetPreeditStyling>,
    .preedit_cursor = Fwd<&IMContextBackend::SetPreeditCursor>,
    .commit_string = Fwd<&IMContextBackend::Commit>,
    .cursor_position = DoNothing,
    .delete_surrounding_text = DoNothing,
    .keysym = Fwd<&IMContextBackend::KeySym>,
    .language = DoNothing,
    .text_direction = DoNothing,
};

IMContextBackend::IMContextBackend(Observer* observer) : observer_(observer) {
  assert(WaylandManager::HasInstance());
  MaybeInitialize();
}

IMContextBackend::~IMContextBackend() {
  if (text_input_)
    zwp_text_input_v1_destroy(text_input_);
}

void IMContextBackend::Activate(wl_seat* seat, wl_surface* surface) {
  MaybeInitialize();

  if (!text_input_) {
    printf("The text input manager is not ready yet or not available.\n");
    return;
  }

  seat_ = seat;
  zwp_text_input_v1_activate(text_input_, seat_, surface);
}

void IMContextBackend::Deactivate() {
  if (!text_input_)
    return;
  if (!seat_) {
    printf("Attempted to deactivate text input which was not activated.\n");
    return;
  }
  zwp_text_input_v1_hide_input_panel(text_input_);
  zwp_text_input_v1_deactivate(text_input_, seat_);
  seat_ = nullptr;
}

void IMContextBackend::Reset() {
  if (!text_input_)
    return;
  zwp_text_input_v1_reset(text_input_);
}

void IMContextBackend::SetSurrounding(const char* text, int cursor_index) {
  if (!text_input_)
    return;
  zwp_text_input_v1_set_surrounding_text(text_input_, text, cursor_index,
                                         cursor_index);
}

void IMContextBackend::SetCursorLocation(int x, int y, int width, int height) {
  if (!text_input_)
    return;
  zwp_text_input_v1_set_cursor_rectangle(text_input_, x, y, width, height);
}

void IMContextBackend::MaybeInitialize() {
  if (text_input_)
    return;
  // May return nullptr.
  text_input_ =
      WaylandManager::Get()->CreateTextInput(&text_input_listener_, this);
}

void IMContextBackend::SetPreeditStyling(uint32_t index,
                                         uint32_t length,
                                         uint32_t style) {
  styles_.push_back({.index = index, .length = length, .style = style});
}

void IMContextBackend::SetPreeditCursor(uint32_t cursor) {
  cursor_pos_ = cursor;
}

// TODO(timloh): Work out what we need to do with serials.

void IMContextBackend::SetPreedit(uint32_t serial,
                                  const char* text,
                                  const char* commit) {
  observer_->SetPreedit(text, cursor_pos_, styles_);
  cursor_pos_ = 0;
  styles_.clear();
}

void IMContextBackend::Commit(uint32_t serial, const char* text) {
  styles_.clear();
  observer_->Commit(text);
}

void IMContextBackend::KeySym(uint32_t serial,
                              uint32_t time,
                              uint32_t sym,
                              uint32_t state,
                              uint32_t modifiers) {
  // TODO(timloh): Handle remaining arguments.
  observer_->KeySym(sym, state == WL_KEYBOARD_KEY_STATE_PRESSED
                             ? KeyState::kPressed
                             : KeyState::kReleased);
}

}  // namespace cros_im
