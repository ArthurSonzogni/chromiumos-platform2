// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/udev_collector.h"

#include <fcntl.h>

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/process/process.h>
#include <brillo/userdb_utils.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>
#include <metrics/metrics_library.h>

#include <crash-reporter/dbus_adaptors/org.chromium.CrashReporterInterface.h>

#include "crash-reporter/connectivity_util.h"
#include "crash-reporter/constants.h"
#include "crash-reporter/crash_adaptor.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/udev_bluetooth_util.h"
#include "crash-reporter/util.h"

using base::FilePath;

namespace {

const char kCollectUdevSignature[] = "crash_reporter-udev-collection";
const char kDefaultDevCoredumpDirectory[] = "/sys/class/devcoredump";
const char kDevCoredumpFilePrefixFormat[] = "devcoredump_%s";
const char kDevCoredumpMsmExecName[] = "devcoredump_adreno";
const char kDevCoredumpAmdgpuExecName[] = "devcoredump_amdgpu";
const char kUdevDrmExecName[] = "udev-drm";
const char kUdevExecName[] = "udev";
const char kUdevSignatureKey[] = "sig";
const char kUdevSubsystemDevCoredump[] = "devcoredump";
const char kUdevTouchscreenTrackpadExecName[] = "udev-i2c-atmel_mxt_ts";
const char kUdevUsbExecName[] = "udev-usb";
const char kIntelWiFiDriverName[] = "iwlwifi";

// Udev constants
const char kUdevSubsystem[] = "SUBSYSTEM";
const char kUdevKernelNumber[] = "KERNEL_NUMBER";
const char kUdevAction[] = "ACTION";
const char kUdevDriver[] = "DRIVER";
const char kUdevKernel[] = "KERNEL";
}  // namespace

UdevCollector::UdevCollector(
    const scoped_refptr<
        base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
        metrics_lib)
    : CrashCollector("udev", metrics_lib),
      dev_coredump_directory_(kDefaultDevCoredumpDirectory) {}

UdevCollector::~UdevCollector() {}

bool UdevCollector::IsSafeDevCoredump(
    std::map<std::string, std::string> udev_event_map) {
  // Is it a device coredump?
  if (udev_event_map[kUdevSubsystem] != kUdevSubsystemDevCoredump)
    return false;

  int instance_number;
  if (!base::StringToInt(udev_event_map[kUdevKernelNumber], &instance_number)) {
    LOG(ERROR) << "Invalid kernel number: "
               << udev_event_map[kUdevKernelNumber];
    return false;
  }

  // Retrieve the driver name of the failing device.
  std::string driver_name = GetFailingDeviceDriverName(instance_number);
  if (driver_name.empty()) {
    LOG(ERROR) << "Failed to obtain driver name for instance: "
               << instance_number;
    return false;
  }

  // Check for safe drivers:
  return driver_name == "adreno" || driver_name == "qcom-venus" ||
         driver_name == "amdgpu";
}

CrashCollector::ComputedCrashSeverity UdevCollector::ComputeSeverity(
    const std::string& exec_name) {
  ComputedCrashSeverity computed_severity{
      .crash_severity = CrashSeverity::kUnspecified,
      .product_group = Product::kPlatform,
  };

  if (exec_name == kUdevUsbExecName) {
    computed_severity.crash_severity = CrashSeverity::kError;
  } else if ((exec_name == kDevCoredumpMsmExecName) ||
             (exec_name == kDevCoredumpAmdgpuExecName) ||
             (exec_name == kUdevTouchscreenTrackpadExecName) ||
             (exec_name == kUdevDrmExecName)) {
    computed_severity.crash_severity = CrashSeverity::kWarning;
  }

  return computed_severity;
}

bool UdevCollector::IsConnectivityWiFiFwdump(int instance_number) {
  std::string driver_name = GetFailingDeviceDriverName(instance_number);

  return driver_name == kIntelWiFiDriverName;
}

bool UdevCollector::CheckConnectivityFwdumpAllowedFinchFlagStatus() {
  std::string val;

  if (!base::ReadFileToStringWithMaxSize(
          paths::Get(paths::kAllowFirmwareDumpsFlagPath), &val,
          /*max_size*/ 1)) {
    if (val.empty()) {
      LOG(ERROR) << "Failed to read "
                 << paths::Get(paths::kAllowFirmwareDumpsFlagPath) << ".";
      return false;
    }
    LOG(ERROR)
        << "Connectivity fwdump Finch value is larger than expected size (1).";
    return false;
  }

  return val == "1";
}

