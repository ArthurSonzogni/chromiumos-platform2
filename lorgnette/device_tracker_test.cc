// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/device_tracker.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/run_loop.h>
#include <base/files/file.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>

#include "lorgnette/firewall_manager.h"
#include "lorgnette/sane_client_fake.h"
#include "lorgnette/test_util.h"
#include "lorgnette/usb/libusb_wrapper_fake.h"
#include "lorgnette/usb/usb_device_fake.h"

using ::testing::_;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::UnorderedElementsAre;

namespace lorgnette {

namespace {

constexpr char kEpson2Name[] = "epson2:net:127.0.0.1";
constexpr char kEpsonDsName[] = "epsonds:libusb:001:002";

ScanParameters MakeScanParameters(int width, int height) {
  return {
      .format = FrameFormat::kRGB,
      .bytes_per_line = 3 * width,
      .pixels_per_line = width,
      .lines = height,
      .depth = 8,
  };
}

// libusb structures contain various pointers to separately allocated
// memory and other resources.  This struct bundles RAII objects for these
// allocations along with the device that uses them so they can all be deleted
// when the whole struct goes out of scope.
struct UsbDeviceBundle {
  std::unique_ptr<libusb_interface_descriptor> altsetting;
  std::unique_ptr<libusb_interface> interface;
  std::unique_ptr<UsbDeviceFake> device;
  std::string connection_string;
  base::File ippusb_socket;
};

UsbDeviceBundle MakeIPPUSBDevice(const std::string& model) {
  static size_t num = 1;  // Incremented on each call so returned devices are
                          // unique.

  UsbDeviceBundle result;
  result.device = std::make_unique<UsbDeviceFake>();

  // Device descriptor containing basic device info.
  libusb_device_descriptor device_desc = MakeMinimalDeviceDescriptor();
  device_desc.bDeviceClass = LIBUSB_CLASS_PER_INTERFACE;
  device_desc.bNumConfigurations = 1;
  device_desc.iManufacturer = 1;
  device_desc.iProduct = 2;
  device_desc.iSerialNumber = 3;
  result.device->SetStringDescriptors(
      {"", "GoogleTest", model, base::StringPrintf("ABC123%02zu", num)});
  result.device->SetDeviceDescriptor(device_desc);

  // One altsetting with a printer class and the IPP-USB protocol.
  result.altsetting = MakeIppUsbInterfaceDescriptor();

  // One interface containing the altsetting.
  result.interface = std::make_unique<libusb_interface>();
  result.interface->num_altsetting = 1;
  result.interface->altsetting = result.altsetting.get();

  // One config descriptor containing the interface.
  libusb_config_descriptor config_desc;
  memset(&config_desc, 0, sizeof(config_desc));
  config_desc.bLength = sizeof(config_desc);
  config_desc.bDescriptorType = LIBUSB_DT_CONFIG;
  config_desc.wTotalLength = sizeof(config_desc);
  config_desc.bNumInterfaces = 1;
  config_desc.interface = result.interface.get();
  result.device->SetConfigDescriptors({config_desc});

  result.device->SetBusNumber(1);
  result.device->SetDeviceAddress(num);
  result.device->Init();
  ++num;

  return result;
}

class MockFirewallManager : public FirewallManager {
 public:
  explicit MockFirewallManager(const std::string& interface)
      : FirewallManager(interface) {}

  MOCK_METHOD(PortToken, RequestUdpPortAccess, (uint16_t), (override));
};

class DeviceTrackerTest : public testing::Test {
 public:
  // Moves discovery sessions along while recording the events that happen.
  // The caller should first start one or more sessions and record their session
  // IDs into `open_sessions_`.  This will then use the run loop to advance the
  // sessions normally:
  // 1. When a scanner is added, it will be recorded in `discovered_scanners_`.
  // 2. When enumeration finishes, that session will be closed.
  // 3. When a session closes, it is recorded into `closed_sessions_`.
  // 4. When all sessions originally present in `open_sessions_` are closed, the
  //    entire run loop is terminated.
  void RecordingSignalSender(const ScannerListChangedSignal& signal) {
    switch (signal.event_type()) {
      case ScannerListChangedSignal::ENUM_COMPLETE: {
        StopScannerDiscoveryRequest stop_request;
        stop_request.set_session_id(signal.session_id());
        tracker_->StopScannerDiscovery(stop_request);
        break;
      }
      case ScannerListChangedSignal::SESSION_ENDING:
        closed_sessions_.push_back(signal.session_id());
        open_sessions_.erase(signal.session_id());
        if (open_sessions_.empty()) {
          run_loop_quit_.Run();
        }
        break;
      case ScannerListChangedSignal::SCANNER_ADDED: {
        std::unique_ptr<ScannerInfo> info(signal.scanner().New());
        info->CopyFrom(signal.scanner());
        discovered_scanners_[signal.session_id()].insert(std::move(info));
        break;
      }
      case ScannerListChangedSignal::SCANNER_REMOVED:
        LOG(ERROR) << "Scanner " << signal.scanner().name()
                   << " removed from session " << signal.session_id();
        NOTIMPLEMENTED();
        break;
      default:
        // Do nothing for the internal proto enum values.
        break;
    }
  }

 protected:
  void SetUp() override {
    run_loop_quit_ = run_loop_.QuitClosure();
    sane_client_ = std::make_unique<SaneClientFake>();
    libusb_ = std::make_unique<LibusbWrapperFake>();
    tracker_ =
        std::make_unique<DeviceTracker>(sane_client_.get(), libusb_.get());

    CHECK(socket_dir_.CreateUniqueTempDir());
    sane_client_->SetIppUsbSocketDir(socket_dir_.GetPath());

    // Using base::Unretained is safe here because the signal sender is removed
    // in TearDown() while this object is still alive.
    tracker_->SetScannerListChangedSignalSender(base::BindRepeating(
        &DeviceTrackerTest::RecordingSignalSender, base::Unretained(this)));

    firewall_manager_ =
        std::make_unique<MockFirewallManager>(/*interface=*/"test");
    tracker_->SetFirewallManager(firewall_manager_.get());
  }

  void TearDown() override {
    tracker_->SetScannerListChangedSignalSender(base::DoNothing());
  }

  // Create the appropriate "socket" in the filesystem to allow an IPP-USB
  // device to pass the SaneDevice checks and fill in the connection string
  // pointing to the socket.
  void CreateIPPUSBSocket(UsbDeviceBundle& device) {
    std::string socket_name = base::StringPrintf(
        "%04x-%04x.sock", device.device->GetVid(), device.device->GetPid());
    base::FilePath socket_path = socket_dir_.GetPath().Append(socket_name);
    device.ippusb_socket = base::File(
        socket_path, base::File::FLAG_CREATE | base::File::FLAG_WRITE);

    device.connection_string = base::StringPrintf(
        "airscan:escl:%s %s:unix://%s/eSCL/",
        device.device
            ->GetStringDescriptor(
                device.device->GetDeviceDescriptor()->iManufacturer)
            ->c_str(),
        device.device
            ->GetStringDescriptor(
                device.device->GetDeviceDescriptor()->iProduct)
            ->c_str(),
        socket_name.c_str());
  }

  SaneDeviceFake* CreateFakeScanner(const std::string& name) {
    auto scanner = std::make_unique<SaneDeviceFake>();
    SaneDeviceFake* raw_scanner = scanner.get();
    sane_client_->SetDeviceForName(name, std::move(scanner));
    return raw_scanner;
  }

  SaneDeviceFake* CreateFakeScanner(const std::string& name,
                                    const std::string& manufacturer,
                                    const std::string& model,
                                    const std::string& type) {
    sane_client_->AddDeviceListing(name, manufacturer, model, type);
    return CreateFakeScanner(name);
  }

