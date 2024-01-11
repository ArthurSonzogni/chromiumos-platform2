// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/connmark_updater.h"

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/mock_conntrack_monitor.h"
#include "patchpanel/mock_process_runner.h"

namespace patchpanel {
namespace {

using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::Mock;
using ::testing::Return;

constexpr char kIPAddress1[] = "8.8.8.8";
constexpr char kIPAddress2[] = "8.8.8.4";
constexpr uint16_t kPort1 = 10000;
constexpr uint16_t kPort2 = 20000;
constexpr ConntrackMonitor::EventType kConntrackEvents[] = {
    ConntrackMonitor::EventType::kNew};

ConnmarkUpdater::UDPConnection CreateUDPConnection() {
  const auto src_addr = net_base::IPAddress::CreateFromString(kIPAddress1);
  const auto dst_addr = net_base::IPAddress::CreateFromString(kIPAddress2);
  return ConnmarkUpdater::UDPConnection{.src_addr = *src_addr,
                                        .dst_addr = *dst_addr,
                                        .sport = kPort1,
                                        .dport = kPort2};
}

// Verifies that when creating connmark updater, a listener will be registered
// on ConntrackMonitor and initially the pending list is empty.
TEST(ConnmarkUpdaterTest, CreateConnmarkUpdater) {
  MockConntrackMonitor conntrack_monitor;
  auto runner = std::make_unique<MockProcessRunner>();
  EXPECT_CALL(conntrack_monitor,
              AddListener(ElementsAreArray(kConntrackEvents), _));
  ConnmarkUpdater updater_(&conntrack_monitor, std::move(runner));
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 0);
}

// Verifies that when initial try to update connmark succeeds, the UDP
// Connection will not be added to the pending list.
TEST(ConnmarkUpdaterTest, UpdateUDPConnectionConnmarkSucceed) {
  MockConntrackMonitor conntrack_monitor;
  auto runner = std::make_unique<MockProcessRunner>();
  auto runner_ptr = runner.get();
  ConnmarkUpdater updater_(&conntrack_monitor, std::move(runner));

  std::vector<std::string> argv = {
      "-p",      "UDP",
      "-s",      kIPAddress1,
      "-d",      kIPAddress2,
      "--sport", std::to_string(kPort1),
      "--dport", std::to_string(kPort2),
      "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(*runner_ptr, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(0));
  updater_.UpdateUDPConnectionConnmark(
      CreateUDPConnection(),
      Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
      kFwmarkQoSCategoryMask);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 0);
}

// Verifies that when initial try to update connmark fails, the UDP connection
// will be added to the pending list, and when trying to add the same UDP
// connection, it will only be added once.
TEST(ConnmarkUpdaterTest, UpdateUDPConnectionConnmarkFail) {
  MockConntrackMonitor conntrack_monitor;
  auto runner = std::make_unique<MockProcessRunner>();
  auto runner_ptr = runner.get();
  ConnmarkUpdater updater_(&conntrack_monitor, std::move(runner));

  std::vector<std::string> argv = {
      "-p",      "UDP",
      "-s",      kIPAddress1,
      "-d",      kIPAddress2,
      "--sport", std::to_string(kPort1),
      "--dport", std::to_string(kPort2),
      "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(*runner_ptr, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(-1));
  updater_.UpdateUDPConnectionConnmark(
      CreateUDPConnection(),
      Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
      kFwmarkQoSCategoryMask);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 1);
  Mock::VerifyAndClearExpectations(runner_ptr);

  argv = {"-p",      "UDP",
          "-s",      kIPAddress1,
          "-d",      kIPAddress2,
          "--sport", std::to_string(kPort1),
          "--dport", std::to_string(kPort2),
          "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(*runner_ptr, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(-1));
  updater_.UpdateUDPConnectionConnmark(
      CreateUDPConnection(),
      Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
      kFwmarkQoSCategoryMask);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 1);
  Mock::VerifyAndClearExpectations(runner_ptr);
}

// Verifies that connmark updater will retry updating connmark after receiving
// conntrack event that matches any entry in the pending list, and the pending
// UDP connection entry will be deleted from the pending list after retrying
// updating regardless of the result.
TEST(ConnmarkUpdaterTest, HandleConntrackMonitorEvent) {
  MockConntrackMonitor conntrack_monitor;
  auto runner = std::make_unique<MockProcessRunner>();
  auto runner_ptr = runner.get();
  ConnmarkUpdater updater_(&conntrack_monitor, std::move(runner));

  // Adds UDP connection to the pending list.
  std::vector<std::string> argv = {
      "-p",      "UDP",
      "-s",      kIPAddress1,
      "-d",      kIPAddress2,
      "--sport", std::to_string(kPort1),
      "--dport", std::to_string(kPort2),
      "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(*runner_ptr, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(-1));
  updater_.UpdateUDPConnectionConnmark(
      CreateUDPConnection(),
      Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
      kFwmarkQoSCategoryMask);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 1);
  Mock::VerifyAndClearExpectations(runner_ptr);

  // Verifies that connmark updater will not update connmark when protocol
  // information does not match.
  const ConntrackMonitor::Event kTCPEvent = ConntrackMonitor::Event{
      .src = *net_base::IPAddress::CreateFromString(kIPAddress1),
      .dst = *net_base::IPAddress::CreateFromString(kIPAddress2),
      .sport = kPort1,
      .dport = kPort2,
      .proto = IPPROTO_TCP,
      .type = ConntrackMonitor::EventType::kNew};
  EXPECT_CALL(*runner_ptr, conntrack("-U", _, _)).Times(0);
  conntrack_monitor.DispatchEventForTesting(kTCPEvent);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 1);
  Mock::VerifyAndClearExpectations(runner_ptr);

  // Verifies that connmark updater will not update connmark when conntrack
  // event type does not match.
  const ConntrackMonitor::Event kUDPUpdateEvent = ConntrackMonitor::Event{
      .src = *net_base::IPAddress::CreateFromString(kIPAddress1),
      .dst = *net_base::IPAddress::CreateFromString(kIPAddress2),
      .sport = kPort1,
      .dport = kPort2,
      .proto = IPPROTO_UDP,
      .type = ConntrackMonitor::EventType::kUpdate};
  EXPECT_CALL(*runner_ptr, conntrack("-U", _, _)).Times(0);
  conntrack_monitor.DispatchEventForTesting(kUDPUpdateEvent);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 1);
  Mock::VerifyAndClearExpectations(runner_ptr);

  // Verifies that UDP connection entry in the pending list will be deleted
  // from the list regardless of the result of retrying.
  const ConntrackMonitor::Event kUDPNewEvent = ConntrackMonitor::Event{
      .src = *net_base::IPAddress::CreateFromString(kIPAddress1),
      .dst = *net_base::IPAddress::CreateFromString(kIPAddress2),
      .sport = kPort1,
      .dport = kPort2,
      .proto = IPPROTO_UDP,
      .type = ConntrackMonitor::EventType::kNew};
  EXPECT_CALL(*runner_ptr, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(-1));
  conntrack_monitor.DispatchEventForTesting(kUDPNewEvent);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 0);
  Mock::VerifyAndClearExpectations(runner_ptr);
}
}  // namespace
}  // namespace patchpanel
