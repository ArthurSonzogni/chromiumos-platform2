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
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/files/file_path.h>
#include <brillo/files/file_util.h>

#include "lorgnette/dlc_client_fake.h"
#include "lorgnette/firewall_manager.h"
#include "lorgnette/sane_client_fake.h"
#include "lorgnette/test_util.h"
#include "lorgnette/usb/libusb_wrapper_fake.h"
#include "lorgnette/usb/usb_device.h"
#include "lorgnette/usb/usb_device_fake.h"

using ::testing::_;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedPointwise;

namespace lorgnette {

namespace {

constexpr char kEpson2Name[] = "epson2:net:127.0.0.1";
constexpr char kEpsonDsName[] = "epsonds:libusb:003:002";

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
  std::string ippusb_string;
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

class DlcDeviceTrackerTest : public testing::Test {
 protected:
  std::unique_ptr<DeviceTracker> DlcMinimalDiscoverySetup(
      base::RunLoop* run_loop, bool add_dlc_device = false) {
    std::vector<std::unique_ptr<UsbDevice>> device_list;

    // Scanners that need DLC backend
    VidPid device1 = {0x832, 0x231};
    VidPid device2 = {0x832, 0x342};
    VidPid device3 = {0x432, 0x342};
    dlc_backend_scanners_ = {device1, device2, device3};

    device_desc_.bDeviceClass = LIBUSB_CLASS_PER_INTERFACE;
    device_desc_.bNumConfigurations = 1;
    device_desc_.iManufacturer = 1;
    device_desc_.iProduct = 2;
    usb_printer_->SetStringDescriptors({"", "GoogleTest", "USB Scanner 3000"});

    printer_altsetting_->bInterfaceProtocol = 0;
    printer_interface_->num_altsetting = 1;
    printer_interface_->altsetting = printer_altsetting_.get();

    memset(&descriptor_, 0, sizeof(descriptor_));
    descriptor_.bLength = sizeof(descriptor_);
    descriptor_.bDescriptorType = LIBUSB_DT_CONFIG;
    descriptor_.wTotalLength = sizeof(descriptor_);
    descriptor_.bNumInterfaces = 1;
    descriptor_.interface = interface_.get();

    usb_printer_->SetDlcBackendScanners(&dlc_backend_scanners_);
    usb_printer_->SetDeviceDescriptor(device_desc_);
    usb_printer_->SetConfigDescriptors({descriptor_});
    usb_printer_->SetBusNumber(1);
    usb_printer_->SetDeviceAddress(1);
    usb_printer_->Init();

    // USB printer with no DLC download required (wrong vid pid)
    auto no_dlc_usb_printer = UsbDeviceFake::Clone(*usb_printer_.get());
    no_dlc_usb_printer->MutableConfigDescriptor(0).interface =
        printer_interface_.get();
    no_dlc_usb_printer->SetStringDescriptors(
        {"", "GoogleTest", "USB Printer 1000"});
    no_dlc_usb_printer->MutableDeviceDescriptor().idProduct = 0x987;
    no_dlc_usb_printer->MutableDeviceDescriptor().idVendor = 0x123;

    // USB printer with no DLC download required (wrong vid, right pid)
    auto no_dlc_usb_printer2 = UsbDeviceFake::Clone(*usb_printer_.get());
    no_dlc_usb_printer2->MutableConfigDescriptor(0).interface =
        printer_interface_.get();
    no_dlc_usb_printer2->SetStringDescriptors(
        {"", "GoogleTest", "USB Printer 1500"});
    no_dlc_usb_printer2->MutableDeviceDescriptor().idProduct = 0x231;
    no_dlc_usb_printer2->MutableDeviceDescriptor().idVendor = 0x432;

    // USB printer with DLC required (Correct vid pid)
    auto dlc_usb_printer = UsbDeviceFake::Clone(*usb_printer_.get());
    dlc_usb_printer->MutableConfigDescriptor(0).interface =
        printer_interface_.get();
    dlc_usb_printer->SetStringDescriptors(
        {"", "GoogleTest", "USB Printer 2000"});
    dlc_usb_printer->MutableDeviceDescriptor().idProduct = 0x231;
    dlc_usb_printer->MutableDeviceDescriptor().idVendor = 0x832;

    device_list.emplace_back(std::move(usb_printer_));
    device_list.emplace_back(std::move(no_dlc_usb_printer));
    device_list.emplace_back(std::move(no_dlc_usb_printer2));
    if (add_dlc_device) {
      device_list.emplace_back(std::move(dlc_usb_printer));
    }
    libusb_->SetDevices(std::move(device_list));

    sane_client_->SetListDevicesResult(true);

    auto tracker =
        std::make_unique<DeviceTracker>(sane_client_.get(), libusb_.get());

    // Signal handler that tracks all the events of interest.
    std::set<std::unique_ptr<ScannerInfo>> scanners;
    // Set signal handler
    auto signal_handler =
        base::BindLambdaForTesting([run_loop, &tracker, &scanners](
                                       const ScannerListChangedSignal& signal) {
          if (signal.event_type() == ScannerListChangedSignal::ENUM_COMPLETE) {
            StopScannerDiscoveryRequest stop_request;
            stop_request.set_session_id(signal.session_id());
            tracker->StopScannerDiscovery(stop_request);
          }
          if (signal.event_type() == ScannerListChangedSignal::SESSION_ENDING) {
            run_loop->Quit();
          }
          if (signal.event_type() == ScannerListChangedSignal::SCANNER_ADDED) {
            std::unique_ptr<ScannerInfo> info(signal.scanner().New());
            info->CopyFrom(signal.scanner());
            scanners.insert(std::move(info));
          }
        });
    tracker->SetScannerListChangedSignalSender(signal_handler);

    return tracker;
  }
  std::unique_ptr<LibusbWrapperFake> libusb_ =
      std::make_unique<LibusbWrapperFake>();
  // Scanner that supports eSCL over IPP-USB.
  std::unique_ptr<UsbDeviceFake> usb_printer_ =
      std::make_unique<UsbDeviceFake>();
  // One config descriptor containing the interface.
  libusb_config_descriptor descriptor_;
  std::unique_ptr<libusb_interface_descriptor> printer_altsetting_ =
      MakeIppUsbInterfaceDescriptor();
  std::unique_ptr<libusb_interface> printer_interface_ =
      std::make_unique<libusb_interface>();
  libusb_device_descriptor device_desc_ = MakeMinimalDeviceDescriptor();
  // One interface containing the altsetting.
  std::unique_ptr<libusb_interface> interface_ =
      std::make_unique<libusb_interface>();
  std::unique_ptr<SaneClientFake> sane_client_ =
      std::make_unique<SaneClientFake>();
  std::set<VidPid> dlc_backend_scanners_;

