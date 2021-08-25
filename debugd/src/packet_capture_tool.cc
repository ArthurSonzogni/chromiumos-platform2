// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/packet_capture_tool.h"

#include <base/logging.h>
#include <base/strings/string_util.h>
#include <string>

#include "debugd/src/error_utils.h"
#include "debugd/src/helper_utils.h"
#include "debugd/src/process_with_id.h"
#include "debugd/src/variant_utils.h"

#include "policy/device_policy.h"
#include "policy/libpolicy.h"

namespace {

const char kPacketCaptureToolErrorString[] =
    "org.chromium.debugd.error.PacketCapture";

bool ValidateInterfaceName(const std::string& name) {
  for (char c : name) {
    // These are the only plausible interface name characters.
    if (!base::IsAsciiAlpha(c) && !base::IsAsciiDigit(c) && c != '-' &&
        c != '_')
      return false;
  }
  return true;
}

bool AddValidatedStringOption(debugd::ProcessWithId* p,
                              const brillo::VariantDictionary& options,
                              const std::string& dbus_option,
                              const std::string& command_line_option,
                              brillo::ErrorPtr* error) {
  std::string name;
  switch (debugd::GetOption(options, dbus_option, &name, error)) {
    case debugd::ParseResult::NOT_PRESENT:
      return true;
    case debugd::ParseResult::PARSE_ERROR:
      return false;
    case debugd::ParseResult::PARSED:
      break;
  }

  if (!ValidateInterfaceName(name)) {
    DEBUGD_ADD_ERROR_FMT(error, kPacketCaptureToolErrorString,
                         "\"%s\" is not a valid interface name", name.c_str());
    return false;
  }

  p->AddStringOption(command_line_option, name);
  return true;
}

// Returns true when packet capture is allowed in device. Packet capture is
// allowed in all devices (consumer-owned devices, enterprise-enrolled devices
// and OOBE) by default and can be disabled by the
// DeviceDebugPacketCaptureAllowed policy by the administrator for
// enterprise-enrolled devices.
bool IsDevicePacketCaptureAllowed(brillo::ErrorPtr* error) {
  policy::PolicyProvider policy_provider;

  // Return true without trying to check the policy if the device is not
  // enrolled as unenrolled devices won't have policies and packet capture
  // should be available by default. This means packet capture will be
  // allowed in consumer-owned devices and in OOBE state.
  if (!policy_provider.IsEnterpriseEnrolledDevice()) {
    return true;
  }

  policy_provider.Reload();
  // No available policies.
  if (!policy_provider.device_policy_is_loaded()) {
    DEBUGD_ADD_ERROR(error, kPacketCaptureToolErrorString,
                     "No device policy available on this device, can't check "
                     "for packet capture policy setting.");
    return false;
  }

  const policy::DevicePolicy* policy = &policy_provider.GetDevicePolicy();
  bool packet_capture_allowed = false;
  // Check if packet captures are allowed by policy for the device.
  if (!policy->GetDeviceDebugPacketCaptureAllowed(&packet_capture_allowed)) {
    // This means policy was not set for the device. Return true since the
    // default value of the policy is defined as true in the policy
    // documentation.
    return true;
  }
  return packet_capture_allowed;
}

bool CheckDeviceBasedCaptureMode(const brillo::VariantDictionary& options,
                                 brillo::ErrorPtr* error) {
  std::string device_value;
  // Check if the "device" option exists in options dictionary. It must be
  // present in device based capture mode.
  if (debugd::GetOption(options, "device", &device_value, error) !=
      debugd::ParseResult::PARSED) {
    return false;
  }
  int freq_value;
  // Check if the "frequency" option exists in options dictionary. It can't be
  // present in device based capture mode.
  if (debugd::GetOption(options, "frequency", &freq_value, error) ==
      debugd::ParseResult::PARSED) {
    return false;
  }
  std::string frequency_based_options[] = {"ht_location", "vht_width",
                                           "monitor_connection_on"};
  // If any of the frequency-based options is present in the arguments, it means
  // the capture will be frequency based.
  for (const std::string& option : frequency_based_options) {
    std::string val;
    debugd::ParseResult result =
        debugd::GetOption(options, option, &val, error);
    if (result == debugd::ParseResult::PARSED) {
      return false;
    }
  }
  // If device option is parsed and none of the frequency based option is
  // present, it means the capture is on device based mode.
  return true;
}

}  // namespace

