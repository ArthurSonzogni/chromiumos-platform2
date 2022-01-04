// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/gtk_test_base.h"

namespace cros_im {
namespace test {

namespace {

using GtkKeySymTextViewTest = GtkSimpleTextViewTest;

}  // namespace

TEST_F(GtkKeySymTextViewTest, TextInput) {
  RunAndExpectTextChangeTo("d");
  RunAndExpectTextChangeTo("do");
  RunAndExpectTextChangeTo("dog");
  RunAndExpectTextChangeTo("dog~");
}

TEST_F(GtkKeySymTextViewTest, NonAscii) {
  RunAndExpectTextChangeTo(u8"£");
  RunAndExpectTextChangeTo(u8"£Ü");
  RunAndExpectTextChangeTo(u8"£ÜŅ");
  RunAndExpectTextChangeTo(u8"£ÜŅァ");
  RunAndExpectTextChangeTo(u8"£ÜŅァژ");
  RunAndExpectTextChangeTo(u8"£ÜŅァژნ");
  RunAndExpectTextChangeTo(u8"£ÜŅァژნο");
}

}  // namespace test
}  // namespace cros_im
