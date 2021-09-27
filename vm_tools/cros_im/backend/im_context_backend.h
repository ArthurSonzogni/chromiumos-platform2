// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_BACKEND_IM_CONTEXT_BACKEND_H_
#define VM_TOOLS_CROS_IM_BACKEND_IM_CONTEXT_BACKEND_H_

#include <cstdint>
#include <string>
#include <vector>

struct wl_array;
struct wl_seat;
struct wl_surface;
struct zwp_text_input_v1;
struct zwp_text_input_v1_listener;

namespace cros_im {

// As per the preedit_styling event, index and length provide the range to
// style in bytes, and style is a zwp_text_input_v1_preedit_style value.
struct PreeditStyle {
  uint32_t index;
  uint32_t length;
  uint32_t style;
};

// IMContextBackend wraps a text_input_v1 object.
class IMContextBackend {
 public:
  class Observer {
   public:
    virtual ~Observer() {}
    // |preedit| is UTF-8, |cursor| is in bytes.
    virtual void SetPreedit(const std::string& preedit,
                            int cursor,
                            const std::vector<PreeditStyle>& styles) = 0;
    virtual void Commit(const std::string& text) = 0;
  };

  explicit IMContextBackend(Observer* observer);
  ~IMContextBackend();

  void Activate(wl_seat* seat, wl_surface* surface);
  void Deactivate();
  void Reset();
  void SetSurrounding(const char* text, int cursor_index);
  void SetCursorLocation(int x, int y, int width, int height);

 private:
  // We usually initialize in the constructor, but if the Wayland connection
  // isn't ready yet we retry in Activate().
  // TODO(timloh): We should queue up requests from the front-end and send them
  // once the connection is ready.
  void MaybeInitialize();

  // Wayland text_input_v1 event handlers.
  void SetPreedit(uint32_t serial, const char* text, const char* commit);
  void SetPreeditStyling(uint32_t index, uint32_t length, uint32_t style);
  void SetPreeditCursor(uint32_t cursor);
  void Commit(uint32_t serial, const char* text);

  static const zwp_text_input_v1_listener text_input_listener_;
  zwp_text_input_v1* text_input_ = nullptr;
  wl_seat* seat_ = nullptr;

  Observer* observer_ = nullptr;

  // Pre-edit updates are split across several events.
  int cursor_pos_ = 0;
  std::vector<PreeditStyle> styles_;
};

}  // namespace cros_im

#endif  // VM_TOOLS_CROS_IM_BACKEND_IM_CONTEXT_BACKEND_H_
