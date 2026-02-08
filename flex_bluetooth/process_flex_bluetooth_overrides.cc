// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/ioctl.h>
#include <sys/socket.h>

#include <set>

#include <base/logging.h>
#include <base/strings/string_util.h>
#include <brillo/syslog_logging.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_device.h>
#include <brillo/udev/udev_enumerate.h>

#include "flex_bluetooth/flex_bluetooth_overrides.h"

enum class ApplyResult {
  kApplied,
  kNotApplied,
  kBlocked,
};

namespace {

// The below defines and structs are copied from Linux's
// net/bluetooth/hci_sock.h and net/bluetooth/bluetooth.h
#define BTPROTO_HCI 1
#define HCIGETDEVLIST _IOR('H', 210, int)
struct hci_dev_req {
  uint16_t dev_id;
  uint32_t dev_opt;
};
struct hci_dev_list_req {
  uint16_t dev_num;
  struct hci_dev_req dev_req[1];  // we only want the first device
};

const char kAttributeDeviceClass[] = "bDeviceClass";
const char kAttributeDeviceSubClass[] = "bDeviceSubClass";
const char kAttributeInterfaceClass[] = "bInterfaceClass";
const char kAttributeInterfaceSubClass[] = "bInterfaceSubClass";
const char kAttributeIdProduct[] = "idProduct";
const char kAttributeIdVendor[] = "idVendor";
// The below DeviceClass and DeviceSubClass can be found at
// https://www.usb.org/defined-class-codes
const char kBluetoothDeviceClass[] = "e0";
const char kBluetoothDeviceSubClass[] = "01";
// On older computers it takes some time for the USB devices to get enumerated.
// These variables control how often to re-read the udevs
// Increase to support slower devices
// Don't increase too much as this process blocks the BT stack from starting
const unsigned int number_of_tries = 10;
const unsigned int seconds_between_retries = 5;

const base::FilePath kSyspropOverridePath = base::FilePath(
    "/var/lib/bluetooth/sysprops.conf.d/floss_reven_overrides.conf");

const std::set<flex_bluetooth::BluetoothAdapter> kAdapterBlocklist = {
    // b/475945265: failed in SET_EVENT_MASK and WRITE_LE_HOST_SUPPORT
    {0x13d3, 0x3331},
    {0x1690, 0x0741},

    // b/475945265: failed in WRITE_LE_HOST_SUPPORT
    {0x0b05, 0x179c},
    {0x0cf3, 0x3005},

    // b/475945265: failed in READ_DEFAULT_ERRONEOUS_DATA_REPORTING
    // even though claimed in READ_LOCAL_SUPPORTED_COMMANDS
    {0x10d7, 0xb012},

    // b/482743750: LE Rand return non-zero status
    {0x03f0, 0x231d},
    {0x044e, 0x3017},
    {0x0489, 0xe00d},
    {0x0489, 0xe00f},
    {0x0489, 0xe010},
    {0x0489, 0xe011},
    {0x05ac, 0x820f},
    {0x05ac, 0x8213},
    {0x05ac, 0x8215},
    {0x05ac, 0x8217},
    {0x05ac, 0x821a},
    {0x05ac, 0x821b},
    {0x0930, 0x020f},
    {0x0a5c, 0x2145},
    {0x0a5c, 0x217f},
    {0x0a5c, 0x219c},
    {0x0a5c, 0x21b4},
    {0x0a5c, 0x21bc},
    {0x0b05, 0x1751},
    {0x10ab, 0x0816},
    {0x18e8, 0x6252},
    {0x413c, 0x8156},
    {0x413c, 0x8160},
    {0x413c, 0x8187},
};

const std::map<flex_bluetooth::BluetoothAdapter,
               std::unordered_set<flex_bluetooth::SyspropOverride>>
    kAdapterSyspropOverrides = {
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe0a2},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x04ca, 0x3015},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe007},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe009},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe300},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe500},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3491},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3519},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3496},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3501},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x8086, 0x0189},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0a12, 0x0001},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0x3004},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x07da},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0a2a},
         {flex_bluetooth::SyspropOverride::kDisableEnhancedSCOConnection}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0a2b},
         {flex_bluetooth::SyspropOverride::kDisableEnhancedSCOConnection}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0aa7},
         {flex_bluetooth::SyspropOverride::kDisableEnhancedSCOConnection}},

        // Enable MSFT AdvMon quirk on RTL8852BE.
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3570},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3571},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3572},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3591},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe123},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe125},
         {flex_bluetooth::SyspropOverride::kEnableLEAdvMonRTLQuirk}},

        // Disable packet boundary & sniff mode opcode for qca chips
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0x311e},
         {flex_bluetooth::SyspropOverride::kDisableSniffMode}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe04e},
         {flex_bluetooth::SyspropOverride::kDisableSniffMode}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0x311e},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0xe04e},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0x817b},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe04e},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x04c5, 0x1330},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x0cf3, 0x817b},
         {flex_bluetooth::SyspropOverride::kDisableSniffMode}},
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe04e},
         {flex_bluetooth::SyspropOverride::kDisableSniffMode}},
        {flex_bluetooth::BluetoothAdapter{0x04c5, 0x1330},
         {flex_bluetooth::SyspropOverride::kDisableSniffMode}},

        // Disable packet boundary for Intel AC7265 chips
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0a2a},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0a2b},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x0aa7},
         {flex_bluetooth::SyspropOverride::kDisablePacketBoundary}},

        // Resolve crashes from b/408887245
        {flex_bluetooth::BluetoothAdapter{0x04ca, 0x3016},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3496},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3501},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x13d3, 0x3503},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x8087, 0x07dc},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x3641, 0x0902},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0489, 0xe09f},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x0a5c, 0x216d},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},

        // Resolve crashes from b/401624875
        {flex_bluetooth::BluetoothAdapter{0x413c, 0x8140},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x044e, 0x301d},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
        {flex_bluetooth::BluetoothAdapter{0x05ac, 0x8205},
         {flex_bluetooth::SyspropOverride::kDisableLEGetVendorCapabilities}},
};
}  // namespace

