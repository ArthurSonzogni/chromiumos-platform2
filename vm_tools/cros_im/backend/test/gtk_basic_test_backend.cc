// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/backend_test.h"

namespace cros_im {
namespace test {

// These tests exist to verify the requests sent in basic cases. There is no
// 'correct' sequence of requests, as Chrome may handle different sequences
// identically. This file documents the current behaviour and ensures changes
// to it are noticed.

BACKEND_TEST(GtkBasicTest, TextViewShownImmediately) {
  Expect(Request::kCreateTextInput);

  Expect(Request::kSetCursorRectangle);
  ExpectSetSurroundingText("", 0, 0);
  Expect(Request::kActivate);
  ExpectSetContentType(3, 0);
  Expect(Request::kShowInputPanel);

  ExpectSetSurroundingText("", 0, 0);
  Expect(Request::kHideInputPanel);
  Expect(Request::kDeactivate);
  Expect(Request::kDestroy);
}

BACKEND_TEST(GtkBasicTest, SwitchFocus) {
  Expect<0>(Request::kCreateTextInput);

  Expect<0>(Request::kSetCursorRectangle);
  ExpectSetSurroundingText<0>("", 0, 0);
  Expect<0>(Request::kActivate);
  ExpectSetContentType<0>(3, 0);
  Expect<0>(Request::kShowInputPanel);
  ExpectSetSurroundingText<0>("", 0, 0);

  Expect<0>(Request::kSetCursorRectangle);
  ExpectSetSurroundingText<0>("", 0, 0);

  Expect<1>(Request::kCreateTextInput);
  Expect<1>(Request::kSetCursorRectangle);
  ExpectSetSurroundingText<1>("", 0, 0);

  Expect<0>(Request::kHideInputPanel);
  Expect<0>(Request::kDeactivate);

  Expect<1>(Request::kActivate);
  ExpectSetContentType<1>(3, 0);
  Expect<1>(Request::kShowInputPanel);
  ExpectSetSurroundingText<1>("", 0, 0);

  Expect<0>(Request::kSetCursorRectangle);
  ExpectSetSurroundingText<0>("", 0, 0);

  Expect<1>(Request::kSetCursorRectangle);
  ExpectSetSurroundingText<1>("", 0, 0);

  Expect<1>(Request::kHideInputPanel);
  Expect<1>(Request::kDeactivate);
  Expect<1>(Request::kDestroy);

  Expect<0>(Request::kDestroy);
}

}  // namespace test
}  // namespace cros_im
