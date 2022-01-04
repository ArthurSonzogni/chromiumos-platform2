// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_TEST_GTK_TEST_BASE_H_
#define VM_TOOLS_CROS_IM_TEST_GTK_TEST_BASE_H_

#include <gtest/gtest.h>
#include <gtkmm/main.h>
#include <gtkmm/textview.h>
#include <gtkmm/window.h>
#include <string>

namespace cros_im {
namespace test {

// Test fixture base class for initializing GTK and settings environment
// variables for the backend. The test runner test/run_tests.py should be used
// to run these tests to capture backend failures and allow running multiple
// tests.
class GtkTestBase : public ::testing::Test {
 public:
  GtkTestBase() {
    auto test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string full_name =
        std::string(test_info->test_case_name()) + "." + test_info->name();
    setenv("CROS_TEST_FULL_NAME", full_name.c_str(), true);
  }

  ~GtkTestBase() override = default;

 protected:
  // e.g. Glib::SignalProxyProperty or Glib::SignalProxy<void()>.
  template <typename Signal>
  void RunUntilSignal(Signal signal) {
    ASSERT_FALSE(connection_);
    connection_ = signal.connect(sigc::mem_fun(*this, &GtkTestBase::OnSignal));
    Gtk::Main::run();
  }

  void OnSignal() {
    connection_.disconnect();
    Gtk::Main::quit();
  }

  Gtk::Main main_;

  sigc::connection connection_;
};

// Test fixture for using a single TextView widget.
class GtkSimpleTextViewTest : public GtkTestBase {
 public:
  GtkSimpleTextViewTest() {
    window_.add(text_view_);
    text_view_.show();
    window_.show();
  }

  ~GtkSimpleTextViewTest() override = default;

 protected:
  std::string GetText() { return text_view_.get_buffer()->get_text(); }

  void RunUntilTextChanged() {
    RunUntilSignal(text_view_.get_buffer()->property_text().signal_changed());
  }

  void RunAndExpectTextChangeTo(const std::string& expect) {
    RunUntilTextChanged();
    EXPECT_EQ(GetText(), expect);
  }

  Gtk::Window window_;
  Gtk::TextView text_view_;
};

}  // namespace test
}  // namespace cros_im

#endif  // VM_TOOLS_CROS_IM_TEST_GTK_TEST_BASE_H_
