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
#include "lorgnette/usb/libusb_wrapper.h"
#include "lorgnette/usb/libusb_wrapper_fake.h"

using ::testing::_;

namespace lorgnette {

namespace {

class MockManager : public Manager {
 public:
  MockManager(base::RepeatingCallback<void(base::TimeDelta)> callback,
              SaneClient* sane_client)
      : Manager(callback, sane_client) {}

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

class MockDeviceTracker : public DeviceTracker {
 public:
  MockDeviceTracker(SaneClient* sane_client, LibusbWrapper* libusb)
      : DeviceTracker(sane_client, libusb) {}

  MOCK_METHOD(StartScannerDiscoveryResponse,
              StartScannerDiscovery,
              (const StartScannerDiscoveryRequest&),
              (override));
  MOCK_METHOD(StopScannerDiscoveryResponse,
              StopScannerDiscovery,
              (const StopScannerDiscoveryRequest&),
              (override));
  MOCK_METHOD(OpenScannerResponse,
              OpenScanner,
              (const OpenScannerRequest&),
              (override));
  MOCK_METHOD(CloseScannerResponse,
              CloseScanner,
              (const CloseScannerRequest&),
              (override));
  MOCK_METHOD(StartPreparedScanResponse,
              StartPreparedScan,
              (const StartPreparedScanRequest&),
              (override));
  MOCK_METHOD(CancelScanResponse,
              CancelScan,
              (const CancelScanRequest& cancel_scan_request),
              (override));
};

class DBusServiceAdaptorTest : public ::testing::Test {
 public:
  DBusServiceAdaptorTest()
      : sane_client_(new SaneClientFake()),
        libusb_(new LibusbWrapperFake()),
        manager_(new MockManager({}, sane_client_.get())),
        tracker_(new MockDeviceTracker(sane_client_.get(), libusb_.get())) {}

 protected:
  std::unique_ptr<SaneClient> sane_client_;
  std::unique_ptr<LibusbWrapper> libusb_;
  MockManager* manager_;  // Owned by DBusServiceAdapator in tests.
  std::unique_ptr<MockDeviceTracker> tracker_;
};

// The adaptor functions contain no real logic and just pass through to the
// underlying implementation, which already has its own unit tests.  We can just
// test here to verify that the correct implementation function gets called for
// each d-bus entry point.

TEST_F(DBusServiceAdaptorTest, ListScanners) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  brillo::ErrorPtr error;
  ListScannersResponse response;
  EXPECT_CALL(*manager_, ListScanners(&error, &response));
  dbus_service.ListScanners(&error, &response);
}

TEST_F(DBusServiceAdaptorTest, GetScannerCapabilities) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  brillo::ErrorPtr error;
  ScannerCapabilities response;
  EXPECT_CALL(*manager_,
              GetScannerCapabilities(&error, "test_device", &response));
  dbus_service.GetScannerCapabilities(&error, "test_device", &response);
}

TEST_F(DBusServiceAdaptorTest, StartScan) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  StartScanRequest request;
  EXPECT_CALL(*manager_, StartScan(EqualsProto(request)));
  dbus_service.StartScan(request);
}

TEST_F(DBusServiceAdaptorTest, GetNextImage) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  GetNextImageRequest request;
  std::unique_ptr<DBusMethodResponse<GetNextImageResponse>> response;
  base::ScopedFD out_fd;
  EXPECT_CALL(*manager_, GetNextImage(_, EqualsProto(request), _));
  dbus_service.GetNextImage(std::move(response), request, std::move(out_fd));
}

TEST_F(DBusServiceAdaptorTest, CancelScanWithoutJobHandle) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  CancelScanRequest request;
  EXPECT_CALL(*tracker_, CancelScan(EqualsProto(request))).Times(0);
  EXPECT_CALL(*manager_, CancelScan(EqualsProto(request)));
  CancelScanResponse response = dbus_service.CancelScan(request);
  EXPECT_THAT(response, EqualsProto(CancelScanResponse()));
}

