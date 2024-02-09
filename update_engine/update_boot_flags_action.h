// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "update_engine/common/action.h"
#include "update_engine/common/boot_control_interface.h"
#include "update_engine/common/hardware_interface.h"

#include <gtest/gtest_prod.h>

namespace chromeos_update_engine {

class UpdateBootFlagsAction : public AbstractAction {
 public:
  explicit UpdateBootFlagsAction(BootControlInterface* boot_control,
                                 HardwareInterface* hardware)
      : boot_control_(boot_control), hardware_(hardware) {}
  UpdateBootFlagsAction(const UpdateBootFlagsAction&) = delete;
  UpdateBootFlagsAction& operator=(const UpdateBootFlagsAction&) = delete;

  void PerformAction() override;

  void TerminateProcessing() override;

  static std::string StaticType() { return "UpdateBootFlagsAction"; }
  std::string Type() const override { return StaticType(); }

  void CompleteUpdateBootFlags(bool successful);

 private:
  FRIEND_TEST(UpdateBootFlagsActionTest, SimpleTest);
  FRIEND_TEST(UpdateBootFlagsActionTest, RunningMiniOSTest);
  FRIEND_TEST(UpdateBootFlagsActionTest, DoubleActionTest);

  // Originally, both of these flags are false. Once UpdateBootFlags is called,
  // |is_running_| is set to true. As soon as UpdateBootFlags completes its
  // asynchronous run, |is_running_| is reset to false and |updated_boot_flags_|
  // is set to true. From that point on there will be no more changes to these
  // flags.
  //
  // True if have updated the boot flags.
  static bool updated_boot_flags_;
  // True if we are still updating the boot flags.
  static bool is_running_;

  // Used for setting the boot flag.
  BootControlInterface* boot_control_;
  // Used for determining whether the device is booted from MiniOS.
  HardwareInterface* hardware_;
};

}  // namespace chromeos_update_engine
