// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_MOCK_UPDATE_ATTEMPTER_H_
#define UPDATE_ENGINE_CROS_MOCK_UPDATE_ATTEMPTER_H_

#include <string>
#include <vector>

#include "update_engine/cros/update_attempter.h"

#include <gmock/gmock.h>

namespace chromeos_update_engine {

class MockUpdateAttempter : public UpdateAttempter {
 public:
  using UpdateAttempter::UpdateAttempter;

  MOCK_METHOD(bool, IsUpdating, (), (override));

  MOCK_METHOD(void,
              Update,
              (const chromeos_update_manager::UpdateCheckParams& params),
              (override));

  MOCK_METHOD1(GetStatus, bool(update_engine::UpdateEngineStatus* out_status));

  MOCK_METHOD1(GetBootTimeAtUpdate, bool(base::Time* out_boot_time));

  MOCK_METHOD0(ResetStatus, bool(void));

  MOCK_CONST_METHOD0(GetCurrentUpdateFlags, update_engine::UpdateFlags(void));

  MOCK_METHOD(bool,
              CheckForUpdate,
              (const update_engine::UpdateParams&),
              (override));

  MOCK_METHOD(bool,
              CheckForInstall,
              (const std::vector<std::string>& dlc_ids,
               const std::string& omaha_url,
               bool scaled,
               bool force_ota),
              (override));

  MOCK_METHOD2(SetDlcActiveValue, bool(bool, const std::string&));

  MOCK_CONST_METHOD0(GetExcluder, ExcluderInterface*(void));

  MOCK_METHOD0(RefreshDevicePolicy, void(void));

  MOCK_CONST_METHOD0(consecutive_failed_update_checks, unsigned int(void));

  MOCK_CONST_METHOD0(server_dictated_poll_interval, unsigned int(void));

  MOCK_METHOD0(IsRepeatedUpdatesEnabled, bool(void));
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_MOCK_UPDATE_ATTEMPTER_H_
