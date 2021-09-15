// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_EUICC_H_
#define HERMES_EUICC_H_

#include <memory>
#include <string>
#include <vector>

#include <google-lpa/lpa/core/lpa.h>
#include <google-lpa/lpa/data/proto/profile_info.pb.h>

#include "hermes/adaptor_interfaces.h"
#include "hermes/context.h"
#include "hermes/dbus_result.h"
#include "hermes/euicc_slot_info.h"
#include "hermes/profile.h"

namespace hermes {

class Euicc {
 public:
  Euicc(uint8_t physical_slot, EuiccSlotInfo slot_info);
  Euicc(const Euicc&) = delete;
  Euicc& operator=(const Euicc&) = delete;

  void UpdateSlotInfo(EuiccSlotInfo slot_info);
  void UpdateLogicalSlot(base::Optional<uint8_t> logical_slot);

  // Install a profile. An empty activation code will cause the default profile
  // to be installed.
  void InstallProfileFromActivationCode(
      std::string activation_code,
      std::string confirmation_code,
      DbusResult<dbus::ObjectPath> dbus_result);
  void UninstallProfile(dbus::ObjectPath profile_path,
                        DbusResult<> dbus_result);
  // Request the eUICC to provide all installed profiles.
  void RequestInstalledProfiles(DbusResult<> dbus_result);

  void InstallPendingProfile(dbus::ObjectPath profile_path,
                             std::string confirmation_code,
                             DbusResult<dbus::ObjectPath> dbus_result);
  void RequestPendingProfiles(DbusResult<> dbus_result, std::string root_smds);
  void SetTestModeHelper(bool is_test_mode, DbusResult<> dbus_result);
  void UseTestCerts(bool use_test_certs);
  void ResetMemoryHelper(DbusResult<> dbus_result, int reset_options);

  uint8_t physical_slot() const { return physical_slot_; }
  dbus::ObjectPath object_path() const { return dbus_adaptor_->object_path(); }

 private:
  void OnProfileInstalled(const lpa::proto::ProfileInfo& profile_info,
                          int error,
                          DbusResult<dbus::ObjectPath> dbus_result);
  void OnProfileUninstalled(const dbus::ObjectPath& profile_path,
                            int error,
                            DbusResult<> dbus_result);

  void UpdateInstalledProfilesProperty();
  void SendNotifications(DbusResult<> dbus_result);

  // Update |installed_profiles_| with all profiles installed on the eUICC.
  void OnInstalledProfilesReceived(
      const std::vector<lpa::proto::ProfileInfo>& profile_infos,
      int error,
      DbusResult<> dbus_result);

  // Update |pending_profiles_| with all profiles installed on the SMDS.
  void UpdatePendingProfilesProperty();
  void OnPendingProfilesReceived(
      const std::vector<lpa::proto::ProfileInfo>& profile_infos,
      int error,
      DbusResult<> dbus_result);

  // Methods that call an eponymous LPA method.
  // These methods are used once a slot switch is performed and a channel has
  // been acquired.
  void GetInstalledProfiles(DbusResult<> dbus_result);
  void DownloadProfile(std::string activation_code,
                       std::string confirmation_code,
                       DbusResult<dbus::ObjectPath> dbus_result);
  void DeleteProfile(dbus::ObjectPath profile_path,
                     std::string iccid,
                     DbusResult<> dbus_result);
  void GetPendingProfilesFromSmds(std::string root_smds,
                                  DbusResult<> dbus_result);
  void SetTestMode(bool is_test_mode, DbusResult<> dbus_result);
  void ResetMemory(int reset_options, DbusResult<> dbus_result);

  const uint8_t physical_slot_;
  EuiccSlotInfo slot_info_;
  bool is_test_mode_;

  Context* context_;
  std::unique_ptr<EuiccAdaptorInterface> dbus_adaptor_;

  std::vector<std::unique_ptr<Profile>> installed_profiles_;
  std::vector<std::unique_ptr<Profile>> pending_profiles_;

  base::WeakPtrFactory<Euicc> weak_factory_;
};

}  // namespace hermes

#endif  // HERMES_EUICC_H_