  void SetQuitClosure(base::RepeatingClosure closure) {
    run_loop_quit_ = std::move(closure);
  }

  base::test::SingleThreadTaskEnvironment task_environment_;
  base::RunLoop run_loop_;
  base::RepeatingClosure run_loop_quit_;
  std::unique_ptr<SaneClientFake> sane_client_;
  std::unique_ptr<LibusbWrapperFake> libusb_;
  std::unique_ptr<MockFirewallManager> firewall_manager_;
  std::unique_ptr<DeviceTracker> tracker_;  // Must come after all the mocks.
  base::ScopedTempDir socket_dir_;

  // Set of open session ids that will be tracked by `RecordingSignalSender`.
  std::set<std::string> open_sessions_;

  // List of sessions that were closed while running the discovery session loop.
  std::vector<std::string> closed_sessions_;

  // For each session ID that has at least one scanner discovered, a set of all
  // the discovered scanners.
  std::map<std::string, std::set<std::unique_ptr<ScannerInfo>>>
      discovered_scanners_;
};

TEST_F(DeviceTrackerTest, CreateMultipleSessions) {
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 0);

  StartScannerDiscoveryRequest start_request;
  start_request.set_client_id("client_1");
  StartScannerDiscoveryResponse response1 =
      tracker_->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 1);

  start_request.set_client_id("client_2");
  StartScannerDiscoveryResponse response2 =
      tracker_->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response2.started());
  EXPECT_FALSE(response2.session_id().empty());
  EXPECT_NE(response1.session_id(), response2.session_id());
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 2);

  StopScannerDiscoveryRequest stop_request;
  stop_request.set_session_id(response1.session_id());
  StopScannerDiscoveryResponse stop1 =
      tracker_->StopScannerDiscovery(stop_request);
  EXPECT_TRUE(stop1.stopped());
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 1);

  stop_request.set_session_id(response2.session_id());
  StopScannerDiscoveryResponse stop2 =
      tracker_->StopScannerDiscovery(stop_request);
  EXPECT_TRUE(stop2.stopped());
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 0);

  EXPECT_THAT(closed_sessions_,
              ElementsAre(response1.session_id(), response2.session_id()));
}

TEST_F(DeviceTrackerTest, CreateDuplicateSessions) {
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 0);

  StartScannerDiscoveryRequest start_request;
  start_request.set_client_id("client_1");
  StartScannerDiscoveryResponse response1 =
      tracker_->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 1);

  start_request.set_client_id("client_1");
  StartScannerDiscoveryResponse response2 =
      tracker_->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response2.started());
  EXPECT_FALSE(response2.session_id().empty());
  EXPECT_EQ(response1.session_id(), response2.session_id());
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 1);

  StopScannerDiscoveryRequest stop_request;
  stop_request.set_session_id(response1.session_id());
  StopScannerDiscoveryResponse stop1 =
      tracker_->StopScannerDiscovery(stop_request);
  EXPECT_TRUE(stop1.stopped());
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 0);

  stop_request.set_session_id(response2.session_id());
  StopScannerDiscoveryResponse stop2 =
      tracker_->StopScannerDiscovery(stop_request);
  EXPECT_TRUE(stop2.stopped());
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 0);

  // Session ID should get closed twice even though it doesn't exist the second
  // time.
  EXPECT_THAT(closed_sessions_,
              ElementsAre(response1.session_id(), response1.session_id()));
}

TEST_F(DeviceTrackerTest, StartSessionMissingClient) {
  StartScannerDiscoveryRequest start_request;
  start_request.set_client_id("");
  StartScannerDiscoveryResponse response =
      tracker_->StartScannerDiscovery(start_request);
  EXPECT_FALSE(response.started());
  EXPECT_TRUE(response.session_id().empty());
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 0);
}

TEST_F(DeviceTrackerTest, StopSessionMissingID) {
  StopScannerDiscoveryRequest stop_request;
  stop_request.set_session_id("");
  StopScannerDiscoveryResponse response =
      tracker_->StopScannerDiscovery(stop_request);
  EXPECT_FALSE(response.stopped());
  EXPECT_TRUE(closed_sessions_.empty());
  EXPECT_EQ(tracker_->NumActiveDiscoverySessions(), 0);
}

// Test the whole flow with several fake USB devices.  Confirm that
// exactly and only the devices that fully match the checks and have a SANE
// backend have a signal emitted before shutting down the session.
TEST_F(DeviceTrackerTest, CompleteDiscoverySession) {
  // Scanner that supports eSCL over IPP-USB.
  auto ippusb_escl_device = MakeIPPUSBDevice("eSCL Scanner 3000");
  ippusb_escl_device.device->MutableDeviceDescriptor().idVendor = 0x04a9;
  ippusb_escl_device.device->Init();
  CreateIPPUSBSocket(ippusb_escl_device);
  CreateFakeScanner(ippusb_escl_device.connection_string);

  // Printer that supports IPP-USB but not eSCL.
  auto ippusb_printer = MakeIPPUSBDevice("IPP-USB Printer 2000");
  ippusb_printer.device->MutableDeviceDescriptor().idProduct = 0x6543;
  ippusb_printer.device->Init();
  CreateIPPUSBSocket(ippusb_printer);

  // Printer that doesn't support IPP-USB.
  auto printer_altsetting = MakeIppUsbInterfaceDescriptor();
  printer_altsetting->bInterfaceProtocol = 0;
  auto printer_interface = std::make_unique<libusb_interface>();
  printer_interface->num_altsetting = 1;
  printer_interface->altsetting = printer_altsetting.get();
  auto usb_printer = MakeIPPUSBDevice("USB Printer 1000");
  usb_printer.device->MutableDeviceDescriptor().idProduct = 0x7654;
  usb_printer.device->MutableConfigDescriptor(0).interface =
      printer_interface.get();
  usb_printer.device->SetStringDescriptors(
      {"", "GoogleTest", "USB Printer 1000"});
  usb_printer.device->Init();

  // Not a printer at all.
  auto non_printer = UsbDeviceFake::Clone(*usb_printer.device.get());
  non_printer->MutableDeviceDescriptor().idProduct = 0x7654;
  non_printer->MutableDeviceDescriptor().bDeviceClass = LIBUSB_DT_HUB;
  non_printer->SetStringDescriptors({"", "GoogleTest", "USB Gadget 500"});
  non_printer->Init();

  std::vector<std::unique_ptr<UsbDevice>> device_list;
  device_list.emplace_back(std::move(non_printer));
  device_list.emplace_back(std::move(ippusb_escl_device.device));
  device_list.emplace_back(std::move(ippusb_printer.device));
  device_list.emplace_back(std::move(usb_printer.device));
  libusb_->SetDevices(std::move(device_list));

  sane_client_->SetListDevicesResult(true);
  // Duplicates of eSCL over ippusb that are filtered out.
  CreateFakeScanner("pixma:04A94321_12AF", "GoogleTest", "eSCL Scanner 3001",
                    "eSCL");
  CreateFakeScanner("epson2:libusb:001:001", "GoogleTest", "eSCL Scanner 3002",
                    "eSCL");

  // Unique USB device without ippusb support that is added during SANE probing.
  CreateFakeScanner(kEpsonDsName, "GoogleTest", "SANE Scanner 4000", "USB");

  // Unique non-eSCL network device that is added during SANE probing.
  CreateFakeScanner(kEpson2Name, "GoogleTest", "GoogleTest SANE NetScan 4200",
                    "Network");

  EXPECT_CALL(*firewall_manager_, RequestUdpPortAccess(8612))
      .WillOnce(testing::Return(PortToken(/*firewall_manager=*/nullptr,
                                          /*port=*/8612)));

  StartScannerDiscoveryRequest start_request;
  start_request.set_client_id("CompleteDiscoverySession");
  start_request.set_preferred_only(true);
  StartScannerDiscoveryResponse response =
      tracker_->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response.started());
  EXPECT_FALSE(response.session_id().empty());
  open_sessions_.insert(response.session_id());

  run_loop_.Run();

  ScannerInfo escl3000;
  escl3000.set_manufacturer("GoogleTest");
  escl3000.set_model("eSCL Scanner 3000");
  escl3000.set_connection_type(lorgnette::CONNECTION_USB);
  escl3000.set_display_name("GoogleTest eSCL Scanner 3000 (USB)");
  escl3000.set_secure(true);
  ScannerInfo sane4000;
  sane4000.set_manufacturer("GoogleTest");
  sane4000.set_model("SANE Scanner 4000");
  sane4000.set_display_name("GoogleTest SANE Scanner 4000 (USB)");
  sane4000.set_connection_type(lorgnette::CONNECTION_USB);
  sane4000.set_secure(true);
  ScannerInfo sane4200;
  sane4200.set_manufacturer("GoogleTest");
  sane4200.set_model("GoogleTest SANE NetScan 4200");
  sane4200.set_display_name("GoogleTest SANE NetScan 4200");
  sane4200.set_connection_type(lorgnette::CONNECTION_NETWORK);
  sane4200.set_secure(false);

  EXPECT_THAT(closed_sessions_, ElementsAre(response.session_id()));
  EXPECT_THAT(discovered_scanners_[response.session_id()],
              UnorderedElementsAre(MatchesScannerInfo(escl3000),
                                   MatchesScannerInfo(sane4000),
                                   MatchesScannerInfo(sane4200)));
}

