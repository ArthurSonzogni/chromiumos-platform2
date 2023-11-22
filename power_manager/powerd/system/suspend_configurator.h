// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_SUSPEND_CONFIGURATOR_H_
#define POWER_MANAGER_POWERD_SYSTEM_SUSPEND_CONFIGURATOR_H_

#include <memory>
#include <optional>
#include <string>

#include "power_manager/powerd/system/dbus_wrapper.h"

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <featured/feature_library.h>

namespace power_manager {

class PrefsInterface;

namespace system {

class DBusWrapperInterface;

// Interface to configure suspend-related kernel parameters on startup or
// before suspend as needed.
class SuspendConfiguratorInterface {
 public:
  SuspendConfiguratorInterface() = default;
  SuspendConfiguratorInterface(const SuspendConfiguratorInterface&) = delete;
  SuspendConfiguratorInterface& operator=(const SuspendConfiguratorInterface&) =
      delete;

  virtual ~SuspendConfiguratorInterface() = default;

  // Do pre-suspend configuration and logging just before asking kernel to
  // suspend. Returns the wakealarm time that gets programmed into the RTC.
  virtual uint64_t PrepareForSuspend(
      const base::TimeDelta& suspend_duration) = 0;
  // Do post-suspend work just after resuming from suspend. Returns false if the
  // last suspend was a failure. Returns true otherwise.
  virtual bool UndoPrepareForSuspend() = 0;
};

class SuspendConfigurator : public SuspendConfiguratorInterface {
 public:
  // Path to write to enable/disable console during suspend.
  static const base::FilePath kConsoleSuspendPath;

  // Filename appended to the runtime state directory (see |run_dir| in Init())
  // to create the |initial_suspend_mode_path_| for storing the initial
  // system suspend mode.
  static constexpr std::string_view kInitialSuspendModeFileName =
      "initial_suspend_mode";

  SuspendConfigurator() = default;
  SuspendConfigurator(const SuspendConfigurator&) = delete;
  SuspendConfigurator& operator=(const SuspendConfigurator&) = delete;

  ~SuspendConfigurator() override = default;

  void Init(feature::PlatformFeaturesInterface* platform_features,
            PrefsInterface* prefs,
            const base::FilePath& run_dir);

  // SuspendConfiguratorInterface implementation.
  uint64_t PrepareForSuspend(const base::TimeDelta& suspend_duration) override;
  bool UndoPrepareForSuspend() override;

  // Sets a prefix path which is used as file system root when testing.
  // Setting to an empty path removes the prefix.
  void set_prefix_path_for_testing(const base::FilePath& file) {
    prefix_path_for_testing_ = file;
  }

  std::optional<std::string> get_initial_sleep_mode_for_testing() {
    return kernel_default_sleep_mode_;
  }

 private:
  // Configures whether console should be enabled/disabled during suspend.
  void ConfigureConsoleForSuspend();

  // Returns true if the serial console is enabled.
  bool IsSerialConsoleEnabled();

  // Get cpu information of the system
  // Reads from /proc/cpuinfo by default
  bool ReadCpuInfo(std::string& cpuInfo);

  // Returns true if running on an Intel CPU.
  bool HasIntelCpu();

  // Reads preferences and sets |suspend_mode_|.
  void ReadSuspendMode();

  // Returns new FilePath after prepending |prefix_path_for_testing_| to
  // given file path.
  base::FilePath GetPrefixedFilePath(const base::FilePath& file_path) const;

  // Used for communicating with featured.
  feature::PlatformFeaturesInterface* platform_features_ = nullptr;  // unowned
  PrefsInterface* prefs_ = nullptr;                                  // unowned

  // Prefixing root paths for testing with a temp directory. Empty (no
  // prefix) by default.
  base::FilePath prefix_path_for_testing_;

  // Mode for suspend. One of Suspend-to-idle, Power-on-suspend, or
  // Suspend-to-RAM.
  std::string suspend_mode_;

  // System initial default sleep mode.
  std::optional<std::string> kernel_default_sleep_mode_;

  // Path to write the initial default suspend mode.
  base::FilePath initial_suspend_mode_path_;

  // Reads the currently selected value from /sys/power/mem_sleep.
  // Returns nullopt on failure reading the value.
  std::optional<std::string> ReadPowerMemSleepValue();

  // Creates the file to store the initial system suspend mode.
  // Returns false if the file is unable to be written.
  bool SaveInitialSuspendMode(const std::optional<std::string>& state);

  // Reads the stored kernel's initial default suspend mode into
  // |kernel_default_sleep_mode_|.
  // |kernel_default_sleep_mode_| will be empty if the stored
  // mode can't be read.
  void ReadInitialSuspendMode();

  bool IsValidSuspendMode(std::string_view mem_sleep);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_SUSPEND_CONFIGURATOR_H_
