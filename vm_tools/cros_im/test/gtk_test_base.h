// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_TEST_GTK_TEST_BASE_H_
#define VM_TOOLS_CROS_IM_TEST_GTK_TEST_BASE_H_

#include <gtest/gtest.h>
#include <gtkmm/application.h>
#include <gtkmm/textview.h>
#include <gtkmm/window.h>

#ifdef GTK4
#include <gtkmm/eventcontrollerfocus.h>
#endif

#include <string>

#include "util/logging.h"

namespace cros_im {
namespace test {

class GtkTestBase;

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

    application_ = Gtk::Application::create(full_name.c_str());
  }
  void SetUp() override {
    application_->signal_activate().connect(
        sigc::mem_fun(*this, &GtkTestBase::OnActivate));

    // An application must be registered and activated for it to be used in
    // tests.
    ASSERT_TRUE(application_->register_application());
    application_->activate();
  }

  virtual void OnActivate() = 0;

  ~GtkTestBase() override = default;

 protected:
  struct PreeditData {
    GtkTestBase* test_fixture;
    std::string preedit_result;
  };

  // e.g. Glib::SignalProxyProperty or Glib::SignalProxy<void()>.
  template <typename Signal>
  void RunUntilSignal(Signal signal) {
    ASSERT_FALSE(connection_);
    connection_ = signal.connect(sigc::mem_fun(*this, &GtkTestBase::OnSignal));
    RunMainLoop();
  }

  void OnSignal() {
    connection_.disconnect();
    main_loop_running_ = false;
  }

  // Gtk::TextView or Gtk::Entry
  template <typename T>
  void ExpectBufferIs(T* text_widget_, const std::string& expect) {
    EXPECT_EQ(text_widget_->get_buffer()->get_text(), expect.c_str());
  }

  // This does not include pre-edit text if present.
  template <typename T>
  void RunAndExpectBufferChangeTo(T* text_widget_, const std::string& expect) {
    RunUntilSignal(
        text_widget_->get_buffer()->property_text().signal_changed());
    ExpectBufferIs(text_widget_, expect);
  }

  template <typename T>
  void RunAndExpectWidgetPreeditChangeTo(T* text_widget_,
                                         const std::string& expect) {
    // preedit-changed isn't hooked up to gtkmm, so manually set up the signal.
    PreeditData preedit_data = {.test_fixture = this, .preedit_result = ""};
    gulong handler_id =
        g_signal_connect(text_widget_->gobj(), "preedit-changed",
                         G_CALLBACK(OnPreeditChanged), &preedit_data);

    RunMainLoop();
    g_signal_handler_disconnect(text_widget_->gobj(), handler_id);

    EXPECT_EQ(preedit_data.preedit_result, expect);
  }

  static void OnPreeditChanged(GtkTextView* self,
                               char* preedit,
                               gpointer user_data) {
    auto preedit_data = static_cast<PreeditData*>(user_data);
    preedit_data->preedit_result = preedit;
    preedit_data->test_fixture->main_loop_running_ = false;
  }

#ifdef GTK4
  template <typename T>
  void RunUntilWidgetFocused(T* widget) {
    auto focus_controller = Gtk::EventControllerFocus::create();
    widget->add_controller(focus_controller);
    RunUntilSignal(focus_controller->signal_enter());
  }
#else
  template <typename T>
  void RunUntilWidgetFocused(T* widget) {
    // focus-in-event isn't hooked up to gtkmm, so manually set up the signal.
    gulong handler_id = g_signal_connect(widget->gobj(), "focus-in-event",
                                         G_CALLBACK(OnFocusInEvent), this);
    RunMainLoop();
    g_signal_handler_disconnect(widget->gobj(), handler_id);
  }

  static gboolean OnFocusInEvent(GtkTextView* self,
                                 GdkEventFocus* event,
                                 gpointer user_data) {
    static_cast<GtkTestBase*>(user_data)->main_loop_running_ = false;
    // Don't consume the event.
    return false;
  }
#endif

  template <typename T>
  void MoveBufferCursor(T* text_widget_, int index) {
    auto buffer = text_widget_->get_buffer();
    buffer->place_cursor(buffer->get_iter_at_offset(index));
  }

  void RunUntilIdle() {
    GMainContext* main_context = g_main_context_default();
    while (g_main_context_pending(main_context))
      g_main_context_iteration(main_context, /*may_block=*/TRUE);
  }

  // The existing g_application_run() in GTK will complete all queued requests
  // upon quitting, and is expected to be run once only. In our tests we want to
  // temporarily pause and re-run the application, so use our own method to run
  // the application manually.
  // Reference: https://github.com/GNOME/glib/blob/main/gio/gapplication.c
  void RunMainLoop() {
    GMainContext* context = g_main_context_default();
    gboolean acquired_context = g_main_context_acquire(context);
    if (!acquired_context) {
      LOG(ERROR) << "Failed to acquire main context to run application.";
      return;
    }

    main_loop_running_ = true;
    while (main_loop_running_) {
      g_main_context_iteration(context, /*may_block=*/TRUE);
    }

    g_main_context_release(context);
  }

  Glib::RefPtr<Gtk::Application> application_;
  bool main_loop_running_;
  sigc::connection connection_;
};

// Test fixture for using a single TextView widget.
class GtkSimpleTextViewTest : public GtkTestBase {
 public:
  ~GtkSimpleTextViewTest() override = default;

  void OnActivate() override {
    application_->add_window(window_);

#ifdef GTK4
    window_.set_child(text_view_);
#else
    window_.add(text_view_);
#endif
    text_view_.set_visible(true);
    window_.set_visible(true);
  }

 protected:
  void RunAndExpectTextChangeTo(const std::string& expect) {
    RunAndExpectBufferChangeTo(&text_view_, expect);
  }

  void ExpectTextIs(const std::string& expect) {
    ExpectBufferIs(&text_view_, expect);
  }

  void RunAndExpectPreeditChangeTo(const std::string& expect) {
    RunAndExpectWidgetPreeditChangeTo(&text_view_, expect);
  }

  void RunUntilFocused() { RunUntilWidgetFocused(&text_view_); }

  // `index` is in characters, not bytes.
  void MoveCursor(int index) { MoveBufferCursor(&text_view_, index); }

  void SetText(const std::string& text) {
    text_view_.get_buffer()->set_text(text);
  }

  Gtk::Window window_;
  Gtk::TextView text_view_;
};

}  // namespace test
}  // namespace cros_im

#endif  // VM_TOOLS_CROS_IM_TEST_GTK_TEST_BASE_H_
