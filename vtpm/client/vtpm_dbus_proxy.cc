// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/client/vtpm_dbus_proxy.h"

#include <memory>
#include <utility>

#include <absl/strings/str_format.h>
#include <base/functional/bind.h>
#include <base/logging.h>

#include "trunks/error_codes.h"
#include "vtpm/dbus_interface.h"

namespace {

// Use a five minute timeout because some commands on some TPM hardware can take
// a very long time. If a few lengthy operations are already in the queue, a
// subsequent command needs to wait for all of them. Timeouts are always
// possible but under normal conditions 5 minutes seems to be plenty.
const int kDBusMaxTimeout = 5 * 60 * 1000;

}  // namespace

namespace vtpm {

VtpmDBusProxy::VtpmDBusProxy() : VtpmDBusProxy(/*bus=*/nullptr) {}

VtpmDBusProxy::VtpmDBusProxy(scoped_refptr<dbus::Bus> bus) : bus_(bus) {}

VtpmDBusProxy::~VtpmDBusProxy() {
  if (bus_) {
    bus_->ShutdownAndBlock();
  }
}

bool VtpmDBusProxy::Init() {
  origin_thread_id_ = base::PlatformThread::CurrentId();
  if (!bus_) {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = base::MakeRefCounted<dbus::Bus>(options);
  }
  if (!bus_->Connect()) {
    return false;
  }

  vtpm_proxy_ =
      std::make_unique<org::chromium::VtpmProxy>(bus_, kVtpmServiceName);

  if (!vtpm_proxy_) {
    return false;
  }
  base::TimeTicks deadline = base::TimeTicks::Now() + init_timeout_;
  while (!IsServiceReady(false /* force_check */) &&
         base::TimeTicks::Now() < deadline) {
    base::PlatformThread::Sleep(init_attempt_delay_);
  }
  return IsServiceReady(false /* force_check */);
}

bool VtpmDBusProxy::IsServiceReady(bool force_check) {
  if (!service_ready_ || force_check) {
    service_ready_ = CheckIfServiceReady();
  }
  return service_ready_;
}

bool VtpmDBusProxy::CheckIfServiceReady() {
  if (!bus_ || !vtpm_proxy_) {
    return false;
  }
  std::string owner = bus_->GetServiceOwnerAndBlock(kVtpmServiceName,
                                                    dbus::Bus::SUPPRESS_ERRORS);
  return !owner.empty();
}

void VtpmDBusProxy::SendCommand(const std::string& command,
                                ResponseCallback callback) {
  if (origin_thread_id_ != base::PlatformThread::CurrentId()) {
    LOG(ERROR) << "Error VtpmDBusProxy cannot be shared by multiple threads.";
    std::move(callback).Run(
        trunks::CreateErrorResponse(trunks::TRUNKS_RC_IPC_ERROR));
    return;
  }
  if (!IsServiceReady(false /* force_check */)) {
    LOG(ERROR) << "Error VtpmDBusProxy cannot connect to vtpmd.";
    std::move(callback).Run(
        trunks::CreateErrorResponse(trunks::SAPI_RC_NO_CONNECTION));
    return;
  }
  std::pair<ResponseCallback, ResponseCallback> split =
      base::SplitOnceCallback(std::move(callback));
  SendCommandRequest tpm_command_proto;
  tpm_command_proto.set_command(command);
  auto on_success = base::BindOnce(
      [](ResponseCallback callback, const SendCommandResponse& response) {
        std::move(callback).Run(response.response());
      },
      std::move(split.first));
  auto on_error = base::BindOnce(&VtpmDBusProxy::OnError, GetWeakPtr(),
                                 std::move(split.second));

  vtpm_proxy_->SendCommandAsync(tpm_command_proto, std::move(on_success),
                                std::move(on_error), kDBusMaxTimeout);
}

void VtpmDBusProxy::OnError(ResponseCallback callback, brillo::Error* error) {
  trunks::TPM_RC error_code = IsServiceReady(true /* force_check */)
                                  ? trunks::SAPI_RC_NO_RESPONSE_RECEIVED
                                  : trunks::SAPI_RC_NO_CONNECTION;
  std::move(callback).Run(trunks::CreateErrorResponse(error_code));
}

std::string VtpmDBusProxy::SendCommandAndWait(const std::string& command) {
  if (origin_thread_id_ != base::PlatformThread::CurrentId()) {
    LOG(ERROR) << "Error VtpmDBusProxy cannot be shared by multiple threads.";
    return trunks::CreateErrorResponse(trunks::TRUNKS_RC_IPC_ERROR);
  }
  if (!IsServiceReady(false /* force_check */)) {
    LOG(ERROR) << "Error VtpmDBusProxy cannot connect to vtpmd.";
    return trunks::CreateErrorResponse(trunks::SAPI_RC_NO_CONNECTION);
  }
  SendCommandRequest tpm_command_proto;
  tpm_command_proto.set_command(command);
  brillo::ErrorPtr error;
  SendCommandResponse tpm_response_proto;

  if (vtpm_proxy_->SendCommand(tpm_command_proto, &tpm_response_proto, &error,
                               kDBusMaxTimeout)) {
    return tpm_response_proto.response();
  } else {
    LOG(ERROR) << "VtpmProxy could not parse response: " << error->GetMessage();
    trunks::TPM_RC error_code;
    if (!IsServiceReady(true /* force_check */)) {
      error_code = trunks::SAPI_RC_NO_CONNECTION;
    } else {
      error_code = trunks::SAPI_RC_MALFORMED_RESPONSE;
    }
    return trunks::CreateErrorResponse(error_code);
  }
}

}  // namespace vtpm