ApplyResult check_and_apply_overrides(
    const flex_bluetooth::FlexBluetoothOverrides& bt,
    const std::vector<std::unique_ptr<brillo::UdevDevice>>& devices) {
  for (const auto& device : devices) {
    if (!device) {
      LOG(INFO) << "Device not found.";
      continue;
    }
    const std::string vendor =
        device->GetSysAttributeValue(kAttributeIdVendor) ?: "";
    const std::string product =
        device->GetSysAttributeValue(kAttributeIdProduct) ?: "";

    LOG(INFO) << "Found Bluetooth adapter with idVendor: " << vendor
              << " and idProduct: " << product;

    uint16_t id_vendor;
    if (!flex_bluetooth::HexStringToUInt16(vendor, &id_vendor)) {
      LOG(WARNING) << "Unable to convert vendor " << vendor << " to uint16_t.";
      continue;
    }

    uint16_t id_product;
    if (!flex_bluetooth::HexStringToUInt16(product, &id_product)) {
      LOG(WARNING) << "Unable to convert product " << product
                   << " to uint16_t.";
      continue;
    }

    if (kAdapterBlocklist.count({id_vendor, id_product})) {
      LOG(INFO) << "Bluetooth adapter is in the blocklist.";
      return ApplyResult::kBlocked;
    }

    bt.ProcessOverridesForVidPid(id_vendor, id_product);
    LOG(INFO) << "Override(s) was found and applied.";

    // TODO(b/277581437): Handle the case when there are multiple Bluetooth
    // adapters. There's currently only support for one Bluetooth adapter.
    // This presents issue where an external Bluetooth adapter cannot be
    // used over an existing internal Bluetooth adapter.
    // (To clarify, if a device has no internal Bluetooth adapter, a user can
    // still currently use an external Bluetooth adapter since there is only
    // one Bluetooth adapter to choose from).
    return ApplyResult::kApplied;
  }

  return ApplyResult::kNotApplied;
}

bool get_devices(const std::unique_ptr<brillo::Udev>& udev,
                 const char* classAttribute,
                 const char* subClassAttribute,
                 std::vector<std::unique_ptr<brillo::UdevDevice>>* result) {
  const auto dev_enumerate = udev->CreateEnumerate();

  if (!dev_enumerate->AddMatchSysAttribute(classAttribute,
                                           kBluetoothDeviceClass) ||
      !dev_enumerate->AddMatchSysAttribute(subClassAttribute,
                                           kBluetoothDeviceSubClass) ||
      !dev_enumerate->ScanDevices()) {
    LOG(INFO) << "Failed to confirm enumerator properties.";
    return false;
  }

  result->clear();
  auto dev_list_entry = dev_enumerate->GetListEntry();
  for (; dev_list_entry; dev_list_entry = dev_list_entry->GetNext()) {
    const std::string sys_path = dev_list_entry->GetName() ?: "";
    std::unique_ptr<brillo::UdevDevice> device =
        udev->CreateDeviceFromSysPath(sys_path.c_str());
    if (!device) {
      LOG(INFO) << "Device Syspath " << sys_path << " not found.";
      continue;
    }
    result->push_back(std::move(device));
  }
  return true;
}

