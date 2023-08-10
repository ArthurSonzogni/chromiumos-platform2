// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/gtk_test_base.h"

#include <gtkmm/entry.h>
#include <gtkmm/popover.h>
#include <gtkmm/window.h>

// Tests involving multiple windows

namespace cros_im {
namespace test {

namespace {

// Popovers are transient windows, attached to a parent widget. This translates
// to a Wayland subsurface, so focus remains on the parent surface.
class GtkPopoverWindowTest : public GtkTestBase {
 public:
  void OnActivate() override {
    application_->add_window(window_);
#ifdef GTK4
    window_.set_child(outer_entry_);

    popover_.set_child(inner_entry_);
    popover_.set_parent(outer_entry_);
#else
    window_.add(outer_entry_);

    popover_.add(inner_entry_);
    popover_.set_relative_to(outer_entry_);
#endif
    outer_entry_.set_visible(true);
    window_.set_visible(true);

    inner_entry_.set_visible(true);
    // Don't show the popover yet
  }

 protected:
  Gtk::Window window_;
  Gtk::Entry outer_entry_;
  Gtk::Popover popover_;
  Gtk::Entry inner_entry_;
};

}  // namespace

TEST_F(GtkPopoverWindowTest, CommitString) {
  RunAndExpectBufferChangeTo(&outer_entry_, "ツ");
  popover_.set_visible(true);
  RunAndExpectBufferChangeTo(&inner_entry_, "ü");
  popover_.set_visible(false);

#ifdef GTK4
  // Popover needs to be manually disconnected from parent in GTK4.
  popover_.unparent();
  // In GTK4, GtkEntry will select all text on focus, which will cause the
  // existing text to be overwritten by newly committed text. To keep the test
  // expectations consistent, we unselect the text manually.
  RunUntilWidgetFocused(&outer_entry_);
  outer_entry_.select_region(1, 1);
#endif

  RunAndExpectBufferChangeTo(&outer_entry_, "ツ:)");
}

// TODO(b/264834882): Work out how to test keysyms here.

}  // namespace test
}  // namespace cros_im
