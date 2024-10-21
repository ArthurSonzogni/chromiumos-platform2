// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/connmark_updater.h"

#include <string>
#include <vector>

#include <base/test/task_environment.h>
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

ConnmarkUpdater::Conntrack5Tuple CreateUDPConnection() {
  const auto src_addr = net_base::IPAddress::CreateFromString(kIPAddress1);
  const auto dst_addr = net_base::IPAddress::CreateFromString(kIPAddress2);
  return ConnmarkUpdater::Conntrack5Tuple{
      .src_addr = *src_addr,
      .dst_addr = *dst_addr,
      .sport = kPort1,
      .dport = kPort2,
      .proto = ConnmarkUpdater::IPProtocol::kUDP};
}

ConnmarkUpdater::Conntrack5Tuple CreateTCPConnection() {
  const auto src_addr = net_base::IPAddress::CreateFromString(kIPAddress1);
  const auto dst_addr = net_base::IPAddress::CreateFromString(kIPAddress2);
  return ConnmarkUpdater::Conntrack5Tuple{
      .src_addr = *src_addr,
      .dst_addr = *dst_addr,
      .sport = kPort1,
      .dport = kPort2,
      .proto = ConnmarkUpdater::IPProtocol::kTCP};
}

class ConnmarkUpdaterTest : public ::testing::Test {
 private:
  // Note that this needs to be initialized at first, since the ctors of other
  // members may rely on it (e.g., FileDescriptorWatcher).
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
};

// Verifies that when creating connmark updater, a listener will be registered
// on ConntrackMonitor and initially the pending list is empty.
TEST_F(ConnmarkUpdaterTest, CreateConnmarkUpdater) {
  MockConntrackMonitor conntrack_monitor;
  MockProcessRunner runner;
  EXPECT_CALL(conntrack_monitor,
              AddListener(ElementsAreArray(kConntrackEvents), _));
  ConnmarkUpdater updater_(&conntrack_monitor, &runner);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 0);
}

// Verifies that whether initial try to update connmark for TCP connections
// succeeds or fails, the TCP connection will not be added to the pending list.
TEST_F(ConnmarkUpdaterTest, UpdateTCPConnectionConnmark) {
  MockConntrackMonitor conntrack_monitor;
  MockProcessRunner runner;
  ConnmarkUpdater updater_(&conntrack_monitor, &runner);

  std::vector<std::string> argv = {
      "-p",      "TCP",
      "-s",      kIPAddress1,
      "-d",      kIPAddress2,
      "--sport", std::to_string(kPort1),
      "--dport", std::to_string(kPort2),
      "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(runner, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(0));
  updater_.UpdateConnmark(
      CreateTCPConnection(),
      Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
      kFwmarkQoSCategoryMask);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 0);

  argv = {"-p",      "TCP",
          "-s",      kIPAddress1,
          "-d",      kIPAddress2,
          "--sport", std::to_string(kPort1),
          "--dport", std::to_string(kPort2),
          "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(runner, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(-1));
  updater_.UpdateConnmark(
      CreateTCPConnection(),
      Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
      kFwmarkQoSCategoryMask);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 0);
}

// Verifies that when initial try to update connmark succeeds, the UDP
// Connection will not be added to the pending list.
TEST_F(ConnmarkUpdaterTest, UpdateUDPConnectionConnmarkSucceed) {
  MockConntrackMonitor conntrack_monitor;
  MockProcessRunner runner;
  ConnmarkUpdater updater_(&conntrack_monitor, &runner);

  std::vector<std::string> argv = {
      "-p",      "UDP",
      "-s",      kIPAddress1,
      "-d",      kIPAddress2,
      "--sport", std::to_string(kPort1),
      "--dport", std::to_string(kPort2),
      "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(runner, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(0));
  updater_.UpdateConnmark(
      CreateUDPConnection(),
      Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
      kFwmarkQoSCategoryMask);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 0);
}