TEST_F(DBusServiceAdaptorTest, CancelScanByJobHandle) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  CancelScanRequest request;
  request.mutable_job_handle()->set_token("TestJobHandle");
  EXPECT_CALL(*tracker_, CancelScan(EqualsProto(request)));
  EXPECT_CALL(*manager_, CancelScan(EqualsProto(request))).Times(0);
  CancelScanResponse response = dbus_service.CancelScan(request);
  EXPECT_THAT(response, EqualsProto(CancelScanResponse()));
}

TEST_F(DBusServiceAdaptorTest, ToggleDebugging) {
  bool callback_called = false;
  base::RepeatingCallback<void()> callback = base::BindLambdaForTesting(
      [&callback_called]() { callback_called = true; });
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), callback);
  SetDebugConfigRequest request;
  request.set_enabled(true);
  SetDebugConfigResponse response = dbus_service.SetDebugConfig(request);
  EXPECT_TRUE(callback_called);
}

TEST_F(DBusServiceAdaptorTest, UnchangedDebugging) {
  bool callback_called = false;
  base::RepeatingCallback<void()> callback = base::BindLambdaForTesting(
      [&callback_called]() { callback_called = true; });
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), callback);
  SetDebugConfigRequest request;
  request.set_enabled(false);
  SetDebugConfigResponse response = dbus_service.SetDebugConfig(request);
  EXPECT_FALSE(callback_called);
}

TEST_F(DBusServiceAdaptorTest, StartScannerDiscovery) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  StartScannerDiscoveryRequest request;
  EXPECT_CALL(*tracker_.get(), StartScannerDiscovery(EqualsProto(request)));
  StartScannerDiscoveryResponse response =
      dbus_service.StartScannerDiscovery(request);
  EXPECT_THAT(response, EqualsProto(StartScannerDiscoveryResponse()));
}

TEST_F(DBusServiceAdaptorTest, StopScannerDiscovery) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  StopScannerDiscoveryRequest request;
  EXPECT_CALL(*tracker_.get(), StopScannerDiscovery(EqualsProto(request)));
  StopScannerDiscoveryResponse response =
      dbus_service.StopScannerDiscovery(request);
  EXPECT_THAT(response, EqualsProto(StopScannerDiscoveryResponse()));
}

TEST_F(DBusServiceAdaptorTest, OpenScanner) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  OpenScannerRequest request;
  EXPECT_CALL(*tracker_.get(), OpenScanner(EqualsProto(request)));
  OpenScannerResponse response = dbus_service.OpenScanner(request);
  EXPECT_THAT(response, EqualsProto(OpenScannerResponse()));
}

TEST_F(DBusServiceAdaptorTest, CloseScanner) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  CloseScannerRequest request;
  EXPECT_CALL(*tracker_.get(), CloseScanner(EqualsProto(request)));
  CloseScannerResponse response = dbus_service.CloseScanner(request);
  EXPECT_THAT(response, EqualsProto(CloseScannerResponse()));
}

TEST_F(DBusServiceAdaptorTest, SetOptions) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  SetOptionsRequest request;
  // TODO(b/274860786): Implement check for real behavior once implemented.
  SetOptionsResponse response = dbus_service.SetOptions(request);
  EXPECT_THAT(response, EqualsProto(SetOptionsResponse()));
}

TEST_F(DBusServiceAdaptorTest, StartPreparedScan) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  StartPreparedScanRequest request;
  EXPECT_CALL(*tracker_.get(), StartPreparedScan(EqualsProto(request)));
  StartPreparedScanResponse response = dbus_service.StartPreparedScan(request);
  EXPECT_THAT(response, EqualsProto(StartPreparedScanResponse()));
}

TEST_F(DBusServiceAdaptorTest, ReadScanData) {
  auto dbus_service = DBusServiceAdaptor(std::unique_ptr<Manager>(manager_),
                                         tracker_.get(), {});
  ReadScanDataRequest request;
  // TODO(b/297443322): Implement check for real behavior once implemented.
  ReadScanDataResponse response = dbus_service.ReadScanData(request);
  EXPECT_THAT(response, EqualsProto(ReadScanDataResponse()));
}

}  // namespace
}  // namespace lorgnette
