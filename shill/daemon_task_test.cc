// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/daemon_task.h"

#include <linux/rtnetlink.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/memory/ref_counted.h>
#include <base/run_loop.h>
#include <chromeos/net-base/mock_netlink_manager.h>
#include <chromeos/net-base/mock_process_manager.h>
#include <chromeos/net-base/mock_rtnl_handler.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mojom/mock_shill_mojo_service_manager.h"
#include "shill/shill_test_config.h"
#include "shill/supplicant/supplicant_manager.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/nl80211_message.h"

using ::testing::_;
using ::testing::Expectation;
using ::testing::InSequence;
using ::testing::Mock;
using ::testing::Return;
using ::testing::Test;

namespace shill {

// Netlink multicast group for neighbor discovery user option message.
constexpr uint32_t RTMGRP_ND_USEROPT = 1 << (RTNLGRP_ND_USEROPT - 1);

class DaemonTaskForTest : public DaemonTask {
 public:
  explicit DaemonTaskForTest(Config* config) : DaemonTask(config) {}
  ~DaemonTaskForTest() override = default;

  bool quit_result() const { return quit_result_; }

  void RunMessageLoop() { dispatcher_->DispatchForever(); }

  bool Quit(base::OnceClosure completion_callback) override {
    quit_result_ = DaemonTask::Quit(std::move(completion_callback));
    dispatcher_->PostTask(
        FROM_HERE,
        base::BindOnce(&EventDispatcher::QuitDispatchForever,
                       // dispatcher_ will not be deleted before RunLoop quits.
                       base::Unretained(dispatcher_.get())));
    return quit_result_;
  }

 private:
  bool quit_result_;
};

class DaemonTaskTest : public Test {
 public:
  DaemonTaskTest()
      : daemon_(&config_),
        dispatcher_(new EventDispatcherForTest()),
        control_(new MockControl()),
        metrics_(new MockMetrics()),
        manager_(new MockManager(control_, dispatcher_, metrics_)) {}
  ~DaemonTaskTest() override = default;
  void SetUp() override {
    // Tests initialization done by the daemon's constructor
    daemon_.rtnl_handler_ = &rtnl_handler_;
    daemon_.process_manager_ = &process_manager_;
    daemon_.metrics_.reset(metrics_);        // Passes ownership
    daemon_.manager_.reset(manager_);        // Passes ownership
    daemon_.control_.reset(control_);        // Passes ownership
    daemon_.dispatcher_.reset(dispatcher_);  // Passes ownership
    daemon_.netlink_manager_ = &netlink_manager_;

    auto mojo_service_manager_factory =
        std::make_unique<MockShillMojoServiceManagerFactory>();
    mojo_service_manager_factory_ = mojo_service_manager_factory.get();
    daemon_.mojo_service_manager_factory_ =
        std::move(mojo_service_manager_factory);
  }
  void StartDaemon() { daemon_.Start(); }

  void StopDaemon() { daemon_.Stop(); }

  void RunDaemon() { daemon_.RunMessageLoop(); }

  MOCK_METHOD(void, OnMojoServiceDestroyed, ());
  MOCK_METHOD(void, TerminationAction, ());
  MOCK_METHOD(void, BreakTerminationLoop, ());

 protected:
  TestConfig config_;
  DaemonTaskForTest daemon_;
  net_base::MockRTNLHandler rtnl_handler_;
  net_base::MockProcessManager process_manager_;
  EventDispatcherForTest* dispatcher_;
  MockControl* control_;
  MockMetrics* metrics_;
  MockManager* manager_;
  MockShillMojoServiceManagerFactory* mojo_service_manager_factory_;
  net_base::MockNetlinkManager netlink_manager_;
};

TEST_F(DaemonTaskTest, StartStop) {
  const uint16_t kNl80211MessageType = 42;  // Arbitrary.
  // To ensure we do not have any stale routes, we flush a device's routes
  // when it is started.  This requires that the routing table is fully
  // populated before we create and start devices.  So test to make sure that
  // the RoutingTable starts before the Manager (which in turn starts
  // DeviceInfo who is responsible for creating and starting devices).
  // The result is that we request the dump of the routing table and when that
  // completes, we request the dump of the links.  For each link found, we
  // create and start the device.
  {
    InSequence s;
    EXPECT_CALL(rtnl_handler_,
                Start(RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE |
                      RTMGRP_IPV6_IFADDR | RTMGRP_IPV6_ROUTE |
                      RTMGRP_ND_USEROPT | RTMGRP_IPV6_PREFIX));
    EXPECT_CALL(process_manager_, Init());
    EXPECT_CALL(netlink_manager_, Init());
    EXPECT_CALL(netlink_manager_,
                GetFamily(Nl80211Message::kMessageTypeString, _))
        .WillOnce(Return(kNl80211MessageType));
    EXPECT_CALL(netlink_manager_, Start());
    EXPECT_CALL(*manager_, Start());
    EXPECT_CALL(*mojo_service_manager_factory_, Create(manager_))
        .WillOnce([this]() {
          return std::make_unique<MockShillMojoServiceManager>(base::BindOnce(
              &DaemonTaskTest::OnMojoServiceDestroyed, base::Unretained(this)));
        });
  }
  StartDaemon();
  Mock::VerifyAndClearExpectations(manager_);
  Mock::VerifyAndClearExpectations(mojo_service_manager_factory_);

  {
    InSequence s;
    EXPECT_CALL(*this, OnMojoServiceDestroyed());
    EXPECT_CALL(*manager_, Stop());
    EXPECT_CALL(process_manager_, Stop());
  }
  StopDaemon();
  Mock::VerifyAndClearExpectations(this);
}

TEST_F(DaemonTaskTest, SupplicantAppearsAfterStop) {
  // This test verifies that the daemon won't crash upon receiving Dbus message
  // via ControlInterface, which outlives the Manager. The SupplicantManager is
  // owned by the Manager, which is freed after Stop().
  StartDaemon();
  manager_->supplicant_manager()->Start();

  StopDaemon();

  control_->supplicant_appear().Run();
  dispatcher_->DispatchPendingEvents();
}

ACTION_P2(CompleteAction, manager, name) {
  manager->TerminationActionComplete(name);
}

TEST_F(DaemonTaskTest, QuitWithTerminationAction) {
  // This expectation verifies that the termination actions are invoked.
  EXPECT_CALL(*this, TerminationAction())
      .WillOnce(CompleteAction(manager_, "daemon test"));
  EXPECT_CALL(*this, BreakTerminationLoop()).Times(1);

  manager_->AddTerminationAction(
      "daemon test", base::BindOnce(&DaemonTaskTest::TerminationAction,
                                    base::Unretained(this)));

  // Run Daemon::Quit() after the daemon starts running.
  dispatcher_->PostTask(
      FROM_HERE,
      base::BindOnce(IgnoreResult(&DaemonTask::Quit),
                     base::Unretained(&daemon_),
                     base::BindOnce(&DaemonTaskTest::BreakTerminationLoop,
                                    base::Unretained(this))));

  RunDaemon();
  EXPECT_FALSE(daemon_.quit_result());
}

TEST_F(DaemonTaskTest, QuitWithoutTerminationActions) {
  EXPECT_CALL(*this, BreakTerminationLoop()).Times(0);
  EXPECT_TRUE(daemon_.Quit(base::BindOnce(&DaemonTaskTest::BreakTerminationLoop,
                                          base::Unretained(this))));
}

}  // namespace shill
