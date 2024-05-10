// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef USB_BOUNCER_UTIL_H_
#define USB_BOUNCER_UTIL_H_

#include <unistd.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/time/time.h>
#include <brillo/files/safe_fd.h>
#include <google/protobuf/map.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/timestamp.pb.h>
#include <metrics/metrics_library.h>

#include "usb_bouncer/usb_bouncer.pb.h"

namespace usb_bouncer {

using google::protobuf::Timestamp;
using EntryMap = google::protobuf::Map<std::string, RuleEntry>;

constexpr char kUsbBouncerUser[] = "usb_bouncer";
constexpr char kUsbBouncerGroup[] = "usb_bouncer";

constexpr char kDefaultDbName[] = "devices.proto";
constexpr char kUserDbBaseDir[] = "/run/daemon-store/usb_bouncer";
constexpr char kUserDbParentDir[] = "device-db";

constexpr char kDBusPath[] = "/run/dbus/system_bus_socket";
constexpr char kUsbDriversPath[] = "/sys/bus/usb/drivers";

constexpr uid_t kRootUid = 0;

constexpr int kDefaultWaitTimeoutInSeconds = 5;

constexpr size_t kMaxWriteAttempts = 10;
constexpr uint32_t kAttemptDelayMicroseconds = 10000;

constexpr char kBcdDevicePath[] = "bcdDevice";
constexpr char kConnectionDurationPath[] = "power/connected_duration";
constexpr char kDeviceClassPath[] = "bDeviceClass";
constexpr char kDriverPath[] = "driver";
constexpr char kEndpointAddress[] = "bEndpointAddress";
constexpr char kInterfaceClassPath[] = "bInterfaceClass";
constexpr char kInterfaceProtocolPath[] = "bInterfaceProtocol";
constexpr char kInterfaceSubClassPath[] = "bInterfaceSubClass";
constexpr char kDevpathPath[] = "devpath";
constexpr char kPanelPath[] = "physical_location/panel";
constexpr char kProductIdPath[] = "idProduct";
constexpr char kProductPath[] = "product";
constexpr char kRemovablePath[] = "removable";
constexpr char kSpeedPath[] = "speed";
constexpr char kVendorIdPath[] = "idVendor";
constexpr char kVendorPath[] = "manufacturer";
constexpr char kVersionPath[] = "version";

enum class UdevAction { kAdd = 0, kRemove = 1 };

enum class UMADeviceRecognized {
  kRecognized,
  kUnrecognized,
};

enum class UMAEventTiming {
  kLoggedOut = 0,
  kLoggedIn = 1,
  kLocked = 2,

