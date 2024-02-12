// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens/screen_debug_options.h"

#include <linux/input.h>
#include <sys/mount.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/to_string.h>
#include <brillo/namespaces/platform.h>
#include <minios/proto_bindings/minios.pb.h>

#include "base/command_line.h"
#include "minios/blkid_wrapper.h"
#include "minios/draw_utils.h"
#include "minios/log_store_manager_interface.h"
#include "minios/process_manager_interface.h"
#include "minios/screen_types.h"
#include "minios/utils.h"

namespace minios {

namespace {

constexpr int kItemHeight = 40;
constexpr int kNumButtons = 6;

const base::FilePath kArchiveFileName{"minios_logs.tar"};
typedef std::vector<std::pair<ScreenDebugOptions::ButtonIndex, std::string>>
    ButtonList;

constexpr char kEraseLogImage[] = "btn_erase_logs";
constexpr char kMessagLogImage[] = "btn_message_log";
constexpr char kBackImage[] = "btn_back";

const ButtonList kButtonLabels{
    {ScreenDebugOptions::ButtonIndex::kEraseLogs, kEraseLogImage},
    {ScreenDebugOptions::ButtonIndex::kMessagLog, kMessagLogImage},
    {ScreenDebugOptions::ButtonIndex::kBack, kBackImage}};

constexpr char kFilesystemTypeTag[] = "TYPE";
constexpr char kLabelTag[] = "LABEL";

}  // namespace

const char kDiskStorageDevice[] = "Disk";

// TODO(b/191139789): minios: clean up, combine generic screens into one.
ScreenDebugOptions::ScreenDebugOptions(
    std::shared_ptr<DrawInterface> draw_utils,
    std::shared_ptr<LogStoreManagerInterface> log_store_manager,
    std::shared_ptr<ProcessManagerInterface> process_manager,
    ScreenControllerInterface* screen_controller,
    std::shared_ptr<BlkIdWrapperInterface> blk_id_wrapper,
    std::shared_ptr<brillo::Platform> platform)
    : ScreenBase(
          /*button_count=*/kNumButtons,
          /*index_=*/static_cast<int>(ButtonIndex::kDeviceDropDown),
          State::DEBUG_OPTIONS,
          draw_utils,
          screen_controller),
      log_store_manager_(log_store_manager),
      process_manager_(process_manager),
      state_(DropDownState::kDropdownClosed),
      max_dropdown_items_(
          (draw_utils_->GetFreconCanvasSize() / 2 - kBtnYStep * 2) /
              kItemHeight -
          1),
      blk_id_wrapper_(blk_id_wrapper),
      platform_(platform) {}

void ScreenDebugOptions::Show() {
  draw_utils_->MessageBaseScreen();
  int frecon_size = draw_utils_->GetFreconCanvasSize();
  const auto kX = -frecon_size / 2 + kDefaultMessageWidth / 2;
  const auto kY = -frecon_size / 2 + 220 + 18;
  draw_utils_->ShowMessage("title_debug_options", kX, kY);
  ShowButtons();
  SetState(State::DEBUG_OPTIONS);
}

void ScreenDebugOptions::ShowCollapsedDeviceOptions(bool is_selected) {
  const int frecon_canvas_size = draw_utils_->GetFreconCanvasSize();
  constexpr int kOffsetY = -100;
  constexpr int kBackgroundOffset = 145;
  constexpr int kTextOffset = 80;
  const int kBgX = -frecon_canvas_size / 2 + kBackgroundOffset;
  const int kTextX = -frecon_canvas_size / 2 + kTextOffset;

  draw_utils_->ShowDropDownClosed(kBgX, kOffsetY, kTextX, "btn_copy_logs",
                                  "settings.png", is_selected);
}

void ScreenDebugOptions::UpdateStorageDevices(
    std::unique_ptr<brillo::Udev> udev) {
  storage_devices_.clear();
  // Storing logs to disk is always an option.
  storage_devices_.emplace_back(kDiskStorageDevice, std::nullopt);
  button_count_ = 2;

  std::vector<base::FilePath> device_paths;
  if (!GetRemovableDevices(device_paths, std::move(udev))) {
    LOG(WARNING) << "Couldn't scan for removable devices.";
    return;
  }
  for (const auto& device : device_paths) {
    // Generate a label if we can't find one for this device.
    const auto label =
        blk_id_wrapper_->CheckAndGetTagValue(kLabelTag, device.value());
    const auto& display_label =
        label.has_value()
            ? label.value()
            : ("Removable Device " + base::ToString(storage_devices_.size()));

    storage_devices_.emplace_back(display_label, device);
    LOG(INFO) << "Added, device_label=" << display_label << " path=" << device;
  }

  button_count_ = storage_devices_.size() + 1;
}

void ScreenDebugOptions::ShowOpenDeviceOptions(int current_index) {
  const int frecon_canvas_size = draw_utils_->GetFreconCanvasSize();
  int offset_y = -100 + kBtnYStep;
  constexpr int kBackgroundOffset = 260;
  const int kBackgroundX = -frecon_canvas_size / 2 + kBackgroundOffset;
  constexpr int kBackgroundWidth = 520;
  constexpr int kHalfBackgroundWidth = (kBackgroundWidth / 2) - 40;
  const int kTextOffsetX =
      (draw_utils_->IsLocaleRightToLeft() ? -kBackgroundX : kBackgroundX) -
      kHalfBackgroundWidth;

  // Pick begin index such that the selected index is centered on the screen.
  // If there are not enough items for a full page then start at 0.
  int begin_index = 0;
  int page_difference = storage_devices_.size() - max_dropdown_items_;
  if (page_difference >= 0) {
    begin_index =
        std::clamp(current_index - max_dropdown_items_ / 2, 0, page_difference);
  }

  const auto items_to_display =
      std::min(begin_index + max_dropdown_items_,
               static_cast<int>(storage_devices_.size()));
  for (int i = begin_index; i < items_to_display;
       i++, offset_y += kItemHeight) {
    if (current_index != i) {
      draw_utils_->ShowBox(kBackgroundX, offset_y, kBackgroundWidth,
                           kItemHeight, kMenuDropdownFrameNavy);
      draw_utils_->ShowBox(kBackgroundX, offset_y, kBackgroundWidth - 2,
                           kItemHeight - 2, kMenuDropdownBackgroundBlack);
      draw_utils_->ShowText(storage_devices_[i].first, kTextOffsetX, offset_y,
                            "dropdown_grey");
    } else {
      draw_utils_->ShowBox(kBackgroundX, offset_y, kBackgroundWidth,
                           kItemHeight, kMenuBlue);
      draw_utils_->ShowText(storage_devices_[i].first, kTextOffsetX, offset_y,
                            "black");
    }
  }
}

void ScreenDebugOptions::ShowButtons() {
  int default_width = draw_utils_->GetDefaultButtonWidth();
  constexpr int kYOffset = -100;
  const int kYStep = kButtonHeight + kButtonMargin;

  switch (state_) {
    case DropDownState::kDropdownClosed: {
      const auto button_index =
          static_cast<ButtonIndex>(std::min(kNumButtons - 1, index_));
      draw_utils_->ShowLanguageMenu(button_index ==
                                    ButtonIndex::kLanguageDropdown);
      ShowCollapsedDeviceOptions(button_index == ButtonIndex::kDeviceDropDown);

      for (const auto& [index, button_token] : kButtonLabels) {
        draw_utils_->ShowButton(
            button_token, kYOffset + ((static_cast<int>(index) - 1) * kYStep),
            button_index == index, default_width, false);
      }

      draw_utils_->ShowPowerButton(button_index == ButtonIndex::kPower);
      break;
    }
    case DropDownState::kDropdownOpen: {
      const auto device_index =
          std::min(static_cast<int>(storage_devices_.size()), index_);
      draw_utils_->ShowLanguageMenu(false);
      ShowCollapsedDeviceOptions(false);
      ShowOpenDeviceOptions(device_index);
      const auto dropdown_size = std::min(
          max_dropdown_items_, static_cast<int>(storage_devices_.size()));
      // Place the back button same place as before, or lower (if the list is
      // very long).
      const auto back_button_y_offset =
          kYOffset +
          std::max(((static_cast<int>(ButtonIndex::kBack) - 1) * kYStep),
                   ((dropdown_size + 1) * kYStep));
      draw_utils_->ShowButton(kBackImage, back_button_y_offset,
                              (device_index == storage_devices_.size()),
                              default_width, false);
      break;
    }
  }
}

void ScreenDebugOptions::HandleButtonSelection() {
  const auto button_index =
      static_cast<ButtonIndex>(std::min(kNumButtons - 1, index_));
  switch (button_index) {
    case ButtonIndex::kLanguageDropdown:
      screen_controller_->SwitchLocale(this);
      break;
    case ButtonIndex::kDeviceDropDown:
      state_ = DropDownState::kDropdownOpen;
      index_ = 0;
      UpdateStorageDevices();
      Show();
      break;
    case ButtonIndex::kEraseLogs:
      if (!ClearLogStoreKey(process_manager_)) {
        LOG(WARNING) << "Failed to clear log store key from VPD.";
      }
      if (log_store_manager_) {
        log_store_manager_->ClearLogs();
      } else {
        LOG(ERROR) << "Log store invalid, cannot clear logs.";
      }
      break;
    case ButtonIndex::kMessagLog:
      screen_controller_->OnForward(this);
      break;
    case ButtonIndex::kBack:
      screen_controller_->OnBackward(this);
      break;
    case ButtonIndex::kPower:
      TriggerShutdown();
      break;
    default:
      LOG(FATAL) << "Index " << index_ << " is not valid.";
  }
}

void ScreenDebugOptions::HandleDeviceSelection() {
  const auto device_index =
      std::min(static_cast<int>(storage_devices_.size()), index_);

  Reset();
  Show();
  if (device_index == storage_devices_.size()) {
    return;
  }
  const auto& [label, path] = storage_devices_.at(device_index);
  bool result = false;
  if (log_store_manager_) {
    if (label == kDiskStorageDevice) {
      result = log_store_manager_->SaveLogs(
          LogStoreManagerInterface::LogDirection::Disk);
    } else if (path) {
      result = SaveLogsToDevice(path.value());
    } else {
      LOG(ERROR) << "No path specified for removable device with label="
                 << label;
    }
  } else {
    LOG(ERROR) << "Log store manager not available.";
  }

  draw_utils_->ShowText(
      (result ? "Logs successfully saved to " : "Failed to save logs to ") +
          label,
      -draw_utils_->GetFreconCanvasSize() / 2 + 360, -100, "grey");
}

void ScreenDebugOptions::OnKeyPress(int key_changed) {
  bool enter = false;
  UpdateButtonsIndex(key_changed, &enter);
  if (enter) {
    switch (state_) {
      case DropDownState::kDropdownClosed:
        HandleButtonSelection();
        break;
      case DropDownState::kDropdownOpen:
        HandleDeviceSelection();
        break;
    }
  } else if (state_ == DropDownState::kDropdownOpen && key_changed == KEY_ESC) {
    Reset();
    Show();
  } else {
    ShowButtons();
  }
}

bool ScreenDebugOptions::MountRemovableDevice(
    base::ScopedTempDir& temp_mount_folder,
    const base::FilePath& device_path) const {
  if (!base::PathExists(device_path)) {
    LOG(ERROR) << "Device path does not exist=" << device_path.value();
    return false;
  }

  if (!temp_mount_folder.CreateUniqueTempDir() ||
      !temp_mount_folder.IsValid()) {
    LOG(ERROR) << "Failed to create temp dir.";
    return false;
  }
  const auto filesystem = blk_id_wrapper_->CheckAndGetTagValue(
      kFilesystemTypeTag, device_path.value());
  if (!filesystem) {
    LOG(ERROR) << "Couldn't determine filesystem for device at="
               << device_path.value();
    return false;
  }
  if (platform_->Mount(device_path.value(), temp_mount_folder.GetPath().value(),
                       filesystem.value(), MS_NOEXEC | MS_NOSUID) != 0) {
    PLOG(ERROR) << "Failed to mount device=" << device_path.value()
                << " at temp path=" << temp_mount_folder.GetPath()
                << " filesystem=" << filesystem.value();
    return false;
  }
  return true;
}

bool ScreenDebugOptions::SaveLogsToDevice(base::FilePath device_path) const {
  base::ScopedTempDir temp_mount_folder;
  if (!MountRemovableDevice(temp_mount_folder, device_path)) {
    return false;
  }

  const auto save_logs_result = log_store_manager_->SaveLogs(
      LogStoreManagerInterface::LogDirection::RemovableDevice,
      temp_mount_folder.GetPath().Append(kArchiveFileName));

  if (!platform_->Unmount(temp_mount_folder.GetPath(), true, nullptr)) {
    PLOG(ERROR) << "Failed to umount=" << temp_mount_folder.GetPath();
  }
  return save_logs_result;
}

void ScreenDebugOptions::Reset() {
  state_ = DropDownState::kDropdownClosed;
  index_ = static_cast<int>(ButtonIndex::kDeviceDropDown);
  button_count_ = kNumButtons;
}

ScreenType ScreenDebugOptions::GetType() {
  return ScreenType::kDebugOptionsScreen;
}

std::string ScreenDebugOptions::GetName() {
  return "ScreenDebugOptions";
}

bool ScreenDebugOptions::MoveForward(brillo::ErrorPtr* error) {
  index_ = static_cast<int>(ButtonIndex::kMessagLog);
  OnKeyPress(KEY_ENTER);
  return true;
}

bool ScreenDebugOptions::MoveBackward(brillo::ErrorPtr* error) {
  index_ = static_cast<int>(ButtonIndex::kBack);
  OnKeyPress(KEY_ENTER);
  return true;
}

}  // namespace minios
