// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_TEST_MOCK_SHILL_H_
#define SECAGENTD_TEST_MOCK_SHILL_H_

#include <gmock/gmock.h>
#include <memory>
#include <vector>

#include "shill/dbus/client/fake_client.h"

namespace secagentd::testing {
class MockShill : public shill::FakeClient {
 public:
  explicit MockShill(scoped_refptr<dbus::Bus> bus) : FakeClient(bus) {}
  MOCK_METHOD(void,
              RegisterOnAvailableCallback,
              (base::OnceCallback<void(bool)> handler),
              (override));

  MOCK_METHOD(void,
              RegisterProcessChangedHandler,
              (const base::RepeatingCallback<void(bool)>& handler),
              (override));

  MOCK_METHOD(void,
              RegisterDefaultServiceChangedHandler,
              (const shill::Client::DefaultServiceChangedHandler& handler),
              (override));
  MOCK_METHOD(void,
              RegisterDefaultDeviceChangedHandler,
              (const shill::Client::DeviceChangedHandler& handler),
              (override));

  MOCK_METHOD(void,
              RegisterDeviceChangedHandler,
              (const shill::Client::DeviceChangedHandler& handler),
              (override));

  MOCK_METHOD(void,
              RegisterDeviceAddedHandler,
              (const shill::Client::DeviceChangedHandler& handler),
              (override));

  MOCK_METHOD(void,
              RegisterDeviceRemovedHandler,
              (const shill::Client::DeviceChangedHandler& handler),
              (override));

  MOCK_METHOD(std::unique_ptr<shill::Client::ManagerPropertyAccessor>,
              ManagerProperties,
              (const base::TimeDelta& timeout),
              (const override));

  MOCK_METHOD(std::unique_ptr<shill::Client::ServicePropertyAccessor>,
              DefaultServicePropertyAccessor,
              (const base::TimeDelta& timeout),
              (const override));

  MOCK_METHOD(std::unique_ptr<brillo::VariantDictionary>,
              GetDefaultServiceProperties,
              (const base::TimeDelta& timeout),
              (const override));

  MOCK_METHOD(std::unique_ptr<shill::Client::Device>,
              DefaultDevice,
              (bool exclude_vpn),
              (override));

  MOCK_METHOD(org::chromium::flimflam::ManagerProxyInterface*,
              GetManagerProxy,
              (),
              (const override));

  // Returns all available devices.
  MOCK_METHOD(std::vector<std::unique_ptr<shill::Client::Device>>,
              GetDevices,
              (),
              (const override));
};
}  // namespace secagentd::testing
#endif  // SECAGENTD_TEST_MOCK_SHILL_H_
