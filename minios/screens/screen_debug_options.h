// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREENS_SCREEN_DEBUG_OPTIONS_H_
#define MINIOS_SCREENS_SCREEN_DEBUG_OPTIONS_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_temp_dir.h>
#include <brillo/namespaces/platform.h>
#include <brillo/udev/udev.h>

#include "minios/blkid_wrapper.h"
#include "minios/log_store_manager_interface.h"
#include "minios/process_manager.h"
#include "minios/screens/screen_base.h"

namespace minios {

extern const char kDiskStorageDevice[];

class ScreenDebugOptions : public ScreenBase {
 public:
  enum class ButtonIndex : int {
    kLanguageDropdown = 0,
    kDeviceDropDown,
    kEraseLogs,
    kMessagLog,
    kBack,
    kPower
  };

  ScreenDebugOptions(
      std::shared_ptr<DrawInterface> draw_utils,
      std::shared_ptr<LogStoreManagerInterface> log_store_manager,
      std::shared_ptr<ProcessManagerInterface> process_manager,
      ScreenControllerInterface* screen_controller,
      std::shared_ptr<BlkIdWrapperInterface> blk_id_wrapper =
          std::make_shared<BlkIdWrapper>(),
      std::shared_ptr<brillo::Platform> platform =
          std::make_shared<brillo::Platform>());

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
  FRIEND_TEST(ScreenDebugOptionsTest, UpdateStorageDevices);
  FRIEND_TEST(ScreenDebugOptionsTest, SaveLogsToDiskTest);
  FRIEND_TEST(ScreenDebugOptionsTest, SaveLogsToDeviceTest);
  FRIEND_TEST(ScreenDebugOptionsTest, SaveLogsFailureTest);
  FRIEND_TEST(ScreenDebugOptionsTest, HandleRemovableDeviceTest);
  FRIEND_TEST(ScreenDebugOptionsTest, UpdateStorageDevicesWithDisk);
  FRIEND_TEST(ScreenDebugOptionsTest, DrawSaveResultSuccess);
  FRIEND_TEST(ScreenDebugOptionsTest, DrawSaveResultFailure);
  FRIEND_TEST(ScreenDebugOptionsTest, MountFileSystemFailure);
  FRIEND_TEST(ScreenDebugOptionsTest, MountRemovableDeviceTest);

  // The internal states of `ScreenDebug` dropdown.
  enum class DropDownState {
    kDropdownClosed = 0,
    kDropdownOpen = 1,
  };

  // Updates buttons with current selection.
  void ShowButtons();

  void ShowCollapsedDeviceOptions(bool is_selected);
  void ShowOpenDeviceOptions(int current_index);

  // Update `storage_devices_` with any removable devices that may have been
  // plugged or unplugged.
  void UpdateStorageDevices(
      std::unique_ptr<brillo::Udev> udev = brillo::Udev::Create());

  // Save logs to removable device specified in `device_path`. Returns true on
  // success, false otherwise.
  bool SaveLogsToDevice(base::FilePath device_path) const;

  void HandleButtonSelection();
  void HandleDeviceSelection();

  bool MountRemovableDevice(base::ScopedTempDir& temp_mount_folder,
                            const base::FilePath& device_path) const;

  std::shared_ptr<LogStoreManagerInterface> log_store_manager_;
  std::shared_ptr<ProcessManagerInterface> process_manager_;

  DropDownState state_;

  // Pair of device labels (for UI) and details.
  std::vector<std::pair<std::string, std::optional<base::FilePath>>>
      storage_devices_;

  // Number of items in the device dropdown.
  const int max_dropdown_items_;

  std::shared_ptr<BlkIdWrapperInterface> blk_id_wrapper_;
  std::shared_ptr<brillo::Platform> platform_;
};

}  // namespace minios

#endif  // MINIOS_SCREENS_SCREEN_DEBUG_OPTIONS_H_