// Verifies that when initial try to update connmark fails, the UDP connection
// will be added to the pending list, and when trying to add the same UDP
// connection, it will only be added once.
TEST_F(ConnmarkUpdaterTest, UpdateUDPConnectionConnmarkFail) {
  MockConntrackMonitor conntrack_monitor;
  MockProcessRunner runner;
  ConnmarkUpdater updater_(&conntrack_monitor, &runner);

  std::vector<std::string> argv = {
      "-p",      "UDP",
      "-s",      kIPAddress1,
      "-d",      kIPAddress2,
      "--sport", std::to_string(kPort1),
      "--dport", std::to_string(kPort2),
      "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(runner, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(-1));
  updater_.UpdateConnmark(
      CreateUDPConnection(),
      Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
      kFwmarkQoSCategoryMask);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 1);
  Mock::VerifyAndClearExpectations(&runner);

  argv = {"-p",      "UDP",
          "-s",      kIPAddress1,
          "-d",      kIPAddress2,
          "--sport", std::to_string(kPort1),
          "--dport", std::to_string(kPort2),
          "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(runner, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(-1));
  updater_.UpdateConnmark(
      CreateUDPConnection(),
      Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
      kFwmarkQoSCategoryMask);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 1);
  Mock::VerifyAndClearExpectations(&runner);
}

// Verifies that connmark updater will retry updating connmark after receiving
// conntrack event that matches any entry in the pending list, and the pending
// UDP connection entry will be deleted from the pending list after retrying
// updating regardless of the result.
TEST_F(ConnmarkUpdaterTest, HandleConntrackMonitorEvent) {
  MockConntrackMonitor conntrack_monitor;
  MockProcessRunner runner;
  ConnmarkUpdater updater_(&conntrack_monitor, &runner);

  // Adds UDP connection to the pending list.
  std::vector<std::string> argv = {
      "-p",      "UDP",
      "-s",      kIPAddress1,
      "-d",      kIPAddress2,
      "--sport", std::to_string(kPort1),
      "--dport", std::to_string(kPort2),
      "-m",      QoSFwmarkWithMask(QoSCategory::kRealTimeInteractive)};
  EXPECT_CALL(runner, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(-1));
  updater_.UpdateConnmark(
      CreateUDPConnection(),
      Fwmark::FromQoSCategory(QoSCategory::kRealTimeInteractive),
      kFwmarkQoSCategoryMask);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 1);
  Mock::VerifyAndClearExpectations(&runner);

  // Verifies that connmark updater will not update connmark when protocol
  // information does not match.
  const ConntrackMonitor::Event kTCPEvent = ConntrackMonitor::Event{
      .src = *net_base::IPAddress::CreateFromString(kIPAddress1),
      .dst = *net_base::IPAddress::CreateFromString(kIPAddress2),
      .sport = kPort1,
      .dport = kPort2,
      .proto = IPPROTO_TCP,
      .type = ConntrackMonitor::EventType::kNew};
  EXPECT_CALL(runner, conntrack("-U", _, _)).Times(0);
  conntrack_monitor.DispatchEventForTesting(kTCPEvent);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 1);
  Mock::VerifyAndClearExpectations(&runner);

  // Verifies that connmark updater will not update connmark when conntrack
  // event type does not match.
  const ConntrackMonitor::Event kUDPUpdateEvent = ConntrackMonitor::Event{
      .src = *net_base::IPAddress::CreateFromString(kIPAddress1),
      .dst = *net_base::IPAddress::CreateFromString(kIPAddress2),
      .sport = kPort1,
      .dport = kPort2,
      .proto = IPPROTO_UDP,
      .type = ConntrackMonitor::EventType::kUpdate};
  EXPECT_CALL(runner, conntrack("-U", _, _)).Times(0);
  conntrack_monitor.DispatchEventForTesting(kUDPUpdateEvent);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 1);
  Mock::VerifyAndClearExpectations(&runner);

  // Verifies that UDP connection entry in the pending list will be deleted
  // from the list regardless of the result of retrying.
  const ConntrackMonitor::Event kUDPNewEvent = ConntrackMonitor::Event{
      .src = *net_base::IPAddress::CreateFromString(kIPAddress1),
      .dst = *net_base::IPAddress::CreateFromString(kIPAddress2),
      .sport = kPort1,
      .dport = kPort2,
      .proto = IPPROTO_UDP,
      .type = ConntrackMonitor::EventType::kNew};
  EXPECT_CALL(runner, conntrack("-U", ElementsAreArray(argv), _))
      .WillOnce(Return(-1));
  conntrack_monitor.DispatchEventForTesting(kUDPNewEvent);
  EXPECT_EQ(updater_.GetPendingListSizeForTesting(), 0);
  Mock::VerifyAndClearExpectations(&runner);
}
}  // namespace
}  // namespace patchpanel