std::optional<connectivity_util::Session>
UdevCollector::ConnectivityFwdumpAllowedForUserSession(
    std::map<std::string, std::string>& udev_event_map,
    int instance_number,
    const base::FilePath& coredump_path) {
  std::optional<connectivity_util::Session> user_session;
  if ((udev_event_map[kUdevSubsystem] == kUdevSubsystemDevCoredump) &&
      IsConnectivityWiFiFwdump(instance_number) &&
      CheckConnectivityFwdumpAllowedFinchFlagStatus() &&
      connectivity_fwdump_feature_enabled_) {
    SetUpDBus();
    user_session =
        connectivity_util::GetPrimaryUserSession(session_manager_proxy_.get());
    if (!user_session.has_value()) {
      LOG(INFO) << "No Primary Session found, exiting.";
      ClearDevCoredump(coredump_path);
      return std::nullopt;
    }
    if (!connectivity_util::IsConnectivityFwdumpAllowed(
            session_manager_proxy_.get(), user_session->username)) {
      // Clear devcoredump memory before returning.
      ClearDevCoredump(coredump_path);
      return std::nullopt;
    }
    return user_session;
  }
  return std::nullopt;
}

bool UdevCollector::HandleCrash(const std::string& udev_event) {
  // Process the udev event string.
  // First get all the key-value pairs.
  std::vector<std::pair<std::string, std::string>> udev_event_keyval;
  base::SplitStringIntoKeyValuePairs(udev_event, '=', ':', &udev_event_keyval);
  std::map<std::string, std::string> udev_event_map;
  for (const auto& key_value : udev_event_keyval) {
    udev_event_map[key_value.first] = key_value.second;
  }

  int instance_number = -1;
  if (udev_event_map[kUdevSubsystem] == kUdevSubsystemDevCoredump) {
    if (!base::StringToInt(udev_event_map[kUdevKernelNumber],
                           &instance_number)) {
      LOG(ERROR) << "Invalid kernel number: "
                 << udev_event_map[kUdevKernelNumber] << ".";
      return false;
    }
  }

  FilePath coredump_path = FilePath(
      base::StringPrintf("%s/devcd%s/data", dev_coredump_directory_.c_str(),
                         udev_event_map[kUdevKernelNumber].c_str()));

  bool is_connectivity_fwdump = false;
  std::optional<connectivity_util::Session> user_session =
      ConnectivityFwdumpAllowedForUserSession(udev_event_map, instance_number,
                                              coredump_path);
  if (user_session.has_value()) {
    is_connectivity_fwdump = true;
    LOG(INFO) << "Process Connectivity intel wifi fwdumps.";
  } else if (bluetooth_util::IsCoredumpEnabled() &&
             bluetooth_util::IsBluetoothCoredump(coredump_path)) {
    LOG(INFO) << "Process bluetooth devcoredump.";
  } else if (UdevCollector::IsSafeDevCoredump(udev_event_map)) {
    LOG(INFO) << "Safe device coredumps are always processed";
  } else if (util::IsDeveloperImage()) {
    LOG(INFO) << "developer image - collect udev crash info.";
  } else if (udev_event_map[kUdevSubsystem] == kUdevSubsystemDevCoredump) {
    LOG(INFO) << "Device coredumps are not processed on non-developer images.";
    // Clear devcoredump memory before returning.
    ClearDevCoredump(coredump_path);
    return false;
  } else {
    LOG(INFO) << "Consent given - collect udev crash info.";
  }

  base::FilePath crash_directory;
  if (is_connectivity_fwdump) {
    // Connectivity firmware dumps are stored in different directory than
    // normal crashes, because unlike normal crashes, connectivity firmware
    // dumps are uploaded to feedback reports rather than crash reporter server.
    // GetConnectivityFwdumpStoragePath opens fbpreprocessord cryptohome
    // directory and returns a symlinked handle.
    std::optional<base::FilePath> maybe_crash_directory =
        GetConnectivityFwdumpStoragePath(*user_session);
    if (!maybe_crash_directory.has_value()) {
      LOG(ERROR)
          << "Could not get storage directory for connectivity fw dumps.";
      return false;
    }
    crash_directory = *maybe_crash_directory;
  } else {
    // Make sure the crash directory exists, or create it if it doesn't.
    if (!GetCreatedCrashDirectoryByEuid(0, &crash_directory, nullptr)) {
      LOG(ERROR) << "Could not get crash directory.";
      return false;
    }
  }

  if (udev_event_map[kUdevSubsystem] == kUdevSubsystemDevCoredump) {
    return ProcessDevCoredump(crash_directory, instance_number,
                              is_connectivity_fwdump);
  }

  return ProcessUdevCrashLogs(crash_directory, udev_event_map[kUdevAction],
                              udev_event_map[kUdevKernel],
                              udev_event_map[kUdevSubsystem]);
}

