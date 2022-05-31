// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/firewall_manager.h"

#include <cstdint>
#include <memory>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/permission_broker/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "permission_broker/dbus-proxy-mocks.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace lorgnette {

namespace {

// Test interface for FirewallManager to request port access on.
constexpr char kTestInterface[] = "Test Interface";
// Well-known port for Canon scanners.
constexpr uint16_t kCanonBjnpPort = 8612;

}  // namespace

class FirewallManagerTest : public testing::Test {
 protected:
  FirewallManagerTest() = default;

  void SetUp() override {
    raw_permission_broker_proxy_mock_ = static_cast<
        testing::StrictMock<org::chromium::PermissionBrokerProxyMock>*>(
        permission_broker_proxy_mock_.get());
  }

  void InitFirewallManager() {
    scoped_refptr<dbus::MockObjectProxy> mock_proxy_ =
        base::MakeRefCounted<dbus::MockObjectProxy>(
            /*bus=*/nullptr, permission_broker::kPermissionBrokerServiceName,
            dbus::ObjectPath(permission_broker::kPermissionBrokerServicePath));
    EXPECT_CALL(*raw_permission_broker_proxy_mock_, GetObjectProxy())
        .Times(2)
        .WillRepeatedly(Return(mock_proxy_.get()));
    EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_));
    EXPECT_CALL(*mock_proxy_, SetNameOwnerChangedCallback(_));
    firewall_manager_.Init(std::move(permission_broker_proxy_mock_));
  }

  FirewallManager* firewall_manager() { return &firewall_manager_; }

  org::chromium::PermissionBrokerProxyMock* permission_broker_proxy_mock()
      const {
    return raw_permission_broker_proxy_mock_;
  }

 private:
  // Ownership of `permission_broker_proxy_mock_` is transferred to
  // `firewall_manager_` if `InitFirewallManager()` is called.
  std::unique_ptr<org::chromium::PermissionBrokerProxyInterface>
      permission_broker_proxy_mock_ = std::make_unique<
          testing::StrictMock<org::chromium::PermissionBrokerProxyMock>>();
  // Allows tests to access the PermissionBrokerProxyMock if ownership of
  // `permission_broker_proxy_mock_` has been transferred.
  testing::StrictMock<org::chromium::PermissionBrokerProxyMock>*
      raw_permission_broker_proxy_mock_ = nullptr;
  FirewallManager firewall_manager_{kTestInterface};
};

// Test that FirewallManager can request access for all well-known PIXMA scanner
// ports.
TEST_F(FirewallManagerTest, RequestPixmaPortAccess) {
  InitFirewallManager();
  EXPECT_CALL(*permission_broker_proxy_mock(),
              RequestUdpPortAccess(kCanonBjnpPort, kTestInterface, _, _, _,
                                   dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(true)));
  PortToken pixma_token = firewall_manager()->RequestPixmaPortAccess();
  // FirewallManager should request to release the associated port when
  // `pixma_token` is destroyed.
  EXPECT_CALL(*permission_broker_proxy_mock(),
              ReleaseUdpPort(kCanonBjnpPort, kTestInterface, _, _,
                             dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
}

}  // namespace lorgnette