TEST_F(DeviceTrackerTest, DiscoverySessionCachingUnpluggedDeviceRemoved) {
  sane_client_->SetListDevicesResult(true);
  CreateFakeScanner("epson2:libusb:001:001", "GoogleTest", "Scanner 1", "USB");

  EXPECT_CALL(*firewall_manager_, RequestUdpPortAccess(8612))
      .WillRepeatedly([] {
        return PortToken(/*firewall_manager=*/nullptr,
                         /*port=*/8612);
      });

  // First discovery session finds the device.
  base::RunLoop run_loop1;
  SetQuitClosure(run_loop1.QuitClosure());
  StartScannerDiscoveryRequest start_request1;
  start_request1.set_client_id("DiscoverySessionCaching1");
  StartScannerDiscoveryResponse response1 =
      tracker_->StartScannerDiscovery(start_request1);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());
  run_loop1.Run();
  EXPECT_THAT(closed_sessions_, ElementsAre(response1.session_id()));
  EXPECT_EQ(discovered_scanners_[response1.session_id()].size(), 1);

  sane_client_->RemoveDeviceListing("epson2:libusb:001:001");

  // Second discovery session does not find the device.
  base::RunLoop run_loop2;
  SetQuitClosure(run_loop2.QuitClosure());
  StartScannerDiscoveryRequest start_request2;
  start_request2.set_client_id("DiscoverySessionCaching2");
  StartScannerDiscoveryResponse response2 =
      tracker_->StartScannerDiscovery(start_request2);
  EXPECT_TRUE(response2.started());
  EXPECT_FALSE(response2.session_id().empty());
  run_loop2.Run();
  EXPECT_THAT(closed_sessions_,
              ElementsAre(response1.session_id(), response2.session_id()));
  EXPECT_EQ(discovered_scanners_[response2.session_id()].size(), 0);
}

TEST_F(DeviceTrackerTest, DiscoverySessionCachingBlockedIPPUSBDeviceIncluded) {
  auto usb_device = MakeIPPUSBDevice("Scanner 1");
  CreateIPPUSBSocket(usb_device);
  CreateFakeScanner(usb_device.connection_string);

  std::vector<std::unique_ptr<UsbDevice>> device_list;
  device_list.emplace_back(std::move(usb_device.device));
  libusb_->SetDevices(std::move(device_list));
  sane_client_->SetListDevicesResult(true);

  EXPECT_CALL(*firewall_manager_, RequestUdpPortAccess(8612))
      .WillRepeatedly([] {
        return PortToken(/*firewall_manager=*/nullptr,
                         /*port=*/8612);
      });

  // First discovery session finds the device.
  base::RunLoop run_loop1;
  SetQuitClosure(run_loop1.QuitClosure());
  StartScannerDiscoveryRequest start_request1;
  start_request1.set_client_id("DiscoverySessionCaching1");
  StartScannerDiscoveryResponse response1 =
      tracker_->StartScannerDiscovery(start_request1);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());
  run_loop1.Run();
  EXPECT_THAT(closed_sessions_, ElementsAre(response1.session_id()));
  EXPECT_EQ(discovered_scanners_[response1.session_id()].size(), 1);

  // After removing the SANE device, it can no longer be opened for probing, but
  // it is still included in the listing.  This is similar to having a device
  // opened by another client.
  sane_client_->SetDeviceForName(usb_device.connection_string, nullptr);

  // Second discovery session finds the device because it reads from the cache.
  base::RunLoop run_loop2;
  SetQuitClosure(run_loop2.QuitClosure());
  StartScannerDiscoveryRequest start_request2;
  start_request2.set_client_id("DiscoverySessionCaching2");
  StartScannerDiscoveryResponse response2 =
      tracker_->StartScannerDiscovery(start_request2);
  EXPECT_TRUE(response2.started());
  EXPECT_FALSE(response2.session_id().empty());
  run_loop2.Run();
  EXPECT_THAT(closed_sessions_,
              ElementsAre(response1.session_id(), response2.session_id()));
  EXPECT_EQ(discovered_scanners_[response2.session_id()].size(), 1);
}

TEST_F(DeviceTrackerTest, DiscoverySessionCachingBlockedSANEDeviceIncluded) {
  sane_client_->SetListDevicesResult(true);
  CreateFakeScanner("epson2:libusb:001:001", "GoogleTest", "Scanner 1", "USB");

  EXPECT_CALL(*firewall_manager_, RequestUdpPortAccess(8612))
      .WillRepeatedly([] {
        return PortToken(/*firewall_manager=*/nullptr,
                         /*port=*/8612);
      });

  // First discovery session finds the device.
  base::RunLoop run_loop1;
  SetQuitClosure(run_loop1.QuitClosure());
  StartScannerDiscoveryRequest start_request1;
  start_request1.set_client_id("DiscoverySessionCaching1");
  StartScannerDiscoveryResponse response1 =
      tracker_->StartScannerDiscovery(start_request1);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());
  run_loop1.Run();
  EXPECT_THAT(closed_sessions_, ElementsAre(response1.session_id()));
  EXPECT_EQ(discovered_scanners_[response1.session_id()].size(), 1);

  // After removing the SANE device, it can no longer be opened for probing, but
  // it is still included in the listing.  This is similar to having a device
  // opened by another client.
  sane_client_->SetDeviceForName("epson2:libusb:001:001", nullptr);

  // Second discovery session finds the device because it reads from the cache.
  base::RunLoop run_loop2;
  SetQuitClosure(run_loop2.QuitClosure());
  StartScannerDiscoveryRequest start_request2;
  start_request2.set_client_id("DiscoverySessionCaching2");
  StartScannerDiscoveryResponse response2 =
      tracker_->StartScannerDiscovery(start_request2);
  EXPECT_TRUE(response2.started());
  EXPECT_FALSE(response2.session_id().empty());
  run_loop2.Run();
  EXPECT_THAT(closed_sessions_,
              ElementsAre(response1.session_id(), response2.session_id()));
  EXPECT_EQ(discovered_scanners_[response2.session_id()].size(), 1);
}