std::optional<base::FilePath> UdevCollector::GetConnectivityFwdumpStoragePath(
    const connectivity_util::Session& user_session) {
  std::optional<base::FilePath> maybe_directory =
      connectivity_util::GetDaemonStoreFbPreprocessordDirectory(user_session);

  if (!maybe_directory) {
    LOG(ERROR) << "Could not get connectivity fwdump storage directory.";
    return std::nullopt;
  }

  mode_t directory_mode;
  uid_t directory_owner;
  gid_t directory_group;

  directory_mode = constants::kDaemonStoreCrashPathMode;
  if (!brillo::userdb::GetUserInfo(constants::kFbpreprocessorUserName,
                                   &directory_owner, nullptr)) {
    LOG(ERROR) << "Couldn't look up user " << constants::kFbpreprocessorUserName
               << ".";
    return std::nullopt;
  }
  if (!brillo::userdb::GetGroupInfo(constants::kFbpreprocessorGroupName,
                                    &directory_group)) {
    LOG(ERROR) << "Couldn't look up group "
               << constants::kFbpreprocessorGroupName << ".";
    return std::nullopt;
  }

  bool out_of_capacity = false;
  std::optional<base::FilePath> maybe_dir = GetCreatedCrashDirectory(
      *maybe_directory, /*can_create_or_fix=*/false, directory_mode,
      directory_owner, directory_group, &out_of_capacity);

  if (out_of_capacity) {
    LOG(ERROR) << "Storage path is full, cannot add more fwdump files.";
    return std::nullopt;
  }
  return maybe_dir;
}

bool UdevCollector::ProcessUdevCrashLogs(const FilePath& crash_directory,
                                         const std::string& action,
                                         const std::string& kernel,
                                         const std::string& subsystem) {
  // Construct the basename string for crash_reporter_logs.conf:
  //   "crash_reporter-udev-collection-[action]-[name]-[subsystem]"
  // If a udev field is not provided, "" is used in its place, e.g.:
  //   "crash_reporter-udev-collection-[action]--[subsystem]"
  // Hence, "" is used as a wildcard name string.
  // TODO(sque, crosbug.com/32238): Implement wildcard checking.
  std::string basename = action + "-" + kernel + "-" + subsystem;
  std::string udev_log_name =
      std::string(kCollectUdevSignature) + '-' + basename;

  // Create the destination path.
  std::string log_file_name = FormatDumpBasename(basename, time(nullptr), 0);
  FilePath crash_path = GetCrashPath(crash_directory, log_file_name, "log.gz");

  // Handle the crash.
  bool result = GetLogContents(log_config_path_, udev_log_name, crash_path);
  if (!result) {
    LOG(ERROR) << "Error reading udev log info " << udev_log_name;
    return false;
  }

  std::string exec_name = std::string(kUdevExecName) + "-" + subsystem;
  AddCrashMetaData(kUdevSignatureKey, udev_log_name);
  FinishCrash(GetCrashPath(crash_directory, log_file_name, "meta"), exec_name,
              crash_path.BaseName().value());
  return true;
}

