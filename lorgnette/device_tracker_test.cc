// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/device_tracker.h"

#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "lorgnette/sane_client_fake.h"
#include "lorgnette/test_util.h"
#include "lorgnette/usb/libusb_wrapper_fake.h"

using ::testing::_;
using ::testing::ElementsAre;

namespace lorgnette {

namespace {

TEST(DeviceTrackerTest, CreateMultipleSessions) {
  base::test::SingleThreadTaskEnvironment task_environment;
  base::RunLoop run_loop;

  std::vector<std::string> closed_sessions;
  auto signal_handler = base::BindLambdaForTesting(
      [&closed_sessions](const ScannerListChangedSignal& signal) {
        if (signal.event_type() == ScannerListChangedSignal::SESSION_ENDING) {
          closed_sessions.push_back(signal.session_id());
        }
      });

  auto sane_client = std::make_unique<SaneClientFake>();
  auto libusb = std::make_unique<LibusbWrapperFake>();
  auto tracker =
      std::make_unique<DeviceTracker>(sane_client.get(), libusb.get());
  tracker->SetScannerListChangedSignalSender(signal_handler);

  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 0);

  StartScannerDiscoveryRequest start_request;
  start_request.set_client_id("client_1");
  StartScannerDiscoveryResponse response1 =
      tracker->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());
  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 1);

  start_request.set_client_id("client_2");
  StartScannerDiscoveryResponse response2 =
      tracker->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response2.started());
  EXPECT_FALSE(response2.session_id().empty());
  EXPECT_NE(response1.session_id(), response2.session_id());
  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 2);

  StopScannerDiscoveryRequest stop_request;
  stop_request.set_session_id(response1.session_id());
  StopScannerDiscoveryResponse stop1 =
      tracker->StopScannerDiscovery(stop_request);
  EXPECT_TRUE(stop1.stopped());
  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 1);

  stop_request.set_session_id(response2.session_id());
  StopScannerDiscoveryResponse stop2 =
      tracker->StopScannerDiscovery(stop_request);
  EXPECT_TRUE(stop2.stopped());
  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 0);

  EXPECT_THAT(closed_sessions,
              ElementsAre(response1.session_id(), response2.session_id()));
}

TEST(DeviceTrackerTest, CreateDuplicateSessions) {
  base::test::SingleThreadTaskEnvironment task_environment;
  base::RunLoop run_loop;

  std::vector<std::string> closed_sessions;
  auto signal_handler = base::BindLambdaForTesting(
      [&closed_sessions](const ScannerListChangedSignal& signal) {
        if (signal.event_type() == ScannerListChangedSignal::SESSION_ENDING) {
          closed_sessions.push_back(signal.session_id());
        }
      });

  auto sane_client = std::make_unique<SaneClientFake>();
  auto libusb = std::make_unique<LibusbWrapperFake>();
  auto tracker =
      std::make_unique<DeviceTracker>(sane_client.get(), libusb.get());
  tracker->SetScannerListChangedSignalSender(signal_handler);

  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 0);

  StartScannerDiscoveryRequest start_request;
  start_request.set_client_id("client_1");
  StartScannerDiscoveryResponse response1 =
      tracker->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());
  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 1);

  start_request.set_client_id("client_1");
  StartScannerDiscoveryResponse response2 =
      tracker->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response2.started());
  EXPECT_FALSE(response2.session_id().empty());
  EXPECT_EQ(response1.session_id(), response2.session_id());
  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 1);

  StopScannerDiscoveryRequest stop_request;
  stop_request.set_session_id(response1.session_id());
  StopScannerDiscoveryResponse stop1 =
      tracker->StopScannerDiscovery(stop_request);
  EXPECT_TRUE(stop1.stopped());
  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 0);

  stop_request.set_session_id(response2.session_id());
  StopScannerDiscoveryResponse stop2 =
      tracker->StopScannerDiscovery(stop_request);
  EXPECT_TRUE(stop2.stopped());
  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 0);

  // Session ID should get closed twice even though it doesn't exist the second
  // time.
  EXPECT_THAT(closed_sessions,
              ElementsAre(response1.session_id(), response1.session_id()));
}

TEST(DeviceTrackerTest, StartSessionMissingClient) {
  base::test::SingleThreadTaskEnvironment task_environment;
  base::RunLoop run_loop;

  auto sane_client = std::make_unique<SaneClientFake>();
  auto libusb = std::make_unique<LibusbWrapperFake>();
  auto tracker =
      std::make_unique<DeviceTracker>(sane_client.get(), libusb.get());

  StartScannerDiscoveryRequest start_request;
  start_request.set_client_id("");
  StartScannerDiscoveryResponse response =
      tracker->StartScannerDiscovery(start_request);
  EXPECT_FALSE(response.started());
  EXPECT_TRUE(response.session_id().empty());
  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 0);
}

TEST(DeviceTrackerTest, StopSessionMissingID) {
  base::test::SingleThreadTaskEnvironment task_environment;
  base::RunLoop run_loop;

  std::vector<std::string> closed_sessions;
  auto signal_handler = base::BindLambdaForTesting(
      [&closed_sessions](const ScannerListChangedSignal& signal) {
        if (signal.event_type() == ScannerListChangedSignal::SESSION_ENDING) {
          closed_sessions.push_back(signal.session_id());
        }
      });

  auto sane_client = std::make_unique<SaneClientFake>();
  auto libusb = std::make_unique<LibusbWrapperFake>();
  auto tracker =
      std::make_unique<DeviceTracker>(sane_client.get(), libusb.get());
  tracker->SetScannerListChangedSignalSender(signal_handler);

  StopScannerDiscoveryRequest stop_request;
  stop_request.set_session_id("");
  StopScannerDiscoveryResponse response =
      tracker->StopScannerDiscovery(stop_request);
  EXPECT_FALSE(response.stopped());
  EXPECT_TRUE(closed_sessions.empty());
  EXPECT_EQ(tracker->NumActiveDiscoverySessions(), 0);
}

}  // namespace
}  // namespace lorgnette
