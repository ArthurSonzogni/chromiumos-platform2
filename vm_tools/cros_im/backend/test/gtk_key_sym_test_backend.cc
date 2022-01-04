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

}  // namespace test
}  // namespace cros_im
