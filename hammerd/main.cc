// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// hammerd - A daemon to update the firmware of Hammer

#include <stdlib.h>

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/strings/stringprintf.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "hammerd/hammer_updater.h"
#include "hammerd/process_lock.h"
#include "hammerd/usb_utils.h"

namespace {
// The lock file used to prevent multiple hammerd be invoked at the same time.
constexpr char kLockFile[] = "/run/lock/hammerd.lock";

enum class ExitStatus {
  kSuccess                = EXIT_SUCCESS,  // 0
  kUnknownError           = 1,
  kNeedUsbInfo            = 10,
  kEcImageNotFound        = 11,
  kTouchpadImageNotFound  = 12,
  kUnknownUpdateCondition = 13,
  kConnectionError        = 14,
  kInvalidFirmware        = 15,
};
}  // namespace

int main(int argc, const char* argv[]) {
  // hammerd should be triggered by upstart job.
  // The default value of arguments are stored in `/etc/init/hammerd.conf`, and
  // each board should override values in `/etc/init/hammerd.override`.
  DEFINE_string(ec_image_path, "", "Path to the EC firmware image file");
  DEFINE_string(touchpad_image_path, "", "Path to the touchpad image file");
  // TODO(b/65534217): Define a flag about touchpad version that is expected
  //                   to be computed by init script.
  DEFINE_int32(vendor_id, -1, "USB vendor ID of the device");
  DEFINE_int32(product_id, -1, "USB product ID of the device");
  DEFINE_int32(usb_bus, -1, "USB bus to search");
  DEFINE_int32(usb_port, -1, "USB port to search");
  DEFINE_int32(autosuspend_delay_ms, -1, "USB autosuspend delay time (ms)");
  DEFINE_bool(at_boot, false,
              "Invoke process at boot time. "
              "Exit if RW is up-to-date (no pairing)");
  DEFINE_string(update_if, "never",
                "Update condition, one of: never|mismatch|always.\n"
                "    never:\n"
                "      Never update, just check if update is needed.\n"
                "    mismatch:\n"
                "      Update as long as the firmware is mismatched.\n"
                "    always:\n"
                "      Update anyways, regardless of version");
  brillo::FlagHelper::Init(argc, argv, "Hammer EC firmware updater daemon");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader |
                  brillo::kLogToStderrIfTty);

  base::FilePath file_path(FILE_PATH_LITERAL(kLockFile));
  hammerd::ProcessLock lock(file_path);
  if (!lock.Acquire()) {
    LOG(INFO) << "Other hammerd process is running, exit.";
    return static_cast<int>(ExitStatus::kSuccess);
  }

  if (FLAGS_vendor_id < 0 || FLAGS_product_id < 0 ||
      FLAGS_usb_bus < 0 || FLAGS_usb_port < 0) {
    LOG(ERROR) << "Must specify USB vendor/product ID and bus/port number.";
    return static_cast<int>(ExitStatus::kNeedUsbInfo);
  }

  std::string ec_image;
  if (!base::ReadFileToString(base::FilePath(FLAGS_ec_image_path), &ec_image)) {
    LOG(ERROR) << "EC image file is not found: " << FLAGS_ec_image_path;
    return static_cast<int>(ExitStatus::kEcImageNotFound);
  }

  std::string touchpad_image;
  std::string touchpad_product_id;
  std::string touchpad_fw_ver;
  if (!FLAGS_touchpad_image_path.size()) {
    LOG(INFO) << "Touchpad image is not assigned. " <<
                 "Proceeding without updating touchpad.";

  } else if (!base::ReadFileToString(base::FilePath(FLAGS_touchpad_image_path),
                                     &touchpad_image)) {
    LOG(ERROR) << "Touchpad image is not found with path ["
               << FLAGS_touchpad_image_path << "]. Abort.";
    return static_cast<int>(ExitStatus::kTouchpadImageNotFound);
  } else if (!hammerd::HammerUpdater::ParseTouchpadInfoFromFilename(
        FLAGS_touchpad_image_path, &touchpad_product_id, &touchpad_fw_ver)) {
    LOG(ERROR) << "Not able to get version info from filename. "
               << "Check if [" << FLAGS_touchpad_image_path << "] follows "
               << "<product_id>_<fw_version>.bin format (applied to symbolic "
               << "link as well).";
    return static_cast<int>(ExitStatus::kInvalidFirmware);
  }
  hammerd::HammerUpdater::UpdateCondition update_condition =
      hammerd::HammerUpdater::ToUpdateCondition(FLAGS_update_if);
  if (update_condition == hammerd::HammerUpdater::UpdateCondition::kUnknown) {
    LOG(ERROR) << "Unknown update condition: " << FLAGS_update_if;
    return static_cast<int>(ExitStatus::kUnknownUpdateCondition);
  }

  // The message loop registers a task runner with the current thread, which
  // is used by DBusWrapper to send signals.
  base::MessageLoop message_loop;
  hammerd::HammerUpdater updater(
      ec_image, touchpad_image, touchpad_product_id, touchpad_fw_ver,
      FLAGS_vendor_id, FLAGS_product_id,
      FLAGS_usb_bus, FLAGS_usb_port, FLAGS_at_boot, update_condition);
  hammerd::HammerUpdater::RunStatus ret = updater.Run();
  if (ret == hammerd::HammerUpdater::RunStatus::kNoUpdate &&
      FLAGS_autosuspend_delay_ms >= 0) {
    LOG(INFO) << "Enable USB autosuspend with delay "
              << FLAGS_autosuspend_delay_ms << " ms.";
    base::FilePath base_path =
        hammerd::GetUsbSysfsPath(FLAGS_usb_bus, FLAGS_usb_port);
    constexpr char kPowerLevelPath[] = "power/level";
    constexpr char kAutosuspendDelayMsPath[] = "power/autosuspend_delay_ms";
    constexpr char kPowerLevel[] = "auto";
    std::string delay_ms = base::StringPrintf("%d", FLAGS_autosuspend_delay_ms);

    base::WriteFile(base_path.Append(base::FilePath(kPowerLevelPath)),
                    kPowerLevel, sizeof(kPowerLevel) - 1);
    base::WriteFile(base_path.Append(base::FilePath(kAutosuspendDelayMsPath)),
                    delay_ms.data(), delay_ms.size());
  }
  switch (ret) {
    case hammerd::HammerUpdater::RunStatus::kNoUpdate:
      return static_cast<int>(ExitStatus::kSuccess);
    case hammerd::HammerUpdater::RunStatus::kLostConnection:
    case hammerd::HammerUpdater::RunStatus::kNeedJump:
    case hammerd::HammerUpdater::RunStatus::kNeedReset:
      return static_cast<int>(ExitStatus::kConnectionError);
    case hammerd::HammerUpdater::RunStatus::kInvalidFirmware:
      return static_cast<int>(ExitStatus::kInvalidFirmware);
    default:
      return static_cast<int>(ExitStatus::kUnknownError);
  }
}
