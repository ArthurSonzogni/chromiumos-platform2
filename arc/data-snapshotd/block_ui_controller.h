// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_DATA_SNAPSHOTD_BLOCK_UI_CONTROLLER_H_
#define ARC_DATA_SNAPSHOTD_BLOCK_UI_CONTROLLER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/command_line.h>
#include <base/process/launch.h>

namespace arc {
namespace data_snapshotd {

// Exposed for testing purposes.
base::CommandLine GetShowScreenCommandLine();
base::CommandLine GetUpdateProgressCommandLine(int percent);

base::LaunchOptions GetShowScreenOptions();
base::LaunchOptions GetUpdateProgressOptions();

// This class controls a system update_arc_data_snapshot: shows the screen in
// ShowScreen, updates a progress bar and dismisses it in dtor.
class BlockUiController final {
 public:
  using LaunchProcessCallback = base::RepeatingCallback<bool(
      const base::CommandLine&, const base::LaunchOptions&)>;

  BlockUiController();
  BlockUiController(const BlockUiController&) = delete;
  BlockUiController& operator=(const BlockUiController&) = delete;
  ~BlockUiController();

  static std::unique_ptr<BlockUiController> CreateForTesting(
      LaunchProcessCallback callback);

  // Shows update_arc_data_snapshot. Returns true if succeeds to show a UI
  // screen.
  bool ShowScreen();

  // Updates a progress bar with a percentage of installed apps of the required
  // number of apps.
  // Returns true if succeeds to update a progress bar.
  bool UpdateProgress(int percent);

  // Returns true if the screen is shown with no error.
  bool shown() const { return shown_; }

 private:
  explicit BlockUiController(LaunchProcessCallback callback);

  // True if the screen is shown.
  bool shown_ = false;

  LaunchProcessCallback launch_process_callback_;
};

}  // namespace data_snapshotd
}  // namespace arc

#endif  // ARC_DATA_SNAPSHOTD_BLOCK_UI_CONTROLLER_H_