bool UdevCollector::ProcessDevCoredump(const FilePath& crash_directory,
                                       int instance_number,
                                       bool is_connectivity_fwdump) {
  FilePath coredump_path = FilePath(base::StringPrintf(
      "%s/devcd%d/data", dev_coredump_directory_.c_str(), instance_number));
  if (!base::PathExists(coredump_path)) {
    LOG(ERROR) << "Device coredump file " << coredump_path.value()
               << " does not exist.";
    return false;
  }

  if (bluetooth_util::IsCoredumpEnabled() &&
      bluetooth_util::IsBluetoothCoredump(coredump_path)) {
    if (!AppendBluetoothCoredump(crash_directory, coredump_path,
                                 instance_number)) {
      ClearDevCoredump(coredump_path);
      return false;
    }
    return ClearDevCoredump(coredump_path);
  }

  // Add coredump file to the crash directory.
  if (!AppendDevCoredump(crash_directory, coredump_path, instance_number,
                         is_connectivity_fwdump)) {
    ClearDevCoredump(coredump_path);
    return false;
  }

  // Clear the coredump data to allow generation of future device coredumps
  // without having to wait for the 5-minutes timeout.
  return ClearDevCoredump(coredump_path);
}

bool UdevCollector::AppendBluetoothCoredump(const FilePath& crash_directory,
                                            const FilePath& coredump_path,
                                            int instance_number) {
  std::string coredump_prefix = bluetooth_util::kBluetoothDevCoredumpExecName;
  std::string dump_basename =
      FormatDumpBasename(coredump_prefix, time(nullptr), instance_number);
  FilePath target_path = GetCrashPath(crash_directory, dump_basename, "txt");
  FilePath log_path = GetCrashPath(crash_directory, dump_basename, "log");
  FilePath meta_path = GetCrashPath(crash_directory, dump_basename, "meta");
  std::string crash_sig;

  if (!bluetooth_util::ProcessBluetoothCoredump(coredump_path, target_path,
                                                &crash_sig)) {
    LOG(ERROR) << "Failed to parse bluetooth devcoredump.";
    return false;
  }

  if (GetLogContents(log_config_path_, coredump_prefix, log_path)) {
    AddCrashMetaUploadFile("logs", log_path.BaseName().value());
  }

  AddCrashMetaData(kUdevSignatureKey, crash_sig);
  FinishCrash(meta_path, coredump_prefix, target_path.BaseName().value());

  return true;
}

void UdevCollector::EmitConnectivityDebugDumpCreatedSignal(
    const std::string& file_name) {
  SetUpDBus();
  auto crash_interface = std::make_unique<CrashAdaptor>(bus_);

  ::fbpreprocessor::DebugDumps fw_dumps;
  ::fbpreprocessor::DebugDump* dump = fw_dumps.add_dump();

  // TODO(b/313483598): Support other DebugDump types e.g. BT, cellular.
  dump->set_type(fbpreprocessor::DebugDump::WIFI);
  auto wifi_dump = dump->mutable_wifi_dump();
  wifi_dump->set_dmpfile(file_name);
  wifi_dump->set_state(fbpreprocessor::WiFiDump::RAW);
  wifi_dump->set_vendor(fbpreprocessor::WiFiDump::IWLWIFI);

  LOG(INFO) << "Going to emit connectivity DebugDumpCreated signal.";
  crash_interface.get()->SendDebugDumpCreatedSignal(fw_dumps);
}

