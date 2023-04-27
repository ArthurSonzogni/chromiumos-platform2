// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/dbus_service_adaptor.h"

#include <string>
#include <utility>

#include <base/test/bind.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>

#include "lorgnette/enums.h"
#include "lorgnette/sane_client_fake.h"
#include "lorgnette/test_util.h"

using ::testing::_;

namespace lorgnette {

namespace {

MATCHER_P(EqualsProto,
          message,
          "Match a proto Message equal to the matcher's argument.") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

class MockManager : public Manager {
 public:
  MockManager(base::RepeatingCallback<void(base::TimeDelta)> callback,
              std::unique_ptr<SaneClient> sane_client)
      : Manager(callback, std::move(sane_client)) {}
  MockManager()
      : Manager(base::RepeatingCallback<void(base::TimeDelta)>(),
                std::make_unique<SaneClientFake>()) {}

  MOCK_METHOD(bool,
              ListScanners,
              (brillo::ErrorPtr * error,
               ListScannersResponse* scanner_list_out),
              (override));
  MOCK_METHOD(bool,
              GetScannerCapabilities,
              (brillo::ErrorPtr * error,
               const std::string& device_name,
               ScannerCapabilities* capabilities),
              (override));
  MOCK_METHOD(StartScanResponse,
              StartScan,
              (const StartScanRequest& request),
              (override));
  MOCK_METHOD(
      void,
      GetNextImage,
      (std::unique_ptr<DBusMethodResponse<GetNextImageResponse>> response,
       const GetNextImageRequest& get_next_image_request,
       const base::ScopedFD& out_fd),
      (override));
  MOCK_METHOD(CancelScanResponse,
              CancelScan,
              (const CancelScanRequest& cancel_scan_request),
              (override));
};

// The adaptor functions contain no real logic and just pass through to the
// underlying implementation, which already has its own unit tests.  We can just
// test here to verify that the correct implementation function gets called for
// each d-bus entry point.

TEST(DBusServiceAdaptorTest, ListScanners) {
  MockManager* manager = new MockManager();
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager), {});
  brillo::ErrorPtr error;
  ListScannersResponse response;
  EXPECT_CALL(*manager, ListScanners(&error, &response));
  dbus_service.ListScanners(&error, &response);
}

TEST(DBusServiceAdaptorTest, GetScannerCapabilities) {
  MockManager* manager = new MockManager();
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager), {});
  brillo::ErrorPtr error;
  ScannerCapabilities response;
  EXPECT_CALL(*manager,
              GetScannerCapabilities(&error, "test_device", &response));
  dbus_service.GetScannerCapabilities(&error, "test_device", &response);
}

TEST(DBusServiceAdaptorTest, StartScan) {
  MockManager* manager = new MockManager();
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager), {});
  StartScanRequest request;
  EXPECT_CALL(*manager, StartScan(EqualsProto(request)));
  dbus_service.StartScan(request);
}

TEST(DBusServiceAdaptorTest, GetNextImage) {
  MockManager* manager = new MockManager();
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager), {});
  GetNextImageRequest request;
  std::unique_ptr<DBusMethodResponse<GetNextImageResponse>> response;
  base::ScopedFD out_fd;
  EXPECT_CALL(*manager, GetNextImage(_, EqualsProto(request), _));
  dbus_service.GetNextImage(std::move(response), request, std::move(out_fd));
}

TEST(DBusServiceAdaptorTest, CancelScan) {
  MockManager* manager = new MockManager();
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager), {});
  CancelScanRequest request;
  EXPECT_CALL(*manager, CancelScan(EqualsProto(request)));
  dbus_service.CancelScan(request);
}

TEST(DBusServiceAdaptorTest, ToggleDebugging) {
  MockManager* manager = new MockManager();
  bool callback_called = false;
  base::RepeatingCallback<void()> callback = base::BindLambdaForTesting(
      [&callback_called]() { callback_called = true; });
  auto dbus_service =
      DBusServiceAdaptor(std::unique_ptr<Manager>(manager), callback);
  SetDebugConfigRequest request;
  request.set_enabled(true);
  SetDebugConfigResponse response = dbus_service.SetDebugConfig(request);
  EXPECT_TRUE(callback_called);
}

TEST(DBusServiceAdaptorTest, UnchangedDebugging) {
  MockManager* manager = new MockManager();
  bool callback_called = false;
  base::RepeatingCallback<void()> callback = base::BindLambdaForTesting(
      [&callback_called]() { callback_called = true; });
  auto dbus_service =
      DBusServiceAdaptor(std::unique_ptr<Manager>(manager), callback);
  SetDebugConfigRequest request;
  request.set_enabled(false);
  SetDebugConfigResponse response = dbus_service.SetDebugConfig(request);
  EXPECT_FALSE(callback_called);
}

}  // namespace
}  // namespace lorgnette