std::optional<int> get_hci_index() {
  int sock = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
  if (sock < 0) {
    LOG(INFO) << "Fail to create socket";
    return std::nullopt;
  }

  struct hci_dev_list_req dev_list = {};
  dev_list.dev_num = 1;
  if (ioctl(sock, HCIGETDEVLIST, &dev_list) < 0) {
    LOG(INFO) << "Fail to ioctl HCIGETDEVLIST";
    close(sock);
    return std::nullopt;
  }

  int hci_index = dev_list.dev_req[0].dev_id;
  LOG(INFO) << "Received bluetooth hci index " << hci_index;
  return hci_index;
}

std::optional<std::string> get_driver_name() {
  auto hci_index = get_hci_index();
  if (!hci_index) {
    return std::nullopt;
  }

  base::FilePath module_path = base::FilePath(std::format(
      "/sys/class/bluetooth/hci{}/device/driver/module", *hci_index));
  base::FilePath real_path;

  if (!base::NormalizeFilePath(module_path, &real_path)) {
    LOG(INFO) << "Module symlink can't be followed";
    return std::nullopt;
  }

  std::string name = real_path.BaseName().value();
  LOG(INFO) << "Received module name " << name;
  return name;
}

ApplyResult attempt_apply_override(
    const flex_bluetooth::FlexBluetoothOverrides& bt) {
  const auto udev = brillo::Udev::Create();
  std::string module_name;

  for (unsigned int attempt = 1; attempt <= number_of_tries; attempt++) {
    if (auto name = get_driver_name()) {
      // BT is ready. Future failures don't need to be retried.
      module_name = *name;
      break;
    }

    // BT is not ready, sleep and maybe retry later.
    if (attempt < number_of_tries) {
      LOG(INFO) << "Device not found. Attempt #" << attempt << ". Retry in "
                << seconds_between_retries << "s";
      sleep(seconds_between_retries);
    } else {
      LOG(WARNING) << "Didn't find a Bluetooth adapter.";
      return ApplyResult::kNotApplied;
    }
  }

  // It's difficult to get VID:PID for non-USB transport, so for now only
  // apply override if BT is on USB transport.
  if (module_name != "btusb") {
    LOG(INFO) << "Override(s) don't apply to module " << module_name << ".";
    return ApplyResult::kNotApplied;
  }

  std::vector<std::unique_ptr<brillo::UdevDevice>> devices;

  // Check if a BT device is found on udev
  if (!get_devices(udev, kAttributeDeviceClass, kAttributeDeviceSubClass,
                   &devices)) {
    return ApplyResult::kNotApplied;
  }

  ApplyResult result = check_and_apply_overrides(bt, devices);
  if (result == ApplyResult::kBlocked || result == ApplyResult::kApplied) {
    return result;
  }

  // No device is recognized as BT...
  // Now check if a device's interface is recognized as BT.
  if (!get_devices(udev, kAttributeInterfaceClass, kAttributeInterfaceSubClass,
                   &devices)) {
    return ApplyResult::kNotApplied;
  }
  std::transform(devices.begin(), devices.end(), devices.begin(),
                 [](std::unique_ptr<brillo::UdevDevice>& device) {
                   return device->GetParent();
                 });
  result = check_and_apply_overrides(bt, devices);
  if (result == ApplyResult::kBlocked || result == ApplyResult::kApplied) {
    return result;
  }

  LOG(INFO) << "btusb device but not found on USB tree.";
  return ApplyResult::kNotApplied;
}

int main() {
  brillo::InitLog(brillo::kLogToSyslog);
  LOG(INFO) << "Started process_flex_bluetooth_overrides.";

  const flex_bluetooth::FlexBluetoothOverrides bt(kSyspropOverridePath,
                                                  kAdapterSyspropOverrides);
  ApplyResult result = attempt_apply_override(bt);

  if (result == ApplyResult::kBlocked) {
    LOG(INFO) << "Bluetooth adapter is blocked. Exiting with failure.";
    return 1;
  }

  if (result == ApplyResult::kNotApplied) {
    LOG(INFO) << "Removing overrides.";
    bt.RemoveOverrides();
  }

  LOG(INFO) << "Exiting process_flex_bluetooth_overrides.";
  return 0;
}
