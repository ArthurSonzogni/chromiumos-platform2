// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/hardware_verifier_client_impl.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <dbus/hardware_verifier/dbus-constants.h>
#include <hardware_verifier/hardware_verifier.pb.h>
#include <rmad/proto_bindings/rmad.pb.h>

#include "rmad/utils/component_utils.h"

namespace rmad {

HardwareVerifierClientImpl::HardwareVerifierClientImpl(
    const scoped_refptr<dbus::Bus>& bus) {
  proxy_ = bus->GetObjectProxy(
      hardware_verifier::kHardwareVerifierServiceName,
      dbus::ObjectPath(hardware_verifier::kHardwareVerifierServicePath));
}

bool HardwareVerifierClientImpl::GetHardwareVerificationResult(
    HardwareVerificationResult* result) const {
  dbus::MethodCall method_call(
      hardware_verifier::kHardwareVerifierInterfaceName,
      hardware_verifier::kVerifyComponentsMethod);

  std::unique_ptr<dbus::Response> response = proxy_->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response.get()) {
    LOG(ERROR) << "Failed to call hardware_verifier D-Bus service";
    return false;
  }

  hardware_verifier::VerifyComponentsReply reply;
  dbus::MessageReader reader(response.get());
  if (!reader.PopArrayOfBytesAsProto(&reply)) {
    LOG(ERROR) << "Failed to decode hardware_verifier protobuf response";
    return false;
  }
  if (reply.error() != hardware_verifier::ERROR_OK) {
    LOG(ERROR) << "hardware_verifier returns error code " << reply.error();
    return false;
  }

  const hardware_verifier::HwVerificationReport& report =
      reply.hw_verification_report();
  result->set_is_compliant(report.is_compliant());
  std::string error_str;
  for (int i = 0; i < report.found_component_infos_size(); ++i) {
    const hardware_verifier::ComponentInfo& info =
        report.found_component_infos(i);
    if (info.qualification_status() != hardware_verifier::QUALIFIED) {
      error_str += GetComponentIdentifier(info);
      error_str += "\n";
    }
  }
  result->set_error_str(error_str);
  return true;
}

}  // namespace rmad
