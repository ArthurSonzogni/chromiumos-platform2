// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/backend_test.h"

#include <xkbcommon/xkbcommon-keysyms.h>

namespace cros_im {
namespace test {

BACKEND_TEST(GtkKeySymTextViewTest, TextInput) {
  Ignore(Request::kSetCursorRectangle);
  Ignore(Request::kSetSurroundingText);
  Ignore(Request::kHideInputPanel);

  Expect(Request::kActivate);
  SendKeySym(XKB_KEY_d);
  SendKeySym(XKB_KEY_o);
  SendKeySym(XKB_KEY_g);
  SendKeySym(XKB_KEY_asciitilde);

  Expect(Request::kDeactivate);
  Expect(Request::kDestroy);
}

BACKEND_TEST(GtkKeySymTextViewTest, NonAscii) {
  Ignore(Request::kSetCursorRectangle);
  Ignore(Request::kSetSurroundingText);
  Ignore(Request::kHideInputPanel);

  Expect(Request::kActivate);

  SendKeySym(XKB_KEY_sterling);
  SendKeySym(XKB_KEY_Udiaeresis);
  SendKeySym(XKB_KEY_Ncedilla);
  SendKeySym(XKB_KEY_kana_a);
  SendKeySym(XKB_KEY_Arabic_jeh);
  SendKeySym(XKB_KEY_Georgian_nar);
  SendKeySym(XKB_KEY_Greek_omicron);

  Expect(Request::kDeactivate);
  Expect(Request::kDestroy);
}

BACKEND_TEST(GtkKeySymTextViewTest, Whitespace) {
  Ignore(Request::kSetCursorRectangle);
  Ignore(Request::kSetSurroundingText);
  Ignore(Request::kHideInputPanel);

  Expect(Request::kActivate);

  SendKeySym(XKB_KEY_Return);
  // As per gtk_text_view_key_press_event in gtk_textview.c.
  Expect(Request::kReset);
  SendKeySym(XKB_KEY_Tab);
  SendKeySym(XKB_KEY_space);
  SendKeySym(XKB_KEY_Return);
  SendKeySym(XKB_KEY_space);
  SendKeySym(XKB_KEY_Tab);

  Expect(Request::kDeactivate);
  Expect(Request::kDestroy);
}

BACKEND_TEST(GtkKeySymTextViewTest, Backspace) {
  Ignore(Request::kSetCursorRectangle);
  Ignore(Request::kSetSurroundingText);
  Ignore(Request::kHideInputPanel);

  Expect(Request::kActivate);

  SendKeySym(XKB_KEY_a);
  SendKeySym(XKB_KEY_BackSpace);
  // As per gtk_text_view_backspace in gtk_textview.c.
  Expect(Request::kReset);
  SendKeySym(XKB_KEY_Return);
  SendKeySym(XKB_KEY_b);
  SendKeySym(XKB_KEY_BackSpace);
  SendKeySym(XKB_KEY_c);
  SendKeySym(XKB_KEY_BackSpace);
  SendKeySym(XKB_KEY_BackSpace);

  Expect(Request::kDeactivate);
  Expect(Request::kDestroy);
}

BACKEND_TEST(GtkKeySymEntryTest, Enter) {
  Ignore(Request::kSetCursorRectangle);
  Ignore(Request::kSetSurroundingText);
  Ignore(Request::kHideInputPanel);

  Expect(Request::kActivate);

  SendKeySym(XKB_KEY_e);
  SendKeySym(XKB_KEY_Return);
  // As per gtk_entry_key_press in gtkentry.c
  Expect(Request::kReset);

  Expect(Request::kDeactivate);
  Expect(Request::kReset);
  Expect(Request::kDestroy);
}

BACKEND_TEST(GtkKeySymEntryTest, Tab) {
  Ignore(Request::kSetCursorRectangle);
  Ignore(Request::kSetSurroundingText);
  Ignore(Request::kHideInputPanel);

  Expect(Request::kActivate);

  SendKeySym(XKB_KEY_t);
  SendKeySym(XKB_KEY_Tab);

  Expect(Request::kDeactivate);
  Expect(Request::kReset);
  Expect(Request::kDestroy);
}

}  // namespace test
}  // namespace cros_im