  // Test root path set in dlc_client_fake.cc
  const base::FilePath root_path_ = base::FilePath("/test/path/to/dlc");
};

class MockFirewallManager : public FirewallManager {
 public:
  explicit MockFirewallManager(const std::string& interface)
      : FirewallManager(interface) {}

  MOCK_METHOD(std::vector<PortToken>, RequestPortsForDiscovery, (), (override));
  MOCK_METHOD(PortToken, RequestUdpPortAccess, (uint16_t), (override));
  MOCK_METHOD(void, ReleaseUdpPortAccess, (uint16_t), (override));
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
    device.ippusb_string = base::StringPrintf(
        "ippusb:escl:%s %s:%04x_%04x/eSCL/",
        device.device
            ->GetStringDescriptor(
                device.device->GetDeviceDescriptor()->iManufacturer)
            ->c_str(),
        device.device
            ->GetStringDescriptor(
                device.device->GetDeviceDescriptor()->iProduct)
            ->c_str(),
        device.device->GetVid(), device.device->GetPid());
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

// Policy: Download never
// Will not install, even though DLC requiring devices are found.
TEST_F(DlcDeviceTrackerTest, TestNeverDownloadDlcPolicy) {
  base::test::SingleThreadTaskEnvironment task_environment;
  base::RunLoop run_loop;
  // Add dlc requiring devices
  auto tracker = DlcMinimalDiscoverySetup(&run_loop, true);

  MockFirewallManager firewall_manager(/*interface=*/"test");
  tracker->SetFirewallManager(&firewall_manager);
  EXPECT_CALL(firewall_manager, RequestPortsForDiscovery()).WillRepeatedly([] {
    std::vector<PortToken> retval;
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
    return retval;
  });

  // Set fake DLC client
  auto dlc_client = std::make_unique<DlcClientFake>();
  tracker->SetDlcClient(dlc_client.get());

  StartScannerDiscoveryRequest start_request;

  // DOWNLOAD_NEVER Policy
  start_request.set_client_id("dlc_client");
  start_request.set_download_policy(BackendDownloadPolicy::DOWNLOAD_NEVER);
  StartScannerDiscoveryResponse response1 =
      tracker->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());

  run_loop.Run();
  EXPECT_TRUE(tracker->GetDlcRootPath().empty());
}

// Policy: Download if needed
// Will not install DLC because no DLC requiring devices detected.
TEST_F(DlcDeviceTrackerTest, TestDownloadIfNeededDlcPolicyWithoutDlcDevices) {
  base::test::SingleThreadTaskEnvironment task_environment;
  base::RunLoop run_loop;
  auto tracker = DlcMinimalDiscoverySetup(&run_loop);

  MockFirewallManager firewall_manager(/*interface=*/"test");
  tracker->SetFirewallManager(&firewall_manager);
  EXPECT_CALL(firewall_manager, RequestPortsForDiscovery()).WillRepeatedly([] {
    std::vector<PortToken> retval;
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
    return retval;
  });

  // Set fake DLC client
  auto dlc_client = std::make_unique<DlcClientFake>();
  tracker->SetDlcClient(dlc_client.get());

  StartScannerDiscoveryRequest start_request;

  // DOWNLOAD_IF_NEEDED Policy
  start_request.set_client_id("dlc_client");
  start_request.set_download_policy(BackendDownloadPolicy::DOWNLOAD_IF_NEEDED);
  StartScannerDiscoveryResponse response1 =
      tracker->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());

