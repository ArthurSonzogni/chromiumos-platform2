// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/packet_capture_tool.h"

#include <base/strings/string_util.h>

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

bool DevicePacketCaptureAllowedByPolicy(brillo::ErrorPtr* error) {
  policy::PolicyProvider policy_provider;
  policy_provider.Reload();

  // No available policies.
  if (!policy_provider.device_policy_is_loaded()) {
    DEBUGD_ADD_ERROR(error, kPacketCaptureToolErrorString,
                     "No device policy available on this device, can't check "
                     "for packet capture policy setting.");
    return false;
  }

  const policy::DevicePolicy* policy = &policy_provider.GetDevicePolicy();
  bool packet_capture_allowed;
  if (!policy->GetDeviceDebugPacketCaptureAllowed(&packet_capture_allowed)) {
    // This means policy was not set in the device. Return true since the
    // default value of the policy is defined as true in the policy
    // documentation.
    return true;
  }
  return packet_capture_allowed;
}

}  // namespace

namespace debugd {

bool PacketCaptureTool::Start(const base::ScopedFD& status_fd,
                              const base::ScopedFD& output_fd,
                              const brillo::VariantDictionary& options,
                              std::string* out_id,
                              brillo::ErrorPtr* error) {
  if (!DevicePacketCaptureAllowedByPolicy(error)) {
    DEBUGD_ADD_ERROR(error, kPacketCaptureToolErrorString,
                     "Packet capture is not allowed by device policy.");
    return false;
  }
  std::string exec_path;
  if (!GetHelperPath("capture_utility.sh", &exec_path)) {
    DEBUGD_ADD_ERROR(error, kPacketCaptureToolErrorString,
                     "Helper path is too long");
    return false;
  }

  ProcessWithId* p =
      CreateProcess(false /* sandboxed */, false /* access_root_mount_ns */);
  if (!p) {
    DEBUGD_ADD_ERROR(error, kPacketCaptureToolErrorString,
                     "Failed to create helper process");
    return false;
  }
  p->AddArg(exec_path);
  if (!AddValidatedStringOption(p, options, "device", "--device", error))
    return false;
  if (!AddIntOption(p, options, "max_size", "--max-size", error))
    return false;
  if (!AddIntOption(p, options, "frequency", "--frequency", error))
    return false;
  if (!AddValidatedStringOption(p, options, "ht_location", "--ht-location",
                                error))
    return false;
  if (!AddValidatedStringOption(p, options, "vht_width", "--vht-width", error))
    return false;
  if (!AddValidatedStringOption(p, options, "monitor_connection_on",
                                "--monitor-connection-on", error))
    return false;

  // Pass the output fd of the pcap as a command line option to the child
  // process.
  int child_output_fd = STDERR_FILENO + 1;
  p->BindFd(output_fd.get(), child_output_fd);
  p->AddIntOption("--output-file", child_output_fd);

  p->BindFd(status_fd.get(), STDOUT_FILENO);
  p->BindFd(status_fd.get(), STDERR_FILENO);
  LOG(INFO) << "packet_capture: running process id: " << p->id();
  p->Start();
  *out_id = p->id();
  return true;
}

}  // namespace debugd
