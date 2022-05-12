// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/common/types.h"

#include <string>

#include <base/notreached.h>

namespace {

constexpr char kUnknownString[] = "UNKNOWN";
constexpr char kRsuString[] = "RSU";
constexpr char kPhysicalAssembleDeviceString[] = "PHYSICAL_ASSEMBLE_DEVICE";
constexpr char kPhysicalKeepDeviceOpenString[] = "PHYSICAL_KEE_DEVICE_OPEN";
constexpr char kSkippedString[] = "SKIPPED";

}  // namespace

namespace rmad {

std::string WpDisableMethod_Name(WpDisableMethod method) {
  switch (method) {
    case WpDisableMethod::UNKNOWN:
      return kUnknownString;
    case WpDisableMethod::RSU:
      return kRsuString;
    case WpDisableMethod::PHYSICAL_ASSEMBLE_DEVICE:
      return kPhysicalAssembleDeviceString;
    case WpDisableMethod::PHYSICAL_KEEP_DEVICE_OPEN:
      return kPhysicalKeepDeviceOpenString;
    case WpDisableMethod::SKIPPED:
      return kSkippedString;
    default:
      break;
  }
  NOTREACHED();
  return "";
}

bool WpDisableMethod_Parse(const std::string& name, WpDisableMethod* method) {
  if (name == kUnknownString) {
    *method = WpDisableMethod::UNKNOWN;
    return true;
  } else if (name == kRsuString) {
    *method = WpDisableMethod::RSU;
    return true;
  } else if (name == kPhysicalAssembleDeviceString) {
    *method = WpDisableMethod::PHYSICAL_ASSEMBLE_DEVICE;
    return true;
  } else if (name == kPhysicalKeepDeviceOpenString) {
    *method = WpDisableMethod::PHYSICAL_KEEP_DEVICE_OPEN;
    return true;
  } else if (name == kSkippedString) {
    *method = WpDisableMethod::SKIPPED;
    return true;
  }
  return false;
}

}  // namespace rmad