namespace debugd {

// Creates helper process for frequency-based (Layer-2) capture and return the
// process. Returns nullptr if process can't be created.
debugd::ProcessWithId*
PacketCaptureTool::CreateCaptureProcessForFrequencyBasedCapture(
    const brillo::VariantDictionary& options,
    int output_fd,
    brillo::ErrorPtr* error) {
  std::string exec_path;
  if (!GetHelperPath("capture_utility.sh", &exec_path)) {
    DEBUGD_ADD_ERROR(error, kPacketCaptureToolErrorString,
                     "Unable to get helper path for frequency-based capture.");
    return nullptr;
  }

  debugd::ProcessWithId* p =
      CreateProcess(false /* sandboxed */, false /* access_root_mount_ns */);
  if (!p) {
    DEBUGD_ADD_ERROR(error, kPacketCaptureToolErrorString,
                     "Failed to create process for device-based capture.");
    return nullptr;
  }
  p->AddArg(exec_path);
  if (!AddValidatedStringOption(p, options, "device", "--device", error))
    return nullptr;
  if (!AddIntOption(p, options, "max_size", "--max-size", error))
    return nullptr;
  if (!AddIntOption(p, options, "frequency", "--frequency", error))
    return nullptr;
  if (!AddValidatedStringOption(p, options, "ht_location", "--ht-location",
                                error))
    return nullptr;
  if (!AddValidatedStringOption(p, options, "vht_width", "--vht-width", error))
    return nullptr;
  if (!AddValidatedStringOption(p, options, "monitor_connection_on",
                                "--monitor-connection-on", error))
    return nullptr;
  // Pass the output fd of the pcap as a command line option to the child
  // process.
  p->AddIntOption("--output-file", output_fd);

  return p;
}

// Creates helper process for device-based (Layer-3) capture and return the
// process. Returns nullptr if process can't be created.
debugd::ProcessWithId*
PacketCaptureTool::CreateCaptureProcessForDeviceBasedCapture(
    const brillo::VariantDictionary& options,
    int output_fd,
    brillo::ErrorPtr* error) {
  std::string exec_path;
  if (!GetHelperPath("capture_packets", &exec_path)) {
    DEBUGD_ADD_ERROR(error, kPacketCaptureToolErrorString,
                     "Unable to get helper path for device-based capture.");
    return nullptr;
  }

  ProcessWithId* p =
      CreateProcess(false /* sandboxed */, false /* access_root_mount_ns */);
  if (!p) {
    DEBUGD_ADD_ERROR(error, kPacketCaptureToolErrorString,
                     "Failed to create process for device-based capture.");
    return nullptr;
  }
  p->AddArg(exec_path);
  // capture_packets executable takes three arguments as <device> <output_file>
  // <max_size>
  std::string device;
  // device option must be present and successfully parsed in order to create
  // process.
  if (debugd::GetOption(options, "device", &device, error) !=
      debugd::ParseResult::PARSED) {
    DEBUGD_ADD_ERROR(
        error, kPacketCaptureToolErrorString,
        "Failed to parse required --device option from arguments.");
    return nullptr;
  }
  p->AddArg(device);
  p->AddArg(std::to_string(output_fd));
  int max_size = 0;
  debugd::GetOption(options, "max_size", &max_size, error);
  p->AddArg(std::to_string(max_size));

  return p;
}

bool PacketCaptureTool::Start(bool is_dev_mode,
                              const base::ScopedFD& status_fd,
                              const base::ScopedFD& output_fd,
                              const brillo::VariantDictionary& options,
                              std::string* out_id,
                              brillo::ErrorPtr* error) {
  if (!IsDevicePacketCaptureAllowed(error)) {
    DEBUGD_ADD_ERROR(error, kPacketCaptureToolErrorString,
                     "Packet capture is not allowed on device. Please check "
                     "your policy settings to enable.");
    return false;
  }

  ProcessWithId* p;
  // The fd in the child that we bind output_fd to. Since all other fd's are
  // cleared automatically, picking a hardcoded value should be safe.
  int child_output_fd = STDERR_FILENO + 1;
  // Check if the capture will be device-based or frequency-based and create
  // helper process accordingly using different executables.
  // TODO(b/188391723): Merge capture_utility.sh and capture_packets executables
  // into one.
  if (CheckDeviceBasedCaptureMode(options, error)) {
    p = CreateCaptureProcessForDeviceBasedCapture(options, child_output_fd,
                                                  error);
  } else if (is_dev_mode) {
    p = CreateCaptureProcessForFrequencyBasedCapture(options, child_output_fd,
                                                     error);
  } else {
    DEBUGD_ADD_ERROR(
        error, kPacketCaptureToolErrorString,
        "The requested capture is frequency-based and it's only available in "
        "developer mode. Please switch to developer mode to use this option.");
    return false;
  }
  if (!p) {
    DEBUGD_ADD_ERROR(error, kPacketCaptureToolErrorString,
                     "Failed to create helper process.");
    return false;
  }
  p->BindFd(output_fd.get(), child_output_fd);
  p->BindFd(status_fd.get(), STDOUT_FILENO);
  p->BindFd(status_fd.get(), STDERR_FILENO);
  LOG(INFO) << "packet_capture: running process id: " << p->id();
  p->Start();
  *out_id = p->id();
  return true;
}

}  // namespace debugd