  run_loop.Run();
  // DLC not installed because no devices needed it.
  EXPECT_TRUE(tracker->GetDlcRootPath().empty());
}

// Policy: Download if needed
// Will install DLC since DLC devices are detected.
TEST_F(DlcDeviceTrackerTest, TestDownloadIfNeededDlcPolicyWithDlcDevices) {
  base::test::SingleThreadTaskEnvironment task_environment;
  base::RunLoop run_loop;
  // Add DLC requiring devices
  auto tracker = DlcMinimalDiscoverySetup(&run_loop, true);

  MockFirewallManager firewall_manager(/*interface=*/"test");
  tracker->SetFirewallManager(&firewall_manager);
  EXPECT_CALL(firewall_manager, RequestPortsForDiscovery()).WillRepeatedly([] {
    std::vector<PortToken> retval;
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
    return retval;
  });

  // Set fake DLC client
  auto dlc_client = std::make_unique<DlcClientFake>();
  tracker->SetDlcClient(dlc_client.get());

  StartScannerDiscoveryRequest start_request;

  // DOWNLOAD_IF_NEEDED Policy
  start_request.set_client_id("dlc_client");
  start_request.set_download_policy(BackendDownloadPolicy::DOWNLOAD_IF_NEEDED);
  StartScannerDiscoveryResponse response1 =
      tracker->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());

  run_loop.Run();
  // DLC installed because 1 device needed it.
  EXPECT_FALSE(tracker->GetDlcRootPath().empty());
  EXPECT_EQ(tracker->GetDlcRootPath(), root_path_);
}

// Policy: Always download
// Should install DLC even if DLC requiring devices aren't present/detected.
TEST_F(DlcDeviceTrackerTest, TestAlwaysDownloadDlcPolicy) {
  base::test::SingleThreadTaskEnvironment task_environment;
  base::RunLoop run_loop;
  // DLC devices shouldn't be required to prompt download in this policy
  auto tracker = DlcMinimalDiscoverySetup(&run_loop);

  MockFirewallManager firewall_manager(/*interface=*/"test");
  tracker->SetFirewallManager(&firewall_manager);
  EXPECT_CALL(firewall_manager, RequestPortsForDiscovery()).WillRepeatedly([] {
    std::vector<PortToken> retval;
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
    return retval;
  });

  // Set fake DLC client
  auto dlc_client = std::make_unique<DlcClientFake>();
  tracker->SetDlcClient(dlc_client.get());

  StartScannerDiscoveryRequest start_request;

  // DOWNLOAD_ALWAYS Policy
  start_request.set_client_id("dlc_client");
  start_request.set_download_policy(BackendDownloadPolicy::DOWNLOAD_ALWAYS);
  StartScannerDiscoveryResponse response1 =
      tracker->StartScannerDiscovery(start_request);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());

  run_loop.Run();
  EXPECT_FALSE(tracker->GetDlcRootPath().empty());
  EXPECT_EQ(tracker->GetDlcRootPath(), root_path_);
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

  sane_client_->SetListDevicesResult(true);
  // Duplicates of eSCL over ippusb that are filtered out.
  std::string dup_by_vidpid =
      base::StringPrintf("pixma:04A94321_%s",
                         ippusb_escl_device.device->GetSerialNumber().c_str());
  CreateFakeScanner(dup_by_vidpid, "GoogleTest", "eSCL Scanner 3001", "eSCL");
  std::string dup_by_busdev = base::StringPrintf(
      "epson2:libusb:%03d:%03d", ippusb_escl_device.device->GetBusNumber(),
      ippusb_escl_device.device->GetDeviceAddress());
  CreateFakeScanner(dup_by_busdev, "GoogleTest", "eSCL Scanner 3002", "eSCL");

  // Unique USB device without ippusb support that is added during SANE probing.
  CreateFakeScanner(kEpsonDsName, "GoogleTest", "SANE Scanner 4000", "USB");

  // Unique non-eSCL network device that is added during SANE probing.
  CreateFakeScanner(kEpson2Name, "GoogleTest", "GoogleTest SANE NetScan 4200",
                    "Network");

  std::vector<std::unique_ptr<UsbDevice>> device_list;
  device_list.emplace_back(std::move(non_printer));
  device_list.emplace_back(std::move(ippusb_escl_device.device));
  device_list.emplace_back(std::move(ippusb_printer.device));
  device_list.emplace_back(std::move(usb_printer.device));
  libusb_->SetDevices(std::move(device_list));

  EXPECT_CALL(*firewall_manager_, RequestPortsForDiscovery()).WillOnce([] {
    std::vector<PortToken> retval;
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
    return retval;
  });

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
  escl3000.set_protocol_type("Mopria");
  escl3000.set_secure(true);
  ScannerInfo sane4000;
  sane4000.set_manufacturer("GoogleTest");
  sane4000.set_model("SANE Scanner 4000");
  sane4000.set_display_name("GoogleTest SANE Scanner 4000 (USB)");
  sane4000.set_connection_type(lorgnette::CONNECTION_USB);
  sane4000.set_secure(true);
  sane4000.set_protocol_type("epsonds");
  ScannerInfo sane4200;
  sane4200.set_manufacturer("GoogleTest");
  sane4200.set_model("GoogleTest SANE NetScan 4200");
  sane4200.set_display_name("GoogleTest SANE NetScan 4200");
  sane4200.set_connection_type(lorgnette::CONNECTION_NETWORK);
  sane4200.set_secure(false);
  sane4200.set_protocol_type("epson2");

  EXPECT_THAT(closed_sessions_, ElementsAre(response.session_id()));
  EXPECT_THAT(discovered_scanners_[response.session_id()],
              UnorderedElementsAre(MatchesScannerInfo(escl3000),
                                   MatchesScannerInfo(sane4000),
                                   MatchesScannerInfo(sane4200)));
}

