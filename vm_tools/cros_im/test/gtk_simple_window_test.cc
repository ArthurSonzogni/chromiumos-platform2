// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtkmm/main.h>
#include <gtkmm/textview.h>
#include <gtkmm/window.h>
#include "gtest/gtest.h"

namespace {

// TODO(timloh): Extract core logic to a separate class.
class GtkSimpleWindowTest : public ::testing::Test {
 public:
  GtkSimpleWindowTest() {
    auto test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string full_name =
        std::string(test_info->test_case_name()) + "." + test_info->name();
    setenv("CROS_TEST_FULL_NAME", full_name.c_str(), true);

    window_.add(text_view_);
    text_view_.show();
    window_.show();
  }

  ~GtkSimpleWindowTest() override = default;

 protected:
  std::string GetText() { return text_view_.get_buffer()->get_text(); }

  void RunUntilTextChanged() {
    connection_ =
        text_view_.get_buffer()->property_text().signal_changed().connect(
            sigc::mem_fun(*this, &GtkSimpleWindowTest::OnSignal));
    Gtk::Main::run();
  }

  void OnSignal() {
    connection_.disconnect();
    Gtk::Main::quit();
  }

  Gtk::Main main_;
  Gtk::Window window_;
  Gtk::TextView text_view_;

  sigc::connection connection_;
};

}  // namespace

TEST_F(GtkSimpleWindowTest, CommitStringSingleCharacters) {
  RunUntilTextChanged();
  EXPECT_EQ(GetText(), "c");
  RunUntilTextChanged();
  EXPECT_EQ(GetText(), "co");
  RunUntilTextChanged();
  EXPECT_EQ(GetText(), "coo");
  RunUntilTextChanged();
  EXPECT_EQ(GetText(), "cool");
  RunUntilTextChanged();
  EXPECT_EQ(GetText(), "cool!");
  RunUntilTextChanged();
  EXPECT_EQ(GetText(), "cool!\n");
}

TEST_F(GtkSimpleWindowTest, CommitStringLongStrings) {
  RunUntilTextChanged();
  std::string expectation = "hello world!\n";
  EXPECT_EQ(GetText(), expectation);

  RunUntilTextChanged();
  expectation += "committing a long string all at once!\n";
  EXPECT_EQ(GetText(), expectation);

  RunUntilTextChanged();
  expectation += "string string string! :)\n";
  EXPECT_EQ(GetText(), expectation);
}
