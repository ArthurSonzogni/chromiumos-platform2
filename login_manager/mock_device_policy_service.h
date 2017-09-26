// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
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

#include "bindings/chrome_device_policy.pb.h"

namespace login_manager {
// Forward declaration.
typedef struct PK11SlotInfoStr PK11SlotInfo;

class MockDevicePolicyService : public DevicePolicyService {
 public:
  MockDevicePolicyService();
  MockDevicePolicyService(std::unique_ptr<MockPolicyStore> policy_store,
                          PolicyKey* policy_key);
  ~MockDevicePolicyService() override;

  MOCK_METHOD4(Store,
               bool(const std::vector<uint8_t>&,
                    int,
                    SignatureCheck,
                    const Completion&));
  MOCK_METHOD1(Retrieve, bool(std::vector<uint8_t>*));
  MOCK_METHOD4(
      CheckAndHandleOwnerLogin,
      bool(const std::string&, PK11SlotInfo*, bool*, brillo::ErrorPtr*));
  MOCK_METHOD3(ValidateAndStoreOwnerKey,
               bool(const std::string&,
                    const std::vector<uint8_t>&,
                    PK11SlotInfo*));
  MOCK_METHOD0(KeyMissing, bool(void));
  MOCK_METHOD0(Mitigating, bool(void));
  MOCK_METHOD0(Initialize, bool(void));
  MOCK_METHOD2(ReportPolicyFileMetrics, void(bool, bool));
  MOCK_METHOD0(InstallAttributesEnterpriseMode, bool(void));

  void set_crossystem(Crossystem* crossystem) { crossystem_ = crossystem; }
  void set_vpd_process(VpdProcess* vpd_process) { vpd_process_ = vpd_process; }
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_DEVICE_POLICY_SERVICE_H_
