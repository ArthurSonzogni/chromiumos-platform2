// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/backend_test.h"

namespace cros_im {
namespace test {

BACKEND_TEST(GtkCommitStringTest, SingleCharacters) {
  Ignore(Request::kSetCursorRectangle);
  Ignore(Request::kSetSurroundingText);
  Ignore(Request::kHideInputPanel);

  Expect(Request::kActivate);

  SendCommitString("c");
  SendCommitString("o");
  SendCommitString("o");
  SendCommitString("l");
  SendCommitString("!");
  SendCommitString("\n");

  Expect(Request::kDeactivate);
  Expect(Request::kDestroy);
}

BACKEND_TEST(GtkCommitStringTest, LongStrings) {
  Ignore(Request::kSetCursorRectangle);
  Ignore(Request::kSetSurroundingText);
  Ignore(Request::kHideInputPanel);

  Expect(Request::kActivate);

  SendCommitString("hello world!\n");
  SendCommitString("committing a long string all at once!\n");
  SendCommitString("string string string! :)\n");

  Expect(Request::kDeactivate);
  Expect(Request::kDestroy);
}

}  // namespace test
}  // namespace cros_im