  // TODO(crbug.com/1218246) Change UMA enum names kUmaDeviceAttachedHistogram.*
  // if new enums are added to avoid data discontinuity.
  kMaxValue = kLocked,
};

enum class UMAPortType {
  kTypeA,
  kTypeC,
};

enum class UMADeviceSpeed {
  kOther = 0,
  k1_5 = 1,          // 1.5 Mbps (USB 1.1)
  k12 = 2,           // 12 Mbps (USB 1.1)
  k480 = 3,          // 480 Mbps (USB 2.0)
  k480Fallback = 4,  // SuperSpeed device operating in 480 Mbps (USB 2.0)
  k5000 = 5,         // 5000 Mbps (USB 3.2 Gen 1)
  k10000 = 6,        // 10000 Mbps (USB 3.2 Gen 2)
  k20000 = 7,        // 20000 Mbps (USB 3.2 Gen 2x2)
  kMaxValue = k20000,
};

enum class UMADeviceDriver {
  kNone = 1,
  kUnknown = 2,
  kBTUSB = 3,
  kCDCACM = 4,
  kCDCEther = 5,
  kCDCMBIM = 6,
  kCDCNCM = 7,
  kCDCWDM = 8,
  kHub = 9,
  kSndUSBAudio = 10,
  kUAS = 11,
  kUDL = 12,
  kUMSRealtek = 13,
  kUSB = 14,
  kUSBStorage = 15,
  kUSBFS = 16,
  kUSBHID = 17,
  kMaxVaule = kUSBHID,
};

enum class UMADeviceError {
  kAny = 0,
  kLanguageIdError = 1,
  kFailedToSuspend = 2,
  kNotAuthorized = 3,
  kNotAcceptingAddress = 4,
  kStringDescriptorZero = 5,
  kDescriptorReadError = 6,
  kHubWithoutPorts = 7,
  kHubPortStatusError = 8,
  kUnabletoEnumerate = 9,
  kOverCurrent = 10,
  kPortDisabled = 11,
  kCannotReset = 12,
  kCannotDisable = 13,
  kCannotEnable = 14,
  kMaxValue = kCannotEnable,
};

struct UdevMetric {
  UdevAction action;
  std::string devpath;
  int busnum;
  int devnum;
  int vid;
  int pid;
  int64_t init_time;
};

// Returns true if the process has CAP_CHOWN.
bool CanChown();

// Returns true if the write succeeds.
bool WriteWithTimeout(
    brillo::SafeFD* fd,
    const std::string& value,
    size_t max_tries = kMaxWriteAttempts,
    base::TimeDelta delay = base::Microseconds(kAttemptDelayMicroseconds),
    ssize_t (*write_func)(int, const void*, size_t) = &write,
    int (*usleep_func)(useconds_t) = &usleep,
    int (*ftruncate_func)(int, off_t) = &ftruncate);

std::string Hash(const std::string& content);
std::string Hash(const google::protobuf::RepeatedPtrField<std::string>& rules);

// Set USB devices to be authorized by default and authorize any devices that
// were left unauthorized. This is performed on unlock when USBGuard is
// disabled. If an error occurs, false is returned.
bool AuthorizeAll(const std::string& devpath = "/sys/devices");

// Invokes usbguard to get a rule corresponding to |devpath|. Note that
// |devpath| isn't actually a valid path until you prepend "/sys". This matches
// the behavior of udev. The return value is a allow-list rule from usbguard
// with the port specific fields removed.
std::string GetRuleFromDevPath(const std::string& devpath);

// Returns false for rules that should not be included in the allow-list at the
// lock screen. The basic idea is to exclude devices whose function cannot be
// performed if they are first plugged in at the lock screen. Some examples
// include printers, scanners, and USB storage devices.
bool IncludeRuleAtLockscreen(const std::string& rule);

// Returns false if rule is not a valid rule.
bool ValidateRule(const std::string& rule);

// Log device attach events to inform future changes in policy.
void UMALogDeviceAttached(MetricsLibrary* metrics,
                          const std::string& rule,
                          UMADeviceRecognized recognized,
                          UMAEventTiming timing);

// Log external device attach events.
void UMALogExternalDeviceAttached(MetricsLibrary* metrics,
                                  const std::string& rule,
                                  UMADeviceRecognized recognized,
                                  UMAEventTiming timing,
                                  UMAPortType port,
                                  UMADeviceSpeed speed);

// Report structured metrics on external device attach events.
void StructuredMetricsExternalDeviceAttached(
    int VendorId,
    std::string VendorName,
    int ProductId,
    std::string ProductName,
    int DeviceClass,
    std::vector<int64_t> InterfaceClass);

// Report structured metrics on internal camera modules
void StructuredMetricsInternalCameraModule(int VendorId,
                                           std::string VendorName,
                                           int ProductId,
                                           std::string ProductName,
                                           int BcdDevice);

// Reports common metrics logged by the USB bouncer processing both udev add
// and remove events.
void ReportMetricsUdev(UdevMetric* udev_metric);

// Reports metrics logged by the USB bouncer processing udev add events.
void ReportMetricsUdevAdd(UdevMetric* udev_metric);

// Reports metrics logged by the USB bouncer processing udev remove events.
void ReportMetricsUdevRemove(UdevMetric* udev_metric);

// Report structured metric on error uevents from the hub driver.
void StructuredMetricsHubError(int ErrorCode,
                               int VendorId,
                               int ProductId,
                               int DeviceClass,
                               std::string UsbTreePath,
                               int ConnectedDuration);

// Report structured metric on error uevents from the xHCI driver.
void StructuredMetricsXhciError(int ErrorCode, int DeviceClass);

// Returns the path where the user DB should be written if there is a user
// signed in and CrOS is unlocked. Otherwise, returns an empty path. In the
// multi-login case, the primary user's daemon-store is used.
base::FilePath GetUserDBDir();

// Returns true if a guest session is active.
bool IsGuestSession();

// Returns true if the lock screen is being shown. On a D-Bus failure true is
// returned because that is the safer failure state. This may result in some
// devices not being added to a user's allow-list, but that is safer than a
// malicious device being added to the allow-list while at the lock-screen.
bool IsLockscreenShown();

std::string StripLeadingPathSeparators(const std::string& path);

// Returns a set of all the rules present in |entries|. This serves as a
// filtering step prior to generating the rules configuration for
// usbguard-daemon so that there aren't duplicate rules. The rules are
// de-duplicated by string value ignoring any metadata like the time last used.
std::unordered_set<std::string> UniqueRules(const EntryMap& entries);

// Attempts to open the specified statefile at
// |base_path|/|parent|/|state_file_name| with the proper permissions. The
// parent directory and state file will be cleared if the ownership or
// permissions don't match. They will be created if they do not exist. If |lock|
// is true, this call blocks until an exclusive lock can be obtained for |path|.
// All runs of usb_bouncer are expected to be relatively fast (<250ms), so
// blocking should be ok.
brillo::SafeFD OpenStateFile(const base::FilePath& base_path,
                             const std::string& parent_dir,
                             const std::string& state_file_name,
                             const std::string& username,
                             bool lock);

// Forks (exiting the parent), calls setsid, and returns the result of a second
// fork.
//
// This is used to avoid blocking udev while waiting on journald to finish
// setting up logging, D-Bus to be ready, or D-Bus calls that can take on the
// order of seconds to complete.
void Daemonize();

void UpdateTimestamp(Timestamp* timestamp);
size_t RemoveEntriesOlderThan(base::TimeDelta cutoff, EntryMap* map);

// Given an USB device path, parse its root device path through USB device sysfs
// topology. If the given device is not part of a tree (no USB hub in between),
// return |dev| as it is.
//
// E.g. .../1-2/1-2.3/1-2.3.4 is attached to the root hub, .../1-2.
base::FilePath GetRootDevice(base::FilePath dev);

// Given a USB interface path, return the path of its parent USB device.
// If GetInterfaceDevice is unable to determine the parent USB device, it will
// return an empty FilePath.
base::FilePath GetInterfaceDevice(base::FilePath intf);

// Given a devpath, determine if the USB device is external or internal based on
// physical location of device (PLD) and removable property.
bool IsExternalDevice(base::FilePath normalized_devpath);

// Determine if the board is ChromeOS Flex to exclude from metrics reporting
// since we do not have control over firmware on ChromeOS Flex and sysfs values
// are unexpected. Return true if the board cannot be determined to avoid
// possibility of metrics pollution.
bool IsFlexBoard();

// Returns port type for a sysfs device (i.e. USB-A, USB-C).
UMAPortType GetPortType(base::FilePath normalized_devpath);

// Returns USB device speed for a sysfs device.
UMADeviceSpeed GetDeviceSpeed(base::FilePath normalized_devpath);

// Returns USB driver enum value from driver name.
UMADeviceDriver GetDriverEnum(std::string driver);

// Determine if any of the devices implements the UVC interface.
bool IsCamera(std::vector<int64_t> interfaces);

// Returns the integer value of a decimal USB device property at |prop|.
int GetDevicePropInt(base::FilePath normalized_devpath, std::string prop);

// Returns the integer value of a hexadecimal USB device property at |prop|.
int GetDevicePropHex(base::FilePath normalized_devpath, std::string prop);

// Returns the string value of a USB device property at |prop|.
std::string GetDevicePropString(base::FilePath normalized_devpath,
                                std::string prop);

// Returns vector of interface property |prop| for all of a devices interfaces.
// If there is a file read error, returns "-1" at that interface's index.
std::vector<int64_t> GetInterfacePropHexArr(base::FilePath normalized_devpath,
                                            std::string prop);

// Returns vector of endpoint property |prop| for all of a devices interfaces.
std::vector<int64_t> GetEndpointPropHexArr(base::FilePath normalized_devpath,
                                           std::string prop);

// Returns the driver bound to a given interface.
UMADeviceDriver GetDriverFromInterface(base::FilePath interface_path);

// Returns vector of integers corresponding to each interface's driver. The
// mapping is defined by UMADeviceDriver.
std::vector<int64_t> GetInterfaceDrivers(base::FilePath normalized_devpath);

// Assigns VID and PID from a uevent's product environment variable. This can
// be used by USB bouncer methods that receive the product environment variable
// to read VID/PID on device disconnection.
void GetVidPidFromEnvVar(std::string product, int* vendor_id, int* product_id);

// Returns the depth of a device in a USB topology. This is based on the USB
// tree path returned by GetUsbTreePath.
int GetUsbTreeDepth(base::FilePath normalized_devpath);

// Returns the PCI device class for a sysfs device.
int GetPciDeviceClass(base::FilePath normalized_devpath);

// Returns the kernel boot_id, which is a unique identifier randomly generated
// each time a system boots.
std::string GetBootId();

// Returns a connection id based on boot id, connection time, busnum and devnum
// which is unique to each device connection.
std::string GenerateConnectionId(UdevMetric* udev_metric);

// Returns the time since boot in microseconds.
int64_t GetSystemTime();

// Returns the amount of time the system has been suspended in microseconds.
uint64_t GetSuspendTime();

// Returns a device's connection duration from the current system time and
// device init time reported by udev.
uint64_t GetConnectionDuration(uint64_t init_time);

// Returns a --since option for a dmesg query to include loglines starting at
// the given |init_time|.
std::string GetDmesgOffset(uint64_t init_time);

// Parses dmesg errors returned by D-Bus for errors which can be attributed to
// the provided device.
std::vector<UMADeviceError> ParseDmesgErrors(std::string devpath,
                                             std::string dmesg);

// Returns a vector of device errors in dmesg over the lifespan of the device's
// connection.
std::vector<UMADeviceError> GetDeviceErrors(UdevMetric* udev_metric);

}  // namespace usb_bouncer

#endif  // USB_BOUNCER_UTIL_H_
