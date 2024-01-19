// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/trunks_dbus_service.h"

#include <cstdint>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/time/time.h>

#include "trunks/dbus_interface.h"
#include "trunks/error_codes.h"
#include "trunks/resilience/write_error_tracker.h"
#include "trunks/trunks_interface.pb.h"
#include "trunks/trunks_metrics.h"

namespace trunks {

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::DBusMethodResponse;

TrunksDBusAdaptor::TrunksDBusAdaptor(scoped_refptr<dbus::Bus> bus,
                                     CommandTransceiver& command_transceiver,
                                     TrunksMetrics& metrics,
                                     WriteErrorTracker& write_error_tracker,
                                     TrunksDBusService& dbus_service)
    : org::chromium::TrunksAdaptor(this),
      dbus_object_(nullptr, bus, org::chromium::TrunksAdaptor::GetObjectPath()),
      command_transceiver_(command_transceiver),
      metrics_(metrics),
      write_error_tracker_(write_error_tracker),
      dbus_service_(dbus_service) {}

void TrunksDBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

void TrunksDBusAdaptor::SendCommandCallback(
    uint64_t sender,
    base::Time start_time,
    std::unique_ptr<DBusMethodResponse<SendCommandResponse>> response,
    const std::string& response_from_tpm) {
  metrics_.ReportCommandTime(sender, base::Time::Now() - start_time);
  SendCommandResponse tpm_response_proto;
  tpm_response_proto.set_response(response_from_tpm);
  response->Return(tpm_response_proto);
  if (write_error_tracker_.ShallTryRecover()) {
    // Note: we don't update the write errno in the file here, in case the
    // the service loop quits for some other reasons.
    LOG(INFO) << "Stopping service to try recovery from write error.";
    dbus_service_.Quit();
  }
}

void TrunksDBusAdaptor::SendCommand(
    std::unique_ptr<DBusMethodResponse<SendCommandResponse>> response,
    const SendCommandRequest& in_request) {
  if (!in_request.has_command() || in_request.command().empty()) {
    LOG(ERROR) << "TrunksDBusService: Invalid request.";
    SendCommandCallback(in_request.sender_id(), base::Time::Now(),
                        std::unique_ptr(std::move(response)),
                        CreateErrorResponse(SAPI_RC_BAD_PARAMETER));
    return;
  }
  command_transceiver_.SendCommandWithSender(
      in_request.command(), in_request.sender_id(),
      base::BindOnce(&TrunksDBusAdaptor::SendCommandCallback,
                     weak_factory_.GetWeakPtr(), in_request.sender_id(),
                     base::Time::Now(), std::unique_ptr(std::move(response))));
}

void TrunksDBusAdaptor::StartEvent(
    std::unique_ptr<DBusMethodResponse<StartEventResponse>> response,
    const StartEventRequest& in_request) {
  metrics_.StartEvent(in_request.event(), in_request.sender_id());
  response->Return(StartEventResponse{});
}

void TrunksDBusAdaptor::StopEvent(
    std::unique_ptr<DBusMethodResponse<StopEventResponse>> response,
    const StopEventRequest& in_request) {
  metrics_.StopEvent(in_request.event(), in_request.sender_id());
  response->Return(StopEventResponse{});
}

TrunksDBusService::TrunksDBusService(CommandTransceiver& command_transceiver,
                                     TrunksMetrics& metrics,
                                     WriteErrorTracker& write_error_tracker)
    : brillo::DBusServiceDaemon(kTrunksServiceName),
      command_transceiver_(command_transceiver),
      metrics_(metrics),
      write_error_tracker_(write_error_tracker) {}

void TrunksDBusService::RegisterDBusObjectsAsync(
    AsyncEventSequencer* sequencer) {
  adaptor_.reset(new TrunksDBusAdaptor(bus_, command_transceiver_, metrics_,
                                       write_error_tracker_, *this));

  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
  if (power_manager_) {
    power_manager_->Init(bus_);
  }
}

void TrunksDBusService::OnShutdown(int* exit_code) {
  if (power_manager_) {
    power_manager_->TearDown();
  }
  DBusServiceDaemon::OnShutdown(exit_code);
}

}  // namespace trunks