TEST_F(DeviceTrackerTest, DiscoverySessionClosesOpenScanners) {
  sane_client_->SetListDevicesResult(true);
  CreateFakeScanner("epson2:libusb:001:001", "GoogleTest", "Scanner 1", "USB");

  EXPECT_CALL(*firewall_manager_, RequestUdpPortAccess(8612))
      .WillRepeatedly([] {
        return PortToken(/*firewall_manager=*/nullptr,
                         /*port=*/8612);
      });

  // First discovery session finds the device.
  base::RunLoop run_loop1;
  SetQuitClosure(run_loop1.QuitClosure());
  StartScannerDiscoveryRequest start_request;
  start_request.set_client_id("DiscoverySessionClosesOpenScanners");
  StartScannerDiscoveryResponse discovery_response1 =
      tracker_->StartScannerDiscovery(start_request);
  EXPECT_TRUE(discovery_response1.started());
  EXPECT_FALSE(discovery_response1.session_id().empty());
  open_sessions_.insert(discovery_response1.session_id());
  run_loop1.Run();
  EXPECT_THAT(closed_sessions_, ElementsAre(discovery_response1.session_id()));
  EXPECT_EQ(discovered_scanners_[discovery_response1.session_id()].size(), 1);

  // Opening the discovered device succeeds.
  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string(
      "epson2:libusb:001:001");
  open_request.set_client_id("DiscoverySessionClosesOpenScanners");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  EXPECT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);
  ASSERT_TRUE(open_response.has_config());
  auto& handle = open_response.config().scanner();
  EXPECT_FALSE(handle.token().empty());

  // Second discovery session succeeds and invalidates the handle.
  base::RunLoop run_loop2;
  SetQuitClosure(run_loop2.QuitClosure());
  StartScannerDiscoveryResponse discovery_response2 =
      tracker_->StartScannerDiscovery(start_request);
  EXPECT_TRUE(discovery_response2.started());
  EXPECT_FALSE(discovery_response2.session_id().empty());
  open_sessions_.insert(discovery_response2.session_id());
  run_loop2.Run();
  EXPECT_THAT(closed_sessions_, ElementsAre(discovery_response1.session_id(),
                                            discovery_response2.session_id()));
  EXPECT_EQ(discovered_scanners_[discovery_response2.session_id()].size(), 1);

  // Handle is no longer valid.
  GetCurrentConfigRequest config_request;
  *config_request.mutable_scanner() = handle;
  GetCurrentConfigResponse config_response =
      tracker_->GetCurrentConfig(config_request);
  EXPECT_THAT(config_response.scanner(), EqualsProto(handle));
  EXPECT_NE(config_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_FALSE(config_response.has_config());
}

TEST_F(DeviceTrackerTest, DiscoverySessionLocalDevices) {
  sane_client_->SetListDevicesResult(true);

  // Unique USB device without ippusb support that is added during SANE probing.
  CreateFakeScanner(kEpsonDsName, "GoogleTest", "SANE Scanner 4000", "USB");

  // Unique non-eSCL network device that is added during SANE probing.
  CreateFakeScanner(kEpson2Name, "GoogleTest", "GoogleTest SANE NetScan 4200",
                    "Network");

  EXPECT_CALL(*firewall_manager_, RequestUdpPortAccess(8612))
      .WillRepeatedly([] {
        return PortToken(/*firewall_manager=*/nullptr,
                         /*port=*/8612);
      });

  // Session that should get both local and net devices.
  StartScannerDiscoveryRequest full_start_request;
  full_start_request.set_client_id("full_discovery");
  StartScannerDiscoveryResponse full_response =
      tracker_->StartScannerDiscovery(full_start_request);
  EXPECT_TRUE(full_response.started());
  EXPECT_FALSE(full_response.session_id().empty());
  open_sessions_.insert(full_response.session_id());

  // Session that only gets local devices.
  StartScannerDiscoveryRequest local_start_request;
  local_start_request.set_client_id("local_discovery");
  local_start_request.set_local_only(true);
  StartScannerDiscoveryResponse local_response =
      tracker_->StartScannerDiscovery(local_start_request);
  EXPECT_TRUE(local_response.started());
  EXPECT_FALSE(local_response.session_id().empty());
  open_sessions_.insert(local_response.session_id());

  run_loop_.Run();

  ScannerInfo usb_device;
  usb_device.set_manufacturer("GoogleTest");
  usb_device.set_model("SANE Scanner 4000");
  usb_device.set_display_name("GoogleTest SANE Scanner 4000 (USB)");
  usb_device.set_connection_type(lorgnette::CONNECTION_USB);
  usb_device.set_secure(true);
  ScannerInfo net_device;
  net_device.set_manufacturer("GoogleTest");
  net_device.set_model("GoogleTest SANE NetScan 4200");
  net_device.set_display_name("GoogleTest SANE NetScan 4200");
  net_device.set_connection_type(lorgnette::CONNECTION_NETWORK);
  net_device.set_secure(false);

  EXPECT_THAT(discovered_scanners_[full_response.session_id()],
              UnorderedElementsAre(MatchesScannerInfo(usb_device),
                                   MatchesScannerInfo(net_device)));
  EXPECT_THAT(discovered_scanners_[local_response.session_id()],
              ElementsAre(MatchesScannerInfo(usb_device)));
}

