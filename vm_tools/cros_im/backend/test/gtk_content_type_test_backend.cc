// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/backend_test.h"

#include "backend/text_input_enums.h"

namespace cros_im {
namespace test {

namespace {

// Different to .._HINT_DEFAULT, see todo in GetZwpHintsFromGtk for details.
uint32_t kDefaultHints = ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_CORRECTION |
                         ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_COMPLETION;

}  // namespace

BACKEND_TEST(GtkContentTypeTest, ContentHints) {
  ExpectCreateTextInput<0>(CreateTextInputOptions::kDefault);
  Ignore<0>(Request::kActivate);
  Ignore<0>(Request::kDeactivate);
  Ignore<0>(Request::kDestroy);
  Ignore<0>(Request::kReset);
  Ignore<0>(Request::kSetCursorRectangle);
  Ignore<0>(Request::kSetSurroundingText);
  Ignore<0>(Request::kHideInputPanel);

  ExpectSetContentType<0>(ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_CORRECTION |
                              ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_COMPLETION |
                              ZWP_TEXT_INPUT_V1_CONTENT_HINT_UPPERCASE,
                          0);
  Expect<0>(Request::kShowInputPanel);
  SendCommitString<0>("a");

  ExpectCreateTextInput<1>(CreateTextInputOptions::kDefault);
  Ignore<1>(Request::kActivate);
  Ignore<1>(Request::kDeactivate);
  Ignore<1>(Request::kDestroy);
  Ignore<1>(Request::kReset);
  Ignore<1>(Request::kSetCursorRectangle);
  Ignore<1>(Request::kSetSurroundingText);
  Ignore<1>(Request::kHideInputPanel);

  ExpectSetContentType<1>(ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_COMPLETION |
                              ZWP_TEXT_INPUT_V1_CONTENT_HINT_LOWERCASE,
                          0);
  Expect<1>(Request::kShowInputPanel);
  SendCommitString<1>("b");

  ExpectSetContentType<0>(ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_CORRECTION |
                              ZWP_TEXT_INPUT_V1_CONTENT_HINT_AUTO_COMPLETION |
                              ZWP_TEXT_INPUT_V1_CONTENT_HINT_TITLECASE,
                          0);
  Expect<0>(Request::kShowInputPanel);
  SendCommitString<0>("c");

  ExpectSetContentType<1>(ZWP_TEXT_INPUT_V1_CONTENT_HINT_DEFAULT, 0);
  Expect<1>(Request::kShowInputPanel);
  SendCommitString<1>("d");

  ExpectSetContentType<0>(kDefaultHints, 0);
  // No call to ShowInputPanel
  SendCommitString<0>("e");
}

BACKEND_TEST(GtkContentTypeTest, ContentPurpose) {
  ExpectCreateTextInput<0>(CreateTextInputOptions::kDefault);
  Ignore<0>(Request::kActivate);
  Ignore<0>(Request::kDeactivate);
  Ignore<0>(Request::kDestroy);
  Ignore<0>(Request::kReset);
  Ignore<0>(Request::kSetCursorRectangle);
  Ignore<0>(Request::kSetSurroundingText);
  Ignore<0>(Request::kShowInputPanel);
  Ignore<0>(Request::kHideInputPanel);

  ExpectSetContentType<0>(kDefaultHints,
                          ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_ALPHA);
  SendCommitString<0>("a");

  ExpectCreateTextInput<1>(CreateTextInputOptions::kDefault);
  Ignore<1>(Request::kActivate);
  Ignore<1>(Request::kDeactivate);
  Ignore<1>(Request::kDestroy);
  Ignore<1>(Request::kReset);
  Ignore<1>(Request::kSetCursorRectangle);
  Ignore<1>(Request::kSetSurroundingText);
  Ignore<1>(Request::kShowInputPanel);
  Ignore<1>(Request::kHideInputPanel);

  ExpectSetContentType<1>(kDefaultHints,
                          ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_DIGITS);
  SendCommitString<1>("1");

  ExpectSetContentType<0>(kDefaultHints,
                          ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_EMAIL);
  SendCommitString<0>("c");

  ExpectSetContentType<1>(ZWP_TEXT_INPUT_V1_CONTENT_HINT_PASSWORD,
                          ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_DIGITS);
  SendCommitString<1>("0");

  ExpectSetContentType<0>(ZWP_TEXT_INPUT_V1_CONTENT_HINT_PASSWORD,
                          ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_PASSWORD);
  SendCommitString<0>("e");
}

}  // namespace test
}  // namespace cros_im
