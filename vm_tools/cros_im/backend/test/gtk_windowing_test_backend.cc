// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/backend_test.h"

#include <xkbcommon/xkbcommon-keysyms.h>

namespace cros_im {
namespace test {

BACKEND_TEST(GtkPopoverWindowTest, CommitString) {
  ExpectCreateTextInput<0>(CreateTextInputOptions::kIgnoreCommon);
  ExpectCreateTextInput<1>(CreateTextInputOptions::kIgnoreCommon);

  Expect<0>(Request::kActivate);
  SendCommitString<0>("ツ");
  Expect<0>(Request::kDeactivate);
  Expect<1>(Request::kActivate);
  Expect<1>(Request::kReset);

  SendCommitString<1>("ü");
  Expect<1>(Request::kDeactivate);
  Expect<0>(Request::kActivate);

  SendCommitString<0>(":)");

  Expect<1>(Request::kReset);
  Expect<1>(Request::kDestroy);
  Expect<0>(Request::kDeactivate);
  Expect<0>(Request::kReset);
  Expect<0>(Request::kDestroy);
}

BACKEND_TEST(GtkPopoverWindowTest, KeySym) {
  ExpectCreateTextInput<0>(CreateTextInputOptions::kIgnoreCommon);
  ExpectCreateTextInput<1>(CreateTextInputOptions::kIgnoreCommon);

  Expect<0>(Request::kActivate);
  SendKeySym<0>(XKB_KEY_a);
  Expect<0>(Request::kDeactivate);
  Expect<1>(Request::kActivate);
  Expect<1>(Request::kReset);

  SendKeySym<1>(XKB_KEY_ssharp);
  Expect<1>(Request::kDeactivate);
  Expect<0>(Request::kActivate);

  SendKeySym<0>(XKB_KEY_oe);
  Expect<0>(Request::kDeactivate);
  Expect<1>(Request::kActivate);
  Expect<1>(Request::kReset);

  SendKeySym<1>(XKB_KEY_p);

  Expect<1>(Request::kDeactivate);
  Expect<1>(Request::kReset);
  Expect<1>(Request::kDestroy);
  Expect<0>(Request::kActivate);
  Expect<0>(Request::kDeactivate);
  Expect<0>(Request::kReset);
  Expect<0>(Request::kDestroy);
}

}  // namespace test
}  // namespace cros_im