TEST_F(DeviceTrackerTest, OpenScannerEmptyDevice) {
  OpenScannerRequest request;
  request.set_client_id("DeviceTrackerTest");
  auto response = tracker_->OpenScanner(request);

  EXPECT_THAT(response.scanner_id(), EqualsProto(request.scanner_id()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_INVALID);
  EXPECT_EQ(tracker_->NumOpenScanners(), 0);
}

TEST_F(DeviceTrackerTest, OpenScannerEmptyString) {
  OpenScannerRequest request;
  request.mutable_scanner_id()->set_connection_string("Test");
  OpenScannerResponse response = tracker_->OpenScanner(request);

  EXPECT_THAT(response.scanner_id(), EqualsProto(request.scanner_id()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_INVALID);
  EXPECT_EQ(tracker_->NumOpenScanners(), 0);
}

TEST_F(DeviceTrackerTest, OpenScannerNoDevice) {
  OpenScannerRequest request;
  request.mutable_scanner_id()->set_connection_string("Test");
  request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse response = tracker_->OpenScanner(request);

  EXPECT_THAT(response.scanner_id(), EqualsProto(request.scanner_id()));
  EXPECT_NE(response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_EQ(tracker_->NumOpenScanners(), 0);
}

TEST_F(DeviceTrackerTest, OpenScannerFirstClientSucceeds) {
  CreateFakeScanner("Test");

  OpenScannerRequest request;
  request.mutable_scanner_id()->set_connection_string("Test");
  request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse response = tracker_->OpenScanner(request);

  EXPECT_THAT(response.scanner_id(), EqualsProto(request.scanner_id()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(response.config().scanner(), Not(EqualsProto(ScannerHandle())));
  EXPECT_EQ(tracker_->NumOpenScanners(), 1);
}

TEST_F(DeviceTrackerTest, OpenScannerSameClientSucceedsTwice) {
  CreateFakeScanner("Test");

  OpenScannerRequest request;
  request.mutable_scanner_id()->set_connection_string("Test");
  request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse response1 = tracker_->OpenScanner(request);
  OpenScannerResponse response2 = tracker_->OpenScanner(request);

  EXPECT_THAT(response1.scanner_id(), EqualsProto(request.scanner_id()));
  EXPECT_EQ(response1.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(response1.config().scanner(), Not(EqualsProto(ScannerHandle())));

  EXPECT_THAT(response2.scanner_id(), EqualsProto(request.scanner_id()));
  EXPECT_EQ(response2.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(response2.config().scanner(), Not(EqualsProto(ScannerHandle())));

  EXPECT_THAT(response2.config().scanner(),
              Not(EqualsProto(response1.config().scanner())));
  EXPECT_EQ(tracker_->NumOpenScanners(), 1);
}

TEST_F(DeviceTrackerTest, OpenScannerSecondClientFails) {
  CreateFakeScanner("Test");

  OpenScannerRequest request;
  request.mutable_scanner_id()->set_connection_string("Test");
  request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse response1 = tracker_->OpenScanner(request);

  // Re-insert the test device because the fake SANE client deletes it after one
  // connection.
  auto scanner2 = std::make_unique<SaneDeviceFake>();
  sane_client_->SetDeviceForName("Test", std::move(scanner2));

  request.set_client_id("DeviceTrackerTest2");
  OpenScannerResponse response2 = tracker_->OpenScanner(request);

  EXPECT_THAT(response1.scanner_id(), EqualsProto(request.scanner_id()));
  EXPECT_EQ(response1.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(response1.config().scanner(), Not(EqualsProto(ScannerHandle())));

  EXPECT_THAT(response2.scanner_id(), EqualsProto(request.scanner_id()));
  EXPECT_EQ(response2.result(), OPERATION_RESULT_DEVICE_BUSY);
  EXPECT_THAT(response2.config().scanner(), EqualsProto(ScannerHandle()));

  EXPECT_EQ(tracker_->NumOpenScanners(), 1);
}

TEST_F(DeviceTrackerTest, CloseScannerMissingHandle) {
  CloseScannerRequest request;
  CloseScannerResponse response = tracker_->CloseScanner(request);

  EXPECT_THAT(request.scanner(), EqualsProto(response.scanner()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_INVALID);
  EXPECT_EQ(tracker_->NumOpenScanners(), 0);
}

TEST_F(DeviceTrackerTest, CloseScannerInvalidHandle) {
  CloseScannerRequest request;
  request.mutable_scanner()->set_token("NoSuchScanner");
  CloseScannerResponse response = tracker_->CloseScanner(request);

  EXPECT_THAT(request.scanner(), EqualsProto(response.scanner()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_MISSING);
  EXPECT_EQ(tracker_->NumOpenScanners(), 0);
}

TEST_F(DeviceTrackerTest, CloseScannerSuccess) {
  CreateFakeScanner("Test");

  OpenScannerRequest request1;
  request1.mutable_scanner_id()->set_connection_string("Test");
  request1.set_client_id("DeviceTrackerTest");
  OpenScannerResponse response1 = tracker_->OpenScanner(request1);

  EXPECT_EQ(tracker_->NumOpenScanners(), 1);

  CloseScannerRequest request2;
  *request2.mutable_scanner() = response1.config().scanner();
  CloseScannerResponse response2 = tracker_->CloseScanner(request2);

  EXPECT_THAT(request2.scanner(), EqualsProto(response2.scanner()));
  EXPECT_EQ(response2.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_EQ(tracker_->NumOpenScanners(), 0);
}

TEST_F(DeviceTrackerTest, CloseScannerTwiceFails) {
  CreateFakeScanner("Test");

  OpenScannerRequest request1;
  request1.mutable_scanner_id()->set_connection_string("Test");
  request1.set_client_id("DeviceTrackerTest");
  OpenScannerResponse response1 = tracker_->OpenScanner(request1);

  EXPECT_EQ(tracker_->NumOpenScanners(), 1);

  CloseScannerRequest request2;
  *request2.mutable_scanner() = response1.config().scanner();
  CloseScannerResponse response2 = tracker_->CloseScanner(request2);
  CloseScannerResponse response3 = tracker_->CloseScanner(request2);

  EXPECT_THAT(request2.scanner(), EqualsProto(response2.scanner()));
  EXPECT_EQ(response2.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(request2.scanner(), EqualsProto(response3.scanner()));
  EXPECT_EQ(response3.result(), OPERATION_RESULT_MISSING);
  EXPECT_EQ(tracker_->NumOpenScanners(), 0);
}

TEST_F(DeviceTrackerTest, CloseScannerFreesDevice) {
  CreateFakeScanner("Test");

  // First client succeeds.
  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse response1 = tracker_->OpenScanner(open_request);
  EXPECT_EQ(tracker_->NumOpenScanners(), 1);

  // This will fail because the device is still open.
  open_request.set_client_id("DeviceTrackerTest2");
  OpenScannerResponse response2 = tracker_->OpenScanner(open_request);
  EXPECT_EQ(tracker_->NumOpenScanners(), 1);

  // Close first client's handle to free up the device.
  CloseScannerRequest close_request;
  *close_request.mutable_scanner() = response1.config().scanner();
  CloseScannerResponse response3 = tracker_->CloseScanner(close_request);
  EXPECT_EQ(tracker_->NumOpenScanners(), 0);

  // Now the second client can open the device.
  OpenScannerResponse response4 = tracker_->OpenScanner(open_request);
  EXPECT_EQ(tracker_->NumOpenScanners(), 1);

  EXPECT_THAT(response1.scanner_id(), EqualsProto(open_request.scanner_id()));
  EXPECT_EQ(response1.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(response1.config().scanner(), Not(EqualsProto(ScannerHandle())));

  EXPECT_THAT(response2.scanner_id(), EqualsProto(open_request.scanner_id()));
  EXPECT_EQ(response2.result(), OPERATION_RESULT_DEVICE_BUSY);
  EXPECT_THAT(response2.config().scanner(), EqualsProto(ScannerHandle()));

  EXPECT_THAT(response3.scanner(), EqualsProto(close_request.scanner()));
  EXPECT_EQ(response3.result(), OPERATION_RESULT_SUCCESS);

  EXPECT_THAT(response4.scanner_id(), EqualsProto(open_request.scanner_id()));
  EXPECT_EQ(response4.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(response4.config().scanner(), Not(EqualsProto(ScannerHandle())));
  EXPECT_THAT(response4.config().scanner(),
              Not(EqualsProto(response1.config().scanner())));
}

TEST_F(DeviceTrackerTest, SetOptionsMissingHandleFails) {
  SetOptionsRequest request;
  request.add_options()->set_name("option");
  SetOptionsResponse response = tracker_->SetOptions(request);
  EXPECT_THAT(response.scanner(), EqualsProto(request.scanner()));
  ASSERT_TRUE(response.results().contains("option"));
  EXPECT_EQ(response.results().at("option"), OPERATION_RESULT_INVALID);
  EXPECT_FALSE(response.has_config());
}

TEST_F(DeviceTrackerTest, SetOptionsEmptyHandleFails) {
  SetOptionsRequest request;
  request.mutable_scanner()->set_token("");
  request.add_options()->set_name("option");
  SetOptionsResponse response = tracker_->SetOptions(request);
  EXPECT_THAT(response.scanner(), EqualsProto(request.scanner()));
  ASSERT_TRUE(response.results().contains("option"));
  EXPECT_EQ(response.results().at("option"), OPERATION_RESULT_INVALID);
  EXPECT_FALSE(response.has_config());
}

TEST_F(DeviceTrackerTest, SetOptionsInvalidHandleFails) {
  SetOptionsRequest request;
  request.mutable_scanner()->set_token("NoSuchScanner");
  request.add_options()->set_name("option");
  SetOptionsResponse response = tracker_->SetOptions(request);
  EXPECT_THAT(response.scanner(), EqualsProto(request.scanner()));
  ASSERT_TRUE(response.results().contains("option"));
  EXPECT_EQ(response.results().at("option"), OPERATION_RESULT_MISSING);
  EXPECT_FALSE(response.has_config());
}

TEST_F(DeviceTrackerTest, SetOptionsScannerConfigFails) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  scanner->SetScannerConfig(std::nullopt);

  SetOptionsRequest set_request;
  *set_request.mutable_scanner() = open_response.config().scanner();
  set_request.add_options()->set_name("option");
  SetOptionsResponse set_response = tracker_->SetOptions(set_request);
  EXPECT_THAT(set_response.scanner(), EqualsProto(set_request.scanner()));
  ASSERT_TRUE(set_response.results().contains("option"));
  EXPECT_EQ(set_response.results().at("option"),
            OPERATION_RESULT_INTERNAL_ERROR);
  EXPECT_FALSE(set_response.has_config());
}

TEST_F(DeviceTrackerTest, SetOptionsAllOptionsAttempted) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetOptionStatus("option1", SANE_STATUS_INVAL);
  scanner->SetOptionStatus("option2", SANE_STATUS_GOOD);

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  SetOptionsRequest set_request;
  *set_request.mutable_scanner() = open_response.config().scanner();
  set_request.add_options()->set_name("option1");
  set_request.add_options()->set_name("option2");
  SetOptionsResponse set_response = tracker_->SetOptions(set_request);
  EXPECT_THAT(set_response.scanner(), EqualsProto(set_request.scanner()));
  ASSERT_TRUE(set_response.results().contains("option1"));
  EXPECT_EQ(set_response.results().at("option1"), OPERATION_RESULT_INVALID);
  ASSERT_TRUE(set_response.results().contains("option2"));
  EXPECT_EQ(set_response.results().at("option2"), OPERATION_RESULT_SUCCESS);
  EXPECT_TRUE(set_response.has_config());
  EXPECT_THAT(set_response.config().scanner(),
              EqualsProto(set_request.scanner()));
}

TEST_F(DeviceTrackerTest, GetCurrentConfigMissingHandleFails) {
  GetCurrentConfigRequest request;
  GetCurrentConfigResponse response = tracker_->GetCurrentConfig(request);
  EXPECT_THAT(response.scanner(), EqualsProto(request.scanner()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_INVALID);
  EXPECT_FALSE(response.has_config());
}

TEST_F(DeviceTrackerTest, GetCurrentConfigEmptyHandleFails) {
  GetCurrentConfigRequest request;
  request.mutable_scanner()->set_token("");
  GetCurrentConfigResponse response = tracker_->GetCurrentConfig(request);
  EXPECT_THAT(response.scanner(), EqualsProto(request.scanner()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_INVALID);
  EXPECT_FALSE(response.has_config());
}

TEST_F(DeviceTrackerTest, GetCurrentConfigInvalidHandleFails) {
  GetCurrentConfigRequest request;
  request.mutable_scanner()->set_token("NoSuchScanner");
  GetCurrentConfigResponse response = tracker_->GetCurrentConfig(request);
  EXPECT_THAT(response.scanner(), EqualsProto(request.scanner()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_MISSING);
  EXPECT_FALSE(response.has_config());
}

TEST_F(DeviceTrackerTest, GetCurrentConfigScannerConfigFails) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  scanner->SetScannerConfig(std::nullopt);

  GetCurrentConfigRequest request;
  *request.mutable_scanner() = open_response.config().scanner();
  GetCurrentConfigResponse response = tracker_->GetCurrentConfig(request);
  EXPECT_THAT(response.scanner(), EqualsProto(request.scanner()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_INTERNAL_ERROR);
  EXPECT_FALSE(response.has_config());
}

TEST_F(DeviceTrackerTest, GetCurrentConfig) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  ScannerConfig config;
  *config.mutable_scanner() = open_response.config().scanner();
  ScannerOption option;
  option.set_name("int-option");
  option.set_option_type(OptionType::TYPE_INT);
  option.mutable_int_value()->add_value(24);
  (*config.mutable_options())["name"] = option;
  OptionGroup* group = config.add_option_groups();
  group->set_title("option-1");
  group->add_members("value-1");
  group->add_members("value-2");
  group = config.add_option_groups();
  group->set_title("option-2");
  group->add_members("another-one");

  scanner->SetScannerConfig(config);

  GetCurrentConfigRequest get_request;
  *get_request.mutable_scanner() = open_response.config().scanner();
  GetCurrentConfigResponse get_response =
      tracker_->GetCurrentConfig(get_request);
  EXPECT_THAT(get_response.scanner(), EqualsProto(get_request.scanner()));
  EXPECT_EQ(get_response.result(), OPERATION_RESULT_SUCCESS);
  ASSERT_TRUE(get_response.has_config());
  EXPECT_THAT(get_response.config().scanner(), EqualsProto(config.scanner()));
  auto it = get_response.config().options().find("name");
  ASSERT_TRUE(it != get_response.config().options().end());
  EXPECT_THAT(it->second, EqualsProto(option));
  ASSERT_EQ(get_response.config().option_groups_size(),
            config.option_groups_size());
  for (int i = 0; i < get_response.config().option_groups_size(); ++i) {
    EXPECT_THAT(get_response.config().option_groups(i),
                EqualsProto(config.option_groups(i)));
  }
}

TEST_F(DeviceTrackerTest, StartPreparedScanMissingHandleFails) {
  StartPreparedScanRequest request;
  StartPreparedScanResponse response = tracker_->StartPreparedScan(request);
  EXPECT_THAT(response.scanner(), EqualsProto(request.scanner()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_INVALID);
  EXPECT_FALSE(response.has_job_handle());
}

TEST_F(DeviceTrackerTest, StartPreparedScanEmptyHandleFails) {
  StartPreparedScanRequest request;
  request.mutable_scanner()->set_token("");
  StartPreparedScanResponse response = tracker_->StartPreparedScan(request);
  EXPECT_THAT(response.scanner(), EqualsProto(request.scanner()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_INVALID);
  EXPECT_FALSE(response.has_job_handle());
}

TEST_F(DeviceTrackerTest, StartPreparedScanInvalidHandleFails) {
  StartPreparedScanRequest request;
  request.mutable_scanner()->set_token("NoSuchScanner");
  StartPreparedScanResponse response = tracker_->StartPreparedScan(request);
  EXPECT_THAT(response.scanner(), EqualsProto(request.scanner()));
  EXPECT_EQ(response.result(), OPERATION_RESULT_MISSING);
  EXPECT_FALSE(response.has_job_handle());
}

TEST_F(DeviceTrackerTest, StartPreparedScanMissingImageFormatFails) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  EXPECT_THAT(sps_response.scanner(), EqualsProto(sps_request.scanner()));
  EXPECT_EQ(sps_response.result(), OPERATION_RESULT_INVALID);
  EXPECT_FALSE(sps_response.has_job_handle());
}

TEST_F(DeviceTrackerTest, StartPreparedScanDeviceStartFails) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));
  scanner->SetStartScanResult(SANE_STATUS_JAMMED);

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  sps_request.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  EXPECT_THAT(sps_response.scanner(), EqualsProto(sps_request.scanner()));
  EXPECT_EQ(sps_response.result(), OPERATION_RESULT_ADF_JAMMED);
  EXPECT_FALSE(sps_response.has_job_handle());
}

TEST_F(DeviceTrackerTest, StartPreparedScanDeviceMissingJob) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));
  scanner->SetCallStartJob(false);

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  sps_request.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  EXPECT_THAT(sps_response.scanner(), EqualsProto(sps_request.scanner()));
  EXPECT_EQ(sps_response.result(), OPERATION_RESULT_INTERNAL_ERROR);
  EXPECT_FALSE(sps_response.has_job_handle());
}

TEST_F(DeviceTrackerTest, StartPreparedScanFailsWithoutImageReader) {
  CreateFakeScanner("Test");
  // No parameters set, so image reader creation will fail.

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  sps_request.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  EXPECT_THAT(sps_response.scanner(), EqualsProto(sps_request.scanner()));
  EXPECT_NE(sps_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_FALSE(sps_response.has_job_handle());
}

TEST_F(DeviceTrackerTest, StartPreparedScanCreatesJob) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  sps_request.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  EXPECT_THAT(sps_response.scanner(), EqualsProto(sps_request.scanner()));
  EXPECT_EQ(sps_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_TRUE(sps_response.has_job_handle());
}

// CancelScan with a scan_uuid is supposed to be handled by Manager::CancelScan,
// not DeviceTracker::CancelScan.
TEST_F(DeviceTrackerTest, CancelScanByUuidIsBlocked) {
  CancelScanRequest request;
  request.set_scan_uuid("12345");
  ASSERT_DEATH(tracker_->CancelScan(std::move(request)), "Manager::CancelScan");
}

TEST_F(DeviceTrackerTest, CancelScanRequiresJobHandle) {
  CancelScanRequest request;
  request.mutable_job_handle()->set_token("");
  CancelScanResponse response = tracker_->CancelScan(request);
  EXPECT_FALSE(response.success());
  EXPECT_NE(response.failure_reason(), "");
  EXPECT_EQ(response.result(), OPERATION_RESULT_INVALID);
  EXPECT_THAT(response.job_handle(), EqualsProto(request.job_handle()));
}

TEST_F(DeviceTrackerTest, CancelScanInvalidJobHandle) {
  CancelScanRequest request;
  request.mutable_job_handle()->set_token("bad_handle");
  request.set_scan_uuid("bad_uuid");
  CancelScanResponse response = tracker_->CancelScan(request);
  EXPECT_FALSE(response.success());
  EXPECT_THAT(response.failure_reason(), HasSubstr("bad_handle"));
  EXPECT_THAT(response.failure_reason(), Not(HasSubstr("bad_uuid")));
  EXPECT_EQ(response.result(), OPERATION_RESULT_INVALID);
  EXPECT_THAT(response.job_handle(), EqualsProto(request.job_handle()));
}

TEST_F(DeviceTrackerTest, CancelScanClosedScanner) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  sps_request.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  ASSERT_EQ(sps_response.result(), OPERATION_RESULT_SUCCESS);

  // Close device, leaving a dangling job handle.
  CloseScannerRequest close_request;
  *close_request.mutable_scanner() = open_response.config().scanner();
  CloseScannerResponse close_response = tracker_->CloseScanner(close_request);
  ASSERT_EQ(close_response.result(), OPERATION_RESULT_SUCCESS);

  CancelScanRequest cancel_request;
  *cancel_request.mutable_job_handle() = sps_response.job_handle();
  CancelScanResponse cancel_response = tracker_->CancelScan(cancel_request);
  EXPECT_FALSE(cancel_response.success());
  EXPECT_NE(cancel_response.failure_reason(), "");
  EXPECT_EQ(cancel_response.result(), OPERATION_RESULT_MISSING);
  EXPECT_THAT(cancel_response.job_handle(),
              EqualsProto(cancel_request.job_handle()));

  // Job handle itself is no longer valid.
  cancel_response = tracker_->CancelScan(cancel_request);
  EXPECT_FALSE(cancel_response.success());
  EXPECT_NE(cancel_response.failure_reason(), "");
  EXPECT_EQ(cancel_response.result(), OPERATION_RESULT_INVALID);
  EXPECT_THAT(cancel_response.job_handle(),
              EqualsProto(cancel_request.job_handle()));
}

TEST_F(DeviceTrackerTest, CancelScanCompletedJob) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request1;
  *sps_request1.mutable_scanner() = open_response.config().scanner();
  sps_request1.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response1 =
      tracker_->StartPreparedScan(sps_request1);
  ASSERT_EQ(sps_response1.result(), OPERATION_RESULT_SUCCESS);

  // Simulate finishing the first job by clearing it out.
  scanner->ClearScanJob();

  // Cancelling original job should succeed because no new job has started.
  CancelScanRequest cancel_request;
  *cancel_request.mutable_job_handle() = sps_response1.job_handle();
  CancelScanResponse cancel_response = tracker_->CancelScan(cancel_request);
  EXPECT_TRUE(cancel_response.success());
  EXPECT_EQ(cancel_response.failure_reason(), "");
  EXPECT_EQ(cancel_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(cancel_response.job_handle(),
              EqualsProto(cancel_request.job_handle()));

  // Cancelling a second time should fail.
  cancel_response = tracker_->CancelScan(cancel_request);
  EXPECT_FALSE(cancel_response.success());
  EXPECT_NE(cancel_response.failure_reason(), "");
  EXPECT_EQ(cancel_response.result(), OPERATION_RESULT_INVALID);
  EXPECT_THAT(cancel_response.job_handle(),
              EqualsProto(cancel_request.job_handle()));
}

TEST_F(DeviceTrackerTest, CancelScanNotCurrentJob) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request1;
  *sps_request1.mutable_scanner() = open_response.config().scanner();
  sps_request1.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response1 =
      tracker_->StartPreparedScan(sps_request1);
  ASSERT_EQ(sps_response1.result(), OPERATION_RESULT_SUCCESS);

  // Simulate finishing the first job by clearing it out.
  scanner->ClearScanJob();

  StartPreparedScanRequest sps_request2;
  *sps_request2.mutable_scanner() = open_response.config().scanner();
  sps_request2.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response2 =
      tracker_->StartPreparedScan(sps_request2);
  ASSERT_EQ(sps_response2.result(), OPERATION_RESULT_SUCCESS);

  // Cancelling original job should fail.
  CancelScanRequest cancel_request;
  *cancel_request.mutable_job_handle() = sps_response1.job_handle();
  CancelScanResponse cancel_response = tracker_->CancelScan(cancel_request);
  EXPECT_FALSE(cancel_response.success());
  EXPECT_NE(cancel_response.failure_reason(), "");
  EXPECT_EQ(cancel_response.result(), OPERATION_RESULT_CANCELLED);
  EXPECT_THAT(cancel_response.job_handle(),
              EqualsProto(cancel_request.job_handle()));

  // Cancelling second/current job should still succeed.
  *cancel_request.mutable_job_handle() = sps_response2.job_handle();
  cancel_response = tracker_->CancelScan(cancel_request);
  EXPECT_TRUE(cancel_response.success());
  EXPECT_EQ(cancel_response.failure_reason(), "");
  EXPECT_EQ(cancel_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(cancel_response.job_handle(),
              EqualsProto(cancel_request.job_handle()));
}

TEST_F(DeviceTrackerTest, CancelScanDeviceCancelFails) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));
  scanner->SetCancelScanResult(false);

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  sps_request.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  ASSERT_EQ(sps_response.result(), OPERATION_RESULT_SUCCESS);

  CancelScanRequest cancel_request;
  *cancel_request.mutable_job_handle() = sps_response.job_handle();
  CancelScanResponse cancel_response = tracker_->CancelScan(cancel_request);
  EXPECT_FALSE(cancel_response.success());
  EXPECT_NE(cancel_response.failure_reason(), "");
  EXPECT_EQ(cancel_response.result(), OPERATION_RESULT_INTERNAL_ERROR);
  EXPECT_THAT(cancel_response.job_handle(),
              EqualsProto(cancel_request.job_handle()));
}

TEST_F(DeviceTrackerTest, CancelScanNoErrors) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  sps_request.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  ASSERT_EQ(sps_response.result(), OPERATION_RESULT_SUCCESS);

  CancelScanRequest cancel_request;
  *cancel_request.mutable_job_handle() = sps_response.job_handle();
  CancelScanResponse cancel_response = tracker_->CancelScan(cancel_request);
  EXPECT_TRUE(cancel_response.success());
  EXPECT_EQ(cancel_response.failure_reason(), "");
  EXPECT_EQ(cancel_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(cancel_response.job_handle(),
              EqualsProto(cancel_request.job_handle()));

  // Job handle is no longer valid.
  cancel_response = tracker_->CancelScan(cancel_request);
  EXPECT_FALSE(cancel_response.success());
  EXPECT_NE(cancel_response.failure_reason(), "");
  EXPECT_EQ(cancel_response.result(), OPERATION_RESULT_INVALID);
  EXPECT_THAT(cancel_response.job_handle(),
              EqualsProto(cancel_request.job_handle()));
}

TEST_F(DeviceTrackerTest, ReadScanDataRequiresJobHandle) {
  ReadScanDataRequest request;
  request.mutable_job_handle()->set_token("");
  ReadScanDataResponse response = tracker_->ReadScanData(request);
  EXPECT_EQ(response.result(), OPERATION_RESULT_INVALID);
  EXPECT_THAT(response.job_handle(), EqualsProto(request.job_handle()));
  EXPECT_FALSE(response.has_data());
  EXPECT_FALSE(response.has_estimated_completion());
}

TEST_F(DeviceTrackerTest, ReadScanDataFailsForInvalidJob) {
  ReadScanDataRequest request;
  request.mutable_job_handle()->set_token("no_job");
  ReadScanDataResponse response = tracker_->ReadScanData(request);
  EXPECT_EQ(response.result(), OPERATION_RESULT_INVALID);
  EXPECT_THAT(response.job_handle(), EqualsProto(request.job_handle()));
  EXPECT_FALSE(response.has_data());
  EXPECT_FALSE(response.has_estimated_completion());
}

TEST_F(DeviceTrackerTest, ReadScanDataFailsForClosedScanner) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  sps_request.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  ASSERT_EQ(sps_response.result(), OPERATION_RESULT_SUCCESS);

  // Close device, leaving a dangling job handle.
  CloseScannerRequest close_request;
  *close_request.mutable_scanner() = open_response.config().scanner();
  CloseScannerResponse close_response = tracker_->CloseScanner(close_request);
  ASSERT_EQ(close_response.result(), OPERATION_RESULT_SUCCESS);

  ReadScanDataRequest rsd_request;
  *rsd_request.mutable_job_handle() = sps_response.job_handle();
  ReadScanDataResponse rsd_response = tracker_->ReadScanData(rsd_request);
  EXPECT_EQ(rsd_response.result(), OPERATION_RESULT_MISSING);
  EXPECT_THAT(rsd_response.job_handle(), EqualsProto(rsd_request.job_handle()));
  EXPECT_FALSE(rsd_response.has_data());
  EXPECT_FALSE(rsd_response.has_estimated_completion());

  // Job handle itself is no longer valid.
  rsd_response = tracker_->ReadScanData(rsd_request);
  EXPECT_EQ(rsd_response.result(), OPERATION_RESULT_INVALID);
  EXPECT_THAT(rsd_response.job_handle(), EqualsProto(rsd_request.job_handle()));
  EXPECT_FALSE(rsd_response.has_data());
  EXPECT_FALSE(rsd_response.has_estimated_completion());
}

TEST_F(DeviceTrackerTest, ReadScanDataFailsForBadRead) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));
  scanner->SetReadScanDataResult(SANE_STATUS_NO_DOCS);

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  sps_request.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  ASSERT_EQ(sps_response.result(), OPERATION_RESULT_SUCCESS);

  ReadScanDataRequest rsd_request;
  *rsd_request.mutable_job_handle() = sps_response.job_handle();
  ReadScanDataResponse rsd_response = tracker_->ReadScanData(rsd_request);
  EXPECT_EQ(rsd_response.result(), OPERATION_RESULT_ADF_EMPTY);
  EXPECT_THAT(rsd_response.job_handle(), EqualsProto(rsd_request.job_handle()));
  EXPECT_FALSE(rsd_response.has_data());
  EXPECT_FALSE(rsd_response.has_estimated_completion());
}

TEST_F(DeviceTrackerTest, ReadScanDataSuccess) {
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(100, 100));
  std::vector<uint8_t> page1(3 * 100 * 100, 0);
  scanner->SetScanData({page1});
  scanner->SetMaxReadSize(3 * 100 * 60 + 5);  // 60 lines plus leftover.
  scanner->SetInitialEmptyReads(2);

  OpenScannerRequest open_request;
  open_request.mutable_scanner_id()->set_connection_string("Test");
  open_request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse open_response = tracker_->OpenScanner(open_request);
  ASSERT_EQ(open_response.result(), OPERATION_RESULT_SUCCESS);

  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  sps_request.set_image_format("image/png");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  ASSERT_EQ(sps_response.result(), OPERATION_RESULT_SUCCESS);

  // First request will read nothing, but header data is available.
  ReadScanDataRequest rsd_request;
  *rsd_request.mutable_job_handle() = sps_response.job_handle();
  ReadScanDataResponse rsd_response = tracker_->ReadScanData(rsd_request);
  EXPECT_EQ(rsd_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(rsd_response.job_handle(), EqualsProto(rsd_request.job_handle()));
  EXPECT_EQ(rsd_response.data().length(), 54);  // Magic + header chunks.
  EXPECT_EQ(rsd_response.estimated_completion(), 0);

  // Second request will read nothing.
  rsd_response = tracker_->ReadScanData(rsd_request);
  EXPECT_EQ(rsd_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(rsd_response.job_handle(), EqualsProto(rsd_request.job_handle()));
  EXPECT_EQ(rsd_response.data().size(), 0);
  EXPECT_EQ(rsd_response.estimated_completion(), 0);

  // Next request will read 60 full lines, but the encoder might not write them
  // yet.
  rsd_response = tracker_->ReadScanData(rsd_request);
  EXPECT_EQ(rsd_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(rsd_response.job_handle(), EqualsProto(rsd_request.job_handle()));
  EXPECT_TRUE(rsd_response.has_data());
  EXPECT_EQ(rsd_response.estimated_completion(), 60);

  // Next request will read the remaining data.
  rsd_response = tracker_->ReadScanData(rsd_request);
  EXPECT_EQ(rsd_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_THAT(rsd_response.job_handle(), EqualsProto(rsd_request.job_handle()));
  EXPECT_GT(rsd_response.data().length(), 9);  // IDAT + data.
  EXPECT_EQ(rsd_response.estimated_completion(), 100);

  // Last request will get EOF plus the IEND chunk.
  rsd_response = tracker_->ReadScanData(rsd_request);
  EXPECT_EQ(rsd_response.result(), OPERATION_RESULT_EOF);
  EXPECT_THAT(rsd_response.job_handle(), EqualsProto(rsd_request.job_handle()));
  EXPECT_EQ(rsd_response.data().length(), 12);  // IEND.
  EXPECT_EQ(rsd_response.estimated_completion(), 100);
}

}  // namespace
}  // namespace lorgnette
