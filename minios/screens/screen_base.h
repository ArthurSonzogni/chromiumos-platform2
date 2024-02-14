// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREENS_SCREEN_BASE_H_
#define MINIOS_SCREENS_SCREEN_BASE_H_

#include <memory>
#include <string>

#include <base/logging.h>
#include <brillo/errors/error.h>
#include <gtest/gtest_prod.h>
#include <minios/proto_bindings/minios.pb.h>

#include "minios/draw_utils.h"
#include "minios/screen_types.h"

namespace minios {

class ScreenControllerInterface;

extern const int kBtnYStep;

// `ScreenInterface` has the common functions for each Screen in miniOS. Screen
// Classes must be able to show their UI components, respond to key events, and
// reset their internal state.
class ScreenInterface {
 public:
  virtual ~ScreenInterface() = default;

  // Shows the screen and all base components.
  virtual void Show() = 0;

  // Changes the screen based on given user input. Re-shows the necessary parts
  // of the screen.
  virtual void OnKeyPress(int key_changed) = 0;

  // Resets screen state.
  virtual void Reset() = 0;

  // Gets the `ScreenType` for each screen.
  virtual ScreenType GetType() = 0;

  // Get the name of the screen as a string.
  virtual std::string GetName() = 0;

  // Get the `State` for each screen.
  virtual State GetState() = 0;

  // Advance to the next screen iff all requirements are satisfied.
  virtual bool MoveForward(brillo::ErrorPtr* error) = 0;

  // Advance to the previous screen iff all requirements are satisfied.
  virtual bool MoveBackward(brillo::ErrorPtr* error) = 0;
};

class ScreenBase : public ScreenInterface {
 public:
  ScreenBase(int button_count,
             int index,
             State::States state,
             std::shared_ptr<DrawInterface> draw_utils,
             ScreenControllerInterface* screen_controller);

  void SetButtonCountForTest(int button_count) { button_count_ = button_count; }

  int GetButtonCountForTest() { return button_count_; }

  void SetIndexForTest(int index) { index_ = index; }

  int GetIndexForTest() { return index_; }

  // `ScreenInterface` overrides.
  bool MoveForward(brillo::ErrorPtr* error) override;
  bool MoveBackward(brillo::ErrorPtr* error) override;
  State GetState() override { return state_; }

 protected:
  FRIEND_TEST(ScreenBaseTest, UpdateButtons);
  FRIEND_TEST(ScreenBaseTest, UpdateButtonsIsDetachable);

  // Set the current state. Notify if the state changes.
  void SetState(State::States state);

  // Changes the index and enter value based on the given key. Unknown keys are
  // ignored and index is kept within the range of menu items. Enter is whether
  // the enter key was pressed and released.
  virtual void UpdateButtonsIndex(int key, bool* enter);

  // The number of buttons or dropdown items on the page.
  int button_count_;

  // The current screen index.
  int index_;

  // The current `State`.
  State state_;

  std::shared_ptr<DrawInterface> draw_utils_;

  ScreenControllerInterface* screen_controller_;
};

}  // namespace minios

#endif  // MINIOS_SCREENS_SCREEN_BASE_H_
