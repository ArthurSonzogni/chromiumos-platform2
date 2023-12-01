// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREENS_SCREEN_DEBUG_OPTIONS_H_
#define MINIOS_SCREENS_SCREEN_DEBUG_OPTIONS_H_

#include <memory>
#include <string>

#include "minios/log_store_manager_interface.h"
#include "minios/process_manager_interface.h"
#include "minios/screens/screen_base.h"

namespace minios {

class ScreenDebugOptions : public ScreenBase {
 public:
  ScreenDebugOptions(
      std::shared_ptr<DrawInterface> draw_utils,
      std::shared_ptr<LogStoreManagerInterface> log_store_manager,
      std::shared_ptr<ProcessManagerInterface> process_manager,
      ScreenControllerInterface* screen_controller);

  ~ScreenDebugOptions() = default;

  ScreenDebugOptions(const ScreenDebugOptions&) = delete;
  ScreenDebugOptions& operator=(const ScreenDebugOptions&) = delete;

  void Show() override;
  void Reset() override;
  void OnKeyPress(int key_changed) override;
  ScreenType GetType() override;
  std::string GetName() override;
  bool MoveForward(brillo::ErrorPtr* error) override;
  bool MoveBackward(brillo::ErrorPtr* error) override;

 private:
  FRIEND_TEST(ScreenDebugOptionsTest, ClearLogs);

  // Updates buttons with current selection.
  void ShowButtons();

  std::shared_ptr<LogStoreManagerInterface> log_store_manager_;
  std::shared_ptr<ProcessManagerInterface> process_manager_;
};

}  // namespace minios

#endif  // MINIOS_SCREENS_SCREEN_DEBUG_OPTIONS_H_
