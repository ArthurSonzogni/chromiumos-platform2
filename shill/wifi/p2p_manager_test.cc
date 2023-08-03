// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_manager.h"

#include <string>

#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/mock_wifi_provider.h"

using testing::_;
using testing::NiceMock;
using testing::Test;

namespace shill {

class P2PManagerTest : public testing::Test {
 public:
  P2PManagerTest()
      : temp_dir_(MakeTempDir()),
        path_(temp_dir_.GetPath().value()),
        manager_(
            &control_interface_, &dispatcher_, &metrics_, path_, path_, path_),
        wifi_provider_(new NiceMock<MockWiFiProvider>(&manager_)),
        p2p_manager_(wifi_provider_->p2p_manager()) {
    // Replace the Manager's WiFi provider with a mock.
    manager_.wifi_provider_.reset(wifi_provider_);
    // Update the Manager's map from technology to provider.
    manager_.UpdateProviderMapping();
  }
  ~P2PManagerTest() override = default;

  void SetAllowed(P2PManager* p2p_manager, bool allowed) {
    Error error;
    PropertyStore store;
    p2p_manager->InitPropertyStore(&store);
    store.SetBoolProperty(kP2PAllowedProperty, allowed, &error);
    EXPECT_TRUE(error.IsSuccess());
  }

  base::ScopedTempDir MakeTempDir() {
    base::ScopedTempDir temp_dir;
    EXPECT_TRUE(temp_dir.CreateUniqueTempDir());
    return temp_dir;
  }

 protected:
  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  base::ScopedTempDir temp_dir_;
  std::string path_;
  MockManager manager_;
  MockWiFiProvider* wifi_provider_;
  P2PManager* p2p_manager_;
};

TEST_F(P2PManagerTest, SetP2PAllowed) {
  SetAllowed(p2p_manager_, true);
  EXPECT_EQ(p2p_manager_->allowed_, true);
  SetAllowed(p2p_manager_, false);
  EXPECT_EQ(p2p_manager_->allowed_, false);
}

}  // namespace shill