TEST_F(DeviceTrackerTest, DiscoverySessionCachingUnpluggedDeviceRemoved) {
  sane_client_->SetListDevicesResult(true);
  CreateFakeScanner("epson2:libusb:001:001", "GoogleTest", "Scanner 1", "USB");

  EXPECT_CALL(*firewall_manager_, RequestPortsForDiscovery())
      .WillRepeatedly([] {
        std::vector<PortToken> retval;
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
        return retval;
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

  EXPECT_CALL(*firewall_manager_, RequestPortsForDiscovery())
      .WillRepeatedly([] {
        std::vector<PortToken> retval;
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
        return retval;
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

  EXPECT_CALL(*firewall_manager_, RequestPortsForDiscovery())
      .WillRepeatedly([] {
        std::vector<PortToken> retval;
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
        return retval;
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
  SaneDeviceFake* scanner = CreateFakeScanner("epson2:libusb:001:001",
                                              "GoogleTest", "Scanner 1", "USB");
  scanner->SetScanParameters(MakeScanParameters(10, 10));

  EXPECT_CALL(*firewall_manager_, RequestPortsForDiscovery())
      .WillRepeatedly([] {
        std::vector<PortToken> retval;
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
        return retval;
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

  // Start a scan.  This job should get closed when the second discovery session
  // invalidates the handle.
  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = open_response.config().scanner();
  sps_request.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  EXPECT_THAT(sps_response.scanner(), EqualsProto(sps_request.scanner()));
  EXPECT_EQ(sps_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_TRUE(sps_response.has_job_handle());

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

  // Job is no longer valid.
  CancelScanRequest cancel_request;
  *cancel_request.mutable_job_handle() = sps_response.job_handle();
  CancelScanResponse cancel_response = tracker_->CancelScan(cancel_request);
  EXPECT_FALSE(cancel_response.success());
  EXPECT_NE(cancel_response.failure_reason(), "");
  EXPECT_EQ(cancel_response.result(), OPERATION_RESULT_INVALID);
  EXPECT_THAT(cancel_response.job_handle(),
              EqualsProto(cancel_request.job_handle()));
}

TEST_F(DeviceTrackerTest, DiscoverySessionLocalDevices) {
  sane_client_->SetListDevicesResult(true);

  // Unique USB device without ippusb support that is added during SANE probing.
  CreateFakeScanner(kEpsonDsName, "GoogleTest", "SANE Scanner 4000", "USB");

  // Unique non-eSCL network device that is added during SANE probing.
  CreateFakeScanner(kEpson2Name, "GoogleTest", "GoogleTest SANE NetScan 4200",
                    "Network");

  EXPECT_CALL(*firewall_manager_, RequestPortsForDiscovery())
      .WillRepeatedly([] {
        std::vector<PortToken> retval;
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
        return retval;
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
  usb_device.set_protocol_type("epsonds");
  usb_device.set_secure(true);
  ScannerInfo net_device;
  net_device.set_manufacturer("GoogleTest");
  net_device.set_model("GoogleTest SANE NetScan 4200");
  net_device.set_display_name("GoogleTest SANE NetScan 4200");
  net_device.set_connection_type(lorgnette::CONNECTION_NETWORK);
  net_device.set_protocol_type("epson2");
  net_device.set_secure(false);

  EXPECT_THAT(discovered_scanners_[full_response.session_id()],
              UnorderedElementsAre(MatchesScannerInfo(usb_device),
                                   MatchesScannerInfo(net_device)));
  EXPECT_THAT(discovered_scanners_[local_response.session_id()],
              ElementsAre(MatchesScannerInfo(usb_device)));
}

TEST_F(DeviceTrackerTest, DiscoverySessionEpsonBackend) {
  sane_client_->SetListDevicesResult(true);

  // Test the case where a device responds to the epson2 backend during SANE
  // discovery but the device really uses the epsonds backend.
  sane_client_->AddDeviceListing("epson2:net:127.0.0.1", "GoogleTest",
                                 "GoogleTest SANE NetScan 4200", "Network");
  sane_client_->SetDeviceForName("epsonds:net:127.0.0.1",
                                 std::make_unique<SaneDeviceFake>());

  EXPECT_CALL(*firewall_manager_, RequestPortsForDiscovery()).WillOnce([] {
    std::vector<PortToken> retval;
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
    retval.emplace_back(PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
    return retval;
  });

  StartScannerDiscoveryRequest request;
  request.set_client_id("discovery");
  StartScannerDiscoveryResponse response =
      tracker_->StartScannerDiscovery(request);
  EXPECT_TRUE(response.started());
  EXPECT_FALSE(response.session_id().empty());
  open_sessions_.insert(response.session_id());

  run_loop_.Run();

  ScannerInfo scanner;
  // The epsonds should be the name of the device found, not epson2.
  scanner.set_name("epsonds:net:127.0.0.1");
  scanner.set_manufacturer("GoogleTest");
  scanner.set_model("GoogleTest SANE NetScan 4200");
  scanner.set_display_name("GoogleTest SANE NetScan 4200");
  scanner.set_connection_type(lorgnette::CONNECTION_NETWORK);
  scanner.set_protocol_type("epsonds");
  scanner.set_secure(false);

  EXPECT_THAT(discovered_scanners_[response.session_id()],
              UnorderedElementsAre(MatchesScannerInfo(scanner)));
}

// Test that two discovery sessions finding the same devices return the same
// device IDs.
TEST_F(DeviceTrackerTest, DiscoverySessionStableDeviceIds) {
  auto ippusb_device1 = MakeIPPUSBDevice("Scanner 1");
  ippusb_device1.device->MutableDeviceDescriptor().idProduct = 0x1234;
  ippusb_device1.device->Init();
  CreateIPPUSBSocket(ippusb_device1);
  CreateFakeScanner(ippusb_device1.connection_string);

  auto ippusb_device2 = MakeIPPUSBDevice("Scanner 2");
  ippusb_device2.device->MutableDeviceDescriptor().idProduct = 0x2345;
  ippusb_device2.device->Init();
  CreateIPPUSBSocket(ippusb_device2);
  CreateFakeScanner(ippusb_device2.connection_string);

  std::vector<std::unique_ptr<UsbDevice>> device_list;
  device_list.emplace_back(std::move(ippusb_device1.device));
  device_list.emplace_back(std::move(ippusb_device2.device));
  libusb_->SetDevices(std::move(device_list));
  sane_client_->SetListDevicesResult(true);

  // Unique device by VID:PID.
  CreateFakeScanner("pixma:04A91234_5555", "GoogleTest",
                    "Unique VIDPID Scanner", "USB");

  // Unique device by BUS:DEV.
  CreateFakeScanner("epson2:libusb:002:003", "GoogleTest",
                    "Unique BUSDEV Scanner", "USB");

  EXPECT_CALL(*firewall_manager_, RequestPortsForDiscovery())
      .WillRepeatedly([] {
        std::vector<PortToken> retval;
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
        return retval;
      });

  // First session finds the scanners.  They should have different deviceUuid
  // values.
  base::RunLoop run_loop1;
  SetQuitClosure(run_loop1.QuitClosure());
  StartScannerDiscoveryRequest start_request1;
  start_request1.set_client_id("DiscoverySessionStableDevicesIds");
  StartScannerDiscoveryResponse response1 =
      tracker_->StartScannerDiscovery(start_request1);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());
  open_sessions_.insert(response1.session_id());
  run_loop1.Run();
  EXPECT_EQ(discovered_scanners_[response1.session_id()].size(), 4);
  std::map<std::string, ScannerInfo*>
      device_ids;  // Device ID to original info.
  for (const auto& scanner : discovered_scanners_[response1.session_id()]) {
    EXPECT_FALSE(scanner->device_uuid().empty());
    EXPECT_FALSE(device_ids.contains(scanner->device_uuid()));
    // It's safe to store these raw pointers because the original unique_ptr in
    // discovered_scanners_ won't be deleted until after the test ends.
    device_ids.emplace(scanner->device_uuid(), scanner.get());
  }

  // Second session for the same client finds the same scanners with the
  // same deviceUuid values.
  base::RunLoop run_loop2;
  SetQuitClosure(run_loop2.QuitClosure());
  StartScannerDiscoveryRequest start_request2;
  start_request2.set_client_id("DiscoverySessionStableDevicesIds");
  StartScannerDiscoveryResponse response2 =
      tracker_->StartScannerDiscovery(start_request2);
  EXPECT_TRUE(response2.started());
  EXPECT_FALSE(response2.session_id().empty());
  open_sessions_.insert(response2.session_id());
  run_loop2.Run();
  EXPECT_EQ(discovered_scanners_[response2.session_id()].size(), 4);
  // Every returned device_uuid should match one of the previously returned
  // devices.
  for (const auto& scanner : discovered_scanners_[response2.session_id()]) {
    EXPECT_FALSE(scanner->device_uuid().empty());
    ASSERT_TRUE(device_ids.contains(scanner->device_uuid()));
    EXPECT_THAT(scanner,
                MatchesScannerInfo(*device_ids.at(scanner->device_uuid())));
  }

  // Session for a different client finds the same scanners with the same
  // deviceUuid values.
  base::RunLoop run_loop3;
  SetQuitClosure(run_loop3.QuitClosure());
  StartScannerDiscoveryRequest start_request3;
  start_request3.set_client_id("DiscoverySessionStableDevicesIds2");
  StartScannerDiscoveryResponse response3 =
      tracker_->StartScannerDiscovery(start_request3);
  EXPECT_TRUE(response3.started());
  EXPECT_FALSE(response3.session_id().empty());
  open_sessions_.insert(response3.session_id());
  run_loop3.Run();
  EXPECT_EQ(discovered_scanners_[response3.session_id()].size(), 4);
  // Every returned device_uuid should match one of the previously returned
  // devices.
  for (const auto& scanner : discovered_scanners_[response3.session_id()]) {
    EXPECT_FALSE(scanner->device_uuid().empty());
    ASSERT_TRUE(device_ids.contains(scanner->device_uuid()));
    EXPECT_THAT(scanner,
                MatchesScannerInfo(*device_ids.at(scanner->device_uuid())));
  }
}

TEST_F(DeviceTrackerTest, DiscoverySessionCanonicalDeviceIds) {
  auto ippusb_device1 = MakeIPPUSBDevice("Scanner 1");
  ippusb_device1.device->MutableDeviceDescriptor().idProduct = 0x1234;
  ippusb_device1.device->Init();
  CreateIPPUSBSocket(ippusb_device1);
  CreateFakeScanner(ippusb_device1.connection_string);

  auto ippusb_device2 = MakeIPPUSBDevice("Scanner 2");
  ippusb_device2.device->MutableDeviceDescriptor().idProduct = 0x2345;
  ippusb_device2.device->Init();
  CreateIPPUSBSocket(ippusb_device2);
  CreateFakeScanner(ippusb_device2.connection_string);

  // Duplicate of device 1 by VID:PID.  Should have the same ID.
  std::string dup_by_vidpid =
      base::StringPrintf("pixma:%04X%04X_%s", ippusb_device1.device->GetVid(),
                         ippusb_device1.device->GetPid(),
                         ippusb_device1.device->GetSerialNumber().c_str());
  CreateFakeScanner(dup_by_vidpid, "GoogleTest", "eSCL Scanner 3001", "eSCL");

  // Duplicate of device 1 by BUS:DEV.  Should have the same ID.
  std::string dup_by_busdev = base::StringPrintf(
      "epson2:libusb:%03d:%03d", ippusb_device1.device->GetBusNumber(),
      ippusb_device1.device->GetDeviceAddress());
  CreateFakeScanner(dup_by_busdev, "GoogleTest", "eSCL Scanner 3002", "eSCL");

  // Unique device by VID:PID.
  CreateFakeScanner("pixma:04A91234_5555", "GoogleTest",
                    "Unique VIDPID Scanner", "USB");

  // Unique device by BUS:DEV.
  CreateFakeScanner("epson2:libusb:002:003", "GoogleTest",
                    "Unique BUSDEV Scanner", "USB");

  std::vector<std::unique_ptr<UsbDevice>> device_list;
  device_list.emplace_back(std::move(ippusb_device1.device));
  device_list.emplace_back(std::move(ippusb_device2.device));
  libusb_->SetDevices(std::move(device_list));
  sane_client_->SetListDevicesResult(true);
  tracker_->SetCacheDirectoryForTesting(socket_dir_.GetPath());
  base::FilePath cache_file = socket_dir_.GetPath().Append("known_devices");
  brillo::DeleteFile(cache_file);  // Start off with no saved devices.

  EXPECT_CALL(*firewall_manager_, RequestPortsForDiscovery())
      .WillRepeatedly([] {
        std::vector<PortToken> retval;
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/8612));
        retval.emplace_back(
            PortToken(/*firewall_manager=*/nullptr, /*port=*/1865));
        return retval;
      });

  // Session finds all the scanners.  Three of them should have the same
  // device_uuid as device1 and the others should be unique.
  base::RunLoop run_loop1;
  SetQuitClosure(run_loop1.QuitClosure());
  StartScannerDiscoveryRequest start_request1;
  start_request1.set_client_id("DiscoverySessionStableDevicesIds");
  StartScannerDiscoveryResponse response1 =
      tracker_->StartScannerDiscovery(start_request1);
  EXPECT_TRUE(response1.started());
  EXPECT_FALSE(response1.session_id().empty());
  open_sessions_.insert(response1.session_id());
  run_loop1.Run();
  std::map<std::string, std::set<std::string>> device_ids1;
  std::string ippusb_device_id;
  for (const auto& scanner : discovered_scanners_[response1.session_id()]) {
    EXPECT_FALSE(scanner->device_uuid().empty());
    device_ids1[scanner->device_uuid()].insert(scanner->name());
    if (scanner->name() == ippusb_device1.ippusb_string) {
      ippusb_device_id = scanner->device_uuid();
    }
  }
  EXPECT_EQ(device_ids1.size(), 4);
  EXPECT_THAT(device_ids1[ippusb_device_id],
              UnorderedElementsAre(ippusb_device1.ippusb_string, dup_by_vidpid,
                                   dup_by_busdev));

  // Saved devices file is non-empty after session.
  int64_t file_size;
  EXPECT_TRUE(base::GetFileSize(cache_file, &file_size));
  EXPECT_GT(file_size, 0);

  // Clear saved devices to simulate a lorgnette shutdown.  Then the second
  // session should produce the same set of devices and IDs because it reloads
  // from the cache.
  tracker_->ClearKnownDevicesForTesting();
  base::RunLoop run_loop2;
  SetQuitClosure(run_loop2.QuitClosure());
  StartScannerDiscoveryRequest start_request2;
  start_request2.set_client_id("DiscoverySessionStableDevicesIds");
  StartScannerDiscoveryResponse response2 =
      tracker_->StartScannerDiscovery(start_request2);
  EXPECT_TRUE(response2.started());
  EXPECT_FALSE(response2.session_id().empty());
  open_sessions_.insert(response2.session_id());
  run_loop2.Run();
  std::map<std::string, std::set<std::string>> device_ids2;
  for (const auto& scanner : discovered_scanners_[response2.session_id()]) {
    EXPECT_FALSE(scanner->device_uuid().empty());
    device_ids2[scanner->device_uuid()].insert(scanner->name());
    if (scanner->name() == ippusb_device1.ippusb_string) {
      EXPECT_EQ(scanner->device_uuid(), ippusb_device_id);
    }
  }
  EXPECT_EQ(device_ids2.size(), 4);
  EXPECT_EQ(device_ids2[ippusb_device_id].size(), 3);
  for (const auto& [id, names] : device_ids2) {
    EXPECT_THAT(device_ids1[id], UnorderedPointwise(Eq(), names));
  }

  // Clear saved devices to simulate a lorgnette shutdown and remove the cache.
  // Then the third session should produce a set of results with the same
  // structure as the first session, but new IDs.
  tracker_->ClearKnownDevicesForTesting();
  brillo::DeleteFile(cache_file);
  base::RunLoop run_loop3;
  SetQuitClosure(run_loop3.QuitClosure());
  StartScannerDiscoveryRequest start_request3;
  start_request3.set_client_id("DiscoverySessionStableDevicesIds");
  StartScannerDiscoveryResponse response3 =
      tracker_->StartScannerDiscovery(start_request3);
  EXPECT_TRUE(response3.started());
  EXPECT_FALSE(response3.session_id().empty());
  open_sessions_.insert(response3.session_id());
  run_loop3.Run();
  std::map<std::string, std::set<std::string>> device_ids3;
  for (const auto& scanner : discovered_scanners_[response3.session_id()]) {
    EXPECT_FALSE(scanner->device_uuid().empty());
    device_ids3[scanner->device_uuid()].insert(scanner->name());
    if (scanner->name() == ippusb_device1.ippusb_string) {
      ippusb_device_id = scanner->device_uuid();
    }
  }
  EXPECT_EQ(device_ids3.size(), 4);
  EXPECT_THAT(device_ids3[ippusb_device_id],
              UnorderedElementsAre(ippusb_device1.ippusb_string, dup_by_vidpid,
                                   dup_by_busdev));
  for (const auto& [id, names] : device_ids3) {
    EXPECT_FALSE(base::Contains(device_ids1, id));
  }
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
  SaneDeviceFake* scanner = CreateFakeScanner("Test");
  scanner->SetScanParameters(MakeScanParameters(10, 10));

  OpenScannerRequest request;
  request.mutable_scanner_id()->set_connection_string("Test");
  request.set_client_id("DeviceTrackerTest");
  OpenScannerResponse response1 = tracker_->OpenScanner(request);

  // Start a scan on the first handle to check that the job gets closed when the
  // scanner gets opened a second time.
  StartPreparedScanRequest sps_request;
  *sps_request.mutable_scanner() = response1.config().scanner();
  sps_request.set_image_format("image/jpeg");
  StartPreparedScanResponse sps_response =
      tracker_->StartPreparedScan(sps_request);
  EXPECT_THAT(sps_response.scanner(), EqualsProto(sps_request.scanner()));
  EXPECT_EQ(sps_response.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_TRUE(sps_response.has_job_handle());

  OpenScannerResponse response2 = tracker_->OpenScanner(request);

  // Cancelling this job should fail since it was implicitly cancelled in the
  // second open request.
  CancelScanRequest cancel_request;
  *cancel_request.mutable_job_handle() = sps_response.job_handle();
  CancelScanResponse cancel_response = tracker_->CancelScan(cancel_request);
  EXPECT_FALSE(cancel_response.success());
  EXPECT_NE(cancel_response.failure_reason(), "");
  EXPECT_EQ(cancel_response.result(), OPERATION_RESULT_INVALID);
  EXPECT_THAT(cancel_response.job_handle(),
              EqualsProto(cancel_request.job_handle()));

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

TEST_F(DeviceTrackerTest, OpenScannerThatRequresFirewall) {
  const std::string connection_string = "pixma:MF2600_1.2.3.4";
  CreateFakeScanner(connection_string, "GoogleTest", "Unique VIDPID Scanner",
                    "USB");

  // Since this is a pixma scanner, the port should get requested when the
  // scanner is opened.
  EXPECT_CALL(*firewall_manager_, RequestUdpPortAccess(8612))
      .WillOnce(testing::Return(
          PortToken(firewall_manager_->GetWeakPtrForTesting(), /*port=*/8612)));

  OpenScannerRequest request1;
  request1.mutable_scanner_id()->set_connection_string(connection_string);
  request1.set_client_id("DeviceTrackerTest");
  OpenScannerResponse response1 = tracker_->OpenScanner(request1);

  EXPECT_EQ(tracker_->NumOpenScanners(), 1);

  // The request to close the port should happen when the scanner is closed.
  EXPECT_CALL(*firewall_manager_, ReleaseUdpPortAccess(8612))
      .WillOnce(testing::Return());

  CloseScannerRequest request2;
  *request2.mutable_scanner() = response1.config().scanner();
  CloseScannerResponse response2 = tracker_->CloseScanner(request2);

  EXPECT_THAT(request2.scanner(), EqualsProto(response2.scanner()));
  EXPECT_EQ(response2.result(), OPERATION_RESULT_SUCCESS);
  EXPECT_EQ(tracker_->NumOpenScanners(), 0);
}

TEST_F(DeviceTrackerTest, OpenScannerThatRequresFirewallFails) {
  // Test the case where the scanner to be opened requires an open firewall
  // port, but connecting to the scanner fails.  Make sure the port gets
  // released even though CloseScanner is not called.
  const std::string connection_string = "pixma:MF2600_1.2.3.4";

  EXPECT_CALL(*firewall_manager_, RequestUdpPortAccess(8612))
      .WillOnce(testing::Return(
          PortToken(firewall_manager_->GetWeakPtrForTesting(), /*port=*/8612)));

  EXPECT_CALL(*firewall_manager_, ReleaseUdpPortAccess(8612))
      .WillOnce(testing::Return());

  OpenScannerRequest request1;
  request1.mutable_scanner_id()->set_connection_string(connection_string);
  request1.set_client_id("DeviceTrackerTest");
  OpenScannerResponse response1 = tracker_->OpenScanner(request1);

  EXPECT_EQ(tracker_->NumOpenScanners(), 0);
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

TEST_F(DeviceTrackerTest, CloseScannerCancelsJob) {
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
  EXPECT_TRUE(sps_response.has_job_handle());

  // Close device, which should also cancel the job.
  CloseScannerRequest close_request;
  *close_request.mutable_scanner() = open_response.config().scanner();
  CloseScannerResponse close_response = tracker_->CloseScanner(close_request);
  ASSERT_EQ(close_response.result(), OPERATION_RESULT_SUCCESS);

  CancelScanRequest cancel_request;
  *cancel_request.mutable_job_handle() = sps_response.job_handle();
  CancelScanResponse cancel_response = tracker_->CancelScan(cancel_request);
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

  // Close device, which cancels the job.
  CloseScannerRequest close_request;
  *close_request.mutable_scanner() = open_response.config().scanner();
  CloseScannerResponse close_response = tracker_->CloseScanner(close_request);
  ASSERT_EQ(close_response.result(), OPERATION_RESULT_SUCCESS);

  ReadScanDataRequest rsd_request;
  *rsd_request.mutable_job_handle() = sps_response.job_handle();
  ReadScanDataResponse rsd_response = tracker_->ReadScanData(rsd_request);
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
