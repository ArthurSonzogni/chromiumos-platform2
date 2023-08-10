// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/gtk_test_base.h"

#include <gtkmm/box.h>
#include <gtkmm/entry.h>
#include <gtkmm/enums.h>
#include <gtkmm/textview.h>

namespace cros_im {
namespace test {

namespace {

#ifdef GTK4
#define HINT(hint) Gtk::InputHints::hint
#define PURPOSE(purpose) Gtk::InputPurpose::purpose
#else
#define HINT(hint) Gtk::INPUT_HINT_##hint
#define PURPOSE(purpose) Gtk::INPUT_PURPOSE_##purpose
#endif

// Live changes to content type are not detected, test by switching focus
// between two text fields.
class GtkContentTypeTest : public GtkTestBase {
 public:
  void OnActivate() override {
    application_->add_window(window_);
#ifdef GTK4
    window_.set_child(box_);
#else
    window_.add(box_);
#endif
    box_.set_visible(true);
    window_.set_visible(true);

    // Add after window is visible so the text view is not focused yet.
#ifdef GTK4
    box_.append(text_view_);
#else
    box_.add(text_view_);
#endif
    text_view_.set_visible(true);
  }

 protected:
  Gtk::Window window_;
  Gtk::Box box_;
  Gtk::TextView text_view_;
  Gtk::Entry entry_;
};

}  // namespace

TEST_F(GtkContentTypeTest, ContentHints) {
  text_view_.set_input_hints(HINT(SPELLCHECK) | HINT(UPPERCASE_CHARS));
  text_view_.grab_focus();
  RunAndExpectBufferChangeTo(&text_view_, "a");

  // Delay adding entry_ so text_input creation order is obvious.
#ifdef GTK4
  box_.append(entry_);
#else
  box_.add(entry_);
#endif
  entry_.set_visible(true);
  entry_.set_input_hints(HINT(WORD_COMPLETION) | HINT(NO_SPELLCHECK) |
                         HINT(LOWERCASE));
  entry_.grab_focus();
  RunAndExpectBufferChangeTo(&entry_, "b");

  // NO_EMOJI is ignored.
  text_view_.set_input_hints(HINT(UPPERCASE_WORDS) | HINT(NO_EMOJI));
  text_view_.grab_focus();
  RunAndExpectBufferChangeTo(&text_view_, "ac");

  // VERTICAL_WRITING and EMOJI are ignored.
  entry_.set_input_hints(HINT(UPPERCASE_SENTENCES) | HINT(VERTICAL_WRITING) |
                         HINT(EMOJI));
  entry_.grab_focus_without_selecting();
  RunAndExpectBufferChangeTo(&entry_, "bd");

  text_view_.set_input_hints(HINT(INHIBIT_OSK));
  text_view_.grab_focus();
  RunAndExpectBufferChangeTo(&text_view_, "ace");
}

TEST_F(GtkContentTypeTest, ContentPurpose) {
  text_view_.set_input_purpose(PURPOSE(ALPHA));
  text_view_.grab_focus();
  RunAndExpectBufferChangeTo(&text_view_, "a");

  // Delay adding entry_ so text_input creation order is obvious.
#ifdef GTK4
  box_.append(entry_);
#else
  box_.add(entry_);
#endif
  entry_.set_visible(true);
  entry_.set_input_purpose(PURPOSE(DIGITS));

  // Like a password field but does not actually set hint or purpose.
  entry_.set_visibility(false);
  entry_.grab_focus();
  RunAndExpectBufferChangeTo(&entry_, "1");
  entry_.set_visibility(true);

  text_view_.set_input_purpose(PURPOSE(EMAIL));
  text_view_.grab_focus();
  RunAndExpectBufferChangeTo(&text_view_, "ac");

  entry_.set_input_purpose(PURPOSE(PIN));
  entry_.grab_focus_without_selecting();
  RunAndExpectBufferChangeTo(&entry_, "10");

  text_view_.set_input_purpose(PURPOSE(PASSWORD));
  text_view_.grab_focus();
  RunAndExpectBufferChangeTo(&text_view_, "ace");
}

}  // namespace test
}  // namespace cros_im