bool UdevCollector::AppendDevCoredump(const FilePath& crash_directory,
                                      const FilePath& coredump_path,
                                      int instance_number,
                                      bool is_connectivity_fwdump) {
  // Retrieve the driver name of the failing device.
  std::string driver_name = GetFailingDeviceDriverName(instance_number);
  if (driver_name.empty()) {
    LOG(ERROR) << "Failed to obtain driver name for instance: "
               << instance_number;
    return false;
  }

  std::string coredump_prefix =
      base::StringPrintf(kDevCoredumpFilePrefixFormat, driver_name.c_str());

  std::string dump_basename =
      FormatDumpBasename(coredump_prefix, time(nullptr), instance_number);
  FilePath core_path =
      GetCrashPath(crash_directory, dump_basename, "devcore.gz");
  FilePath log_path = GetCrashPath(crash_directory, dump_basename, "log");
  FilePath meta_path = GetCrashPath(crash_directory, dump_basename, "meta");

  // Collect coredump data.
  // We expect /sys/class/devcoredump/devcdN (the path we typically use to
  // access the dump) to be a symlink. devcdN/data, however, should not be a
  // symlink. This means we can't use functionality (e.g. SafeFD) that verifies
  // that no path components are symlinks, but we can use O_NOFOLLOW.
  const char* filename_cstr = coredump_path.value().c_str();
  int source_fd =
      HANDLE_EINTR(open(filename_cstr, O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
  if (source_fd < 0) {
    PLOG(ERROR) << "Failed to open " << filename_cstr;
    return false;
  }
  // Similarly, the core_path will be of form /proc/self/fd/<n>/foo.devcore,
  // where /proc/self is a symlink, but foo.devcore should not be.
  if (!CopyFdToNewCompressedFile(base::ScopedFD(source_fd), core_path)) {
    PLOG(ERROR) << "Failed to copy device coredump file from "
                << coredump_path.value() << " to " << core_path.value();
    return false;
  }

  // Do not write meta and log file if it is connectivity firmware dumps.
  // Connectivity firmware dumps use dbus signal to process firmware dumps
  // unlike meta file based synchronization.
  if (is_connectivity_fwdump) {
    // Generate debug dump created signal for connectivity firmware dumps
    // for fbpreprocessord to process.
    EmitConnectivityDebugDumpCreatedSignal(core_path.value());
  } else {
    // Collect additional logs if one is specified in the config file.
    std::string udev_log_name = std::string(kCollectUdevSignature) + '-' +
                                kUdevSubsystemDevCoredump + '-' + driver_name;
    bool result = GetLogContents(log_config_path_, udev_log_name, log_path);
    if (result) {
      AddCrashMetaUploadFile("logs", log_path.BaseName().value());
    }

    AddCrashMetaData(kUdevSignatureKey, udev_log_name);

    FinishCrash(meta_path, coredump_prefix, core_path.BaseName().value());
  }

  return true;
}

bool UdevCollector::ClearDevCoredump(const FilePath& coredump_path) {
  if (!base::WriteFile(coredump_path, "0", 1)) {
    PLOG(ERROR) << "Failed to delete the coredump data file "
                << coredump_path.value();
    return false;
  }
  return true;
}

FilePath UdevCollector::GetFailingDeviceDriverPath(
    int instance_number, const std::string& sub_path) {
  const FilePath dev_coredump_path(dev_coredump_directory_);
  FilePath failing_uevent_path = dev_coredump_path.Append(
      base::StringPrintf("devcd%d/%s", instance_number, sub_path.c_str()));
  return failing_uevent_path;
}

std::string UdevCollector::ExtractFailingDeviceDriverName(
    const FilePath& failing_uevent_path) {
  if (!base::PathExists(failing_uevent_path)) {
    LOG(ERROR) << "Failing uevent path " << failing_uevent_path.value()
               << " does not exist";
    return "";
  }

  std::string uevent_content;
  if (!base::ReadFileToString(failing_uevent_path, &uevent_content)) {
    PLOG(ERROR) << "Failed to read uevent file " << failing_uevent_path.value();
    return "";
  }

  // Parse uevent file contents as key-value pairs.
  std::vector<std::pair<std::string, std::string>> uevent_keyval;
  base::SplitStringIntoKeyValuePairs(uevent_content, '=', '\n', &uevent_keyval);
  for (const auto& key_value : uevent_keyval) {
    if (key_value.first == kUdevDriver) {
      return key_value.second;
    }
  }

  return "";
}

std::string UdevCollector::GetFailingDeviceDriverName(int instance_number) {
  FilePath failing_uevent_path =
      GetFailingDeviceDriverPath(instance_number, "failing_device/uevent");
  std::string name = ExtractFailingDeviceDriverName(failing_uevent_path);
  if (name.empty()) {
    LOG(WARNING)
        << "Failed to obtain driver name; trying alternate uevent paths.";
    failing_uevent_path = GetFailingDeviceDriverPath(
        instance_number, "failing_device/device/uevent");
    name = ExtractFailingDeviceDriverName(failing_uevent_path);
  }
  return name;
}

// static
CollectorInfo UdevCollector::GetHandlerInfo(
    const std::string& udev_event,
    const scoped_refptr<
        base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
        metrics_lib) {
  auto udev_collector = std::make_shared<UdevCollector>(metrics_lib);
  return {.collector = udev_collector,
          .handlers = {{
              .should_handle = !udev_event.empty(),
              .cb = base::BindRepeating(&UdevCollector::HandleCrash,
                                        udev_collector, udev_event),
          }}};
}
