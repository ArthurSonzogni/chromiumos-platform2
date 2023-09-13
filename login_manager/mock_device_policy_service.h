// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_MOCK_DEVICE_POLICY_SERVICE_H_
#define LOGIN_MANAGER_MOCK_DEVICE_POLICY_SERVICE_H_

#include "login_manager/device_policy_service.h"
#include "login_manager/mock_policy_store.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <crypto/scoped_nss_types.h>
#include <libcrossystem/crossystem.h>

#include "bindings/chrome_device_policy.pb.h"

namespace login_manager {
class SystemUtils;

class MockDevicePolicyService : public DevicePolicyService {
 public:
  MockDevicePolicyService();
  explicit MockDevicePolicyService(PolicyKey* policy_key);
  ~MockDevicePolicyService() override;

  MOCK_METHOD(
      void,
      Store,
      (const PolicyNamespace&, const std::vector<uint8_t>&, int, Completion),
      (override));
  MOCK_METHOD(bool,
              Retrieve,
              (const PolicyNamespace&, std::vector<uint8_t>*),
              (override));
  MOCK_METHOD(bool,
              HandleOwnerLogin,
              (const std::string&, PK11SlotDescriptor*, brillo::ErrorPtr*),
              (override));
  MOCK_METHOD(bool, UserIsOwner, (const std::string&), (override));
  MOCK_METHOD(bool,
              ValidateAndStoreOwnerKey,
              (const std::string&,
               const std::vector<uint8_t>&,
               PK11SlotDescriptor*),
              (override));
  MOCK_METHOD(bool, KeyMissing, (), (override));
  MOCK_METHOD(bool, Mitigating, (), (override));
  MOCK_METHOD(bool, Initialize, (), (override));
  MOCK_METHOD(void, ClearBlockDevmode, (Completion), (override));
  MOCK_METHOD(
      bool,
      ValidateRemoteDeviceWipeCommand,
      (const std::vector<uint8_t>&,
       enterprise_management::PolicyFetchRequest::SignatureType signature_type),
      (override));

  void set_system_utils(SystemUtils* system) { system_ = system; }
  void set_crossystem(crossystem::Crossystem* crossystem) {
    crossystem_ = crossystem;
  }
  void set_vpd_process(VpdProcess* vpd_process) { vpd_process_ = vpd_process; }
  void set_install_attributes_reader(
      InstallAttributesReader* install_attributes_reader) {
    install_attributes_reader_ = install_attributes_reader;
  }

  void OnPolicySuccessfullyPersisted() {
    OnPolicyPersisted(Completion(), dbus_error::kNone);
  }
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_DEVICE_POLICY_SERVICE_H_
