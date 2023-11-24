// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SUPPLICANT_MOCK_SUPPLICANT_GROUP_PROXY_H_
#define SHILL_SUPPLICANT_MOCK_SUPPLICANT_GROUP_PROXY_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "shill/refptr_types.h"
#include "shill/supplicant/supplicant_group_proxy_interface.h"
#include "supplicant/dbus-proxies.h"

namespace shill {

class MockSupplicantGroupProxy : public SupplicantGroupProxyInterface {
 public:
  MockSupplicantGroupProxy();
  MockSupplicantGroupProxy(const MockSupplicantGroupProxy&) = delete;
  MockSupplicantGroupProxy& operator=(const MockSupplicantGroupProxy&) = delete;

  ~MockSupplicantGroupProxy() override;

  MOCK_METHOD(bool,
              GetMembers,
              (std::vector<dbus::ObjectPath>*),
              (const override));
  MOCK_METHOD(bool, GetRole, (std::string*), (const override));
  MOCK_METHOD(bool, GetSSID, (std::vector<uint8_t>*), (const override));
  MOCK_METHOD(bool, GetBSSID, (std::vector<uint8_t>*), (const override));
  MOCK_METHOD(bool, GetFrequency, (uint16_t*), (const override));
  MOCK_METHOD(bool, GetPassphrase, (std::string*), (const override));
};

}  // namespace shill

#endif  // SHILL_SUPPLICANT_MOCK_SUPPLICANT_GROUP_PROXY_H_
