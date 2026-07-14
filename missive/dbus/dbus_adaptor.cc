// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/dbus/dbus_adaptor.h"

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>
#include <brillo/userdb_utils.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <featured/feature_library.h>

#include "missive/analytics/metrics.h"
#include "missive/missive/missive_service.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/util/reporting_errors.h"
#include "missive/util/status.h"

namespace reporting {

namespace {
// UMA names
static constexpr char kStartStatusUmaName[] = "Platform.Missive.StartStatus";

// Generic helper to construct an error response with a given status code
// and detailed error message, and log the associated error reason to UMA.
template <typename ResponseType, typename EnumType>
ResponseType RespondError(error::Code code,
                          const std::string& error_message,
                          const std::string& uma_name,
                          EnumType sample,
                          EnumType exclusive_max) {
  const auto res =
      analytics::Metrics::SendEnumToUMA(uma_name, sample, exclusive_max);
  LOG_IF(ERROR, !res) << "SendEnumToUMA failure, " << uma_name << " "
                      << static_cast<int>(sample);

  ResponseType response_body;
  auto* status = response_body.mutable_status();
  status->set_code(code);
  status->set_error_message(error_message);
  return response_body;
}

template <typename ResponseType>
ResponseType RespondDaemonNotReady() {
  return RespondError<ResponseType>(
      error::UNAVAILABLE, "The daemon is still starting.",
      kUmaUnavailableErrorReason, UnavailableErrorReason::DAEMON_STILL_STARTING,
      UnavailableErrorReason::MAX_VALUE);
}

EnqueueRecordResponse RespondPermissionDenied(Destination destination,
                                              uid_t uid) {
  LOG(ERROR) << "Unauthorized attempt to enqueue record for "
             << "security destination " << destination << " from UID "
             << (uid == static_cast<uid_t>(-1) ? "unknown"
                                               : std::to_string(uid));
  return RespondError<EnqueueRecordResponse>(
      error::PERMISSION_DENIED, "Unauthorized reporting destination.",
      kUmaPermissionDeniedErrorReason,
      PermissionDeniedErrorReason::SECURE_DESTINATION_FORBIDDEN,
      PermissionDeniedErrorReason::MAX_VALUE);
}

// Returns a map of destinations that require additional access controls
// (e.g. CROS_SECURITY_*) to the usernames authorized to send them.
const std::unordered_map<Destination, std::vector<std::string>>&
GetControlledDestinationMap() {
  static const base::NoDestructor<
      std::unordered_map<Destination, std::vector<std::string>>>
      kControlledDestinations({
          {Destination::CROS_SECURITY_PROCESS, {"secagentd"}},
          {Destination::CROS_SECURITY_NETWORK, {"secagentd"}},
          {Destination::CROS_SECURITY_FILE, {"secagentd"}},
          {Destination::CROS_SECURITY_AGENT, {"secagentd"}},
          {Destination::CROS_SECURITY_USER, {"secagentd"}},
      });
  return *kControlledDestinations;
}

bool IsControlledDestination(Destination destination) {
  const auto& controlled_map = GetControlledDestinationMap();
  return controlled_map.find(destination) != controlled_map.end();
}

bool IsAuthorizedUidForDestination(Destination destination, uid_t uid) {
  if (uid == 0) {
    return true;
  }
  const auto& controlled_map = GetControlledDestinationMap();
  const auto it = controlled_map.find(destination);
  if (it == controlled_map.end()) {
    // Missing Destinations are allowed for all.
    return true;
  }

  // Resolve and cache the UIDs of all allowed users
  static const base::NoDestructor<std::unordered_map<std::string, uid_t>>
      kAllowedUserUids([]() {
        std::unordered_map<std::string, uid_t> resolved_uids;
        for (const auto& [_, users] : GetControlledDestinationMap()) {
          for (const std::string& username : users) {
            if (resolved_uids.find(username) != resolved_uids.end()) {
              continue;
            }
            uid_t allowed_uid;
            if (brillo::userdb::GetUserInfo(username, &allowed_uid, nullptr)) {
              resolved_uids[username] = allowed_uid;
            } else {
              LOG(ERROR) << "Failed to find user: " << username;
              resolved_uids[username] = static_cast<uid_t>(-1);
            }
          }
        }
        return resolved_uids;
      }());

  for (const std::string& username : it->second) {
    const auto uid_it = kAllowedUserUids->find(username);
    if (uid_it != kAllowedUserUids->end() &&
        uid_it->second != static_cast<uid_t>(-1)) {
      if (uid == uid_it->second) {
        return true;
      }
    }
  }
  return false;
}
}  // namespace

DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus,
                         std::unique_ptr<MissiveService> missive,
                         base::OnceCallback<void(Status)> failure_cb)
    : org::chromium::MissivedAdaptor(this),
      dbus_object_(/*object_manager=*/nullptr,
                   bus,
                   org::chromium::MissivedAdaptor::GetObjectPath()),
      bus_(bus),
      dbus_proxy_(bus_->GetObjectProxy(
          "org.freedesktop.DBus", dbus::ObjectPath("/org/freedesktop/DBus"))),
      missive_(std::move(missive)) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(feature::PlatformFeatures::Initialize(bus));

  dbus_proxy_->ConnectToSignal(
      "org.freedesktop.DBus", "NameOwnerChanged",
      base::BindRepeating(&DBusAdaptor::OnNameOwnerChanged,
                          weak_ptr_factory_.GetWeakPtr()),
      base::DoNothing());

  missive_->StartUp(
      bus, feature::PlatformFeatures::Get(),
      base::BindPostTaskToCurrentDefault(base::BindOnce(
          &DBusAdaptor::StartupFinished, weak_ptr_factory_.GetWeakPtr(),
          Scoped<Status>(
              std::move(failure_cb),
              Status(error::UNAVAILABLE, "DBusAdaptor has been destructed")))));
}

void DBusAdaptor::StartupFinished(base::OnceCallback<void(Status)> failure_cb,
                                  Status status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto res = analytics::Metrics::SendEnumToUMA(
      /*name=*/kStartStatusUmaName, status.code(), error::Code::MAX_VALUE);
  LOG_IF(ERROR, !res) << "SendEnumToUMA failure, " << kStartStatusUmaName << " "
                      << static_cast<int>(status.code());
  if (!status.ok()) {
    if (failure_cb) {
      std::move(failure_cb).Run(status);
    }
    return;
  }
  daemon_is_ready_ = true;
  missive_->OnReady();
}

// static
void DBusAdaptor::OnFailure(Status status) {
  LOG(FATAL) << "Unable to start Missive daemon, status: " << status;
}

void DBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

void DBusAdaptor::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto status = missive_->ShutDown();
  if (!status.ok()) {
    LOG(FATAL) << "Failed to shutdown Missive daemon, status: " << status;
  }
  daemon_is_ready_ = false;
  missive_.reset();
}

void DBusAdaptor::EnqueueRecord(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        EnqueueRecordResponse>> out_response,
    dbus::Message* message,
    const EnqueueRecordRequest& in_request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!daemon_is_ready_) {
    out_response->Return(RespondDaemonNotReady<EnqueueRecordResponse>());
    return;
  }

  const auto destination = in_request.record().destination();
  if (!IsControlledDestination(destination)) {
    missive_->EnqueueRecord(in_request, std::move(out_response));
    return;
  }

  const std::string sender = message->GetSender();

  const auto it = sender_uids_.find(sender);
  if (it != sender_uids_.end()) {
    const uid_t uid = it->second;
    if (IsAuthorizedUidForDestination(destination, uid)) {
      missive_->EnqueueRecord(in_request, std::move(out_response));
    } else {
      out_response->Return(RespondPermissionDenied(destination, uid));
    }
    return;
  }

  auto pending_it = pending_lookups_.find(sender);
  if (pending_it != pending_lookups_.end()) {
    pending_it->second.push_back({std::move(out_response), in_request});
    return;
  }

  pending_lookups_[sender].push_back({std::move(out_response), in_request});

  dbus::MethodCall method_call("org.freedesktop.DBus", "GetConnectionUnixUser");
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(sender);

  dbus_proxy_->CallMethod(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
      base::BindOnce(&DBusAdaptor::OnGetConnectionUnixUser,
                     weak_ptr_factory_.GetWeakPtr(), sender));
}

void DBusAdaptor::OnGetConnectionUnixUser(const std::string& sender,
                                          dbus::Response* response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto pending_it = pending_lookups_.find(sender);
  if (pending_it == pending_lookups_.end()) {
    return;
  }
  auto pending_requests = std::move(pending_it->second);
  pending_lookups_.erase(pending_it);

  uid_t uid = static_cast<uid_t>(-1);
  bool lookup_succeeded = false;
  if (response) {
    dbus::MessageReader reader(response);
    uint32_t uint_uid;
    if (reader.PopUint32(&uint_uid)) {
      uid = static_cast<uid_t>(uint_uid);
      sender_uids_[sender] = uid;
      lookup_succeeded = true;
    } else {
      LOG(ERROR) << "Failed to pop UID from GetConnectionUnixUser response";
    }
  } else {
    LOG(ERROR) << "GetConnectionUnixUser failed: no response";
  }

  for (auto& pending : pending_requests) {
    if (!lookup_succeeded) {
      pending.response->Return(RespondError<EnqueueRecordResponse>(
          error::UNAVAILABLE, "Failed to verify sender identity. Please retry.",
          kUmaUnavailableErrorReason,
          UnavailableErrorReason::SENDER_UNIX_USER_LOOKUP_FAILED,
          UnavailableErrorReason::MAX_VALUE));
      continue;
    }

    const auto destination = pending.request.record().destination();
    if (IsAuthorizedUidForDestination(destination, uid)) {
      missive_->EnqueueRecord(pending.request, std::move(pending.response));
    } else {
      pending.response->Return(RespondPermissionDenied(destination, uid));
    }
  }
}

void DBusAdaptor::OnNameOwnerChanged(dbus::Signal* signal) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  dbus::MessageReader reader(signal);
  std::string name;
  std::string old_owner;
  std::string new_owner;
  if (reader.PopString(&name) && reader.PopString(&old_owner) &&
      reader.PopString(&new_owner)) {
    // If the new owner is empty, it means the connection has been
    // closed/released. Evict it from the cache to prevent potential UID
    // spoofing of recycled unique connection names and clean up any pending
    // lookups.
    if (new_owner.empty()) {
      sender_uids_.erase(name);
      pending_lookups_.erase(name);
    }
  }
}

void DBusAdaptor::FlushPriority(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        FlushPriorityResponse>> out_response,
    const FlushPriorityRequest& in_request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!daemon_is_ready_) {
    out_response->Return(RespondDaemonNotReady<FlushPriorityResponse>());
    return;
  }
  missive_->FlushPriority(in_request, std::move(out_response));
}

void DBusAdaptor::ConfirmRecordUpload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        ConfirmRecordUploadResponse>> out_response,
    const ConfirmRecordUploadRequest& in_request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConfirmRecordUploadResponse response_body;
  if (!daemon_is_ready_) {
    out_response->Return(RespondDaemonNotReady<ConfirmRecordUploadResponse>());
    return;
  }
  missive_->ConfirmRecordUpload(in_request, std::move(out_response));
}

void DBusAdaptor::UpdateConfigInMissive(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        UpdateConfigInMissiveResponse>> out_response,
    const UpdateConfigInMissiveRequest& in_request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  UpdateConfigInMissiveResponse response_body;
  if (!daemon_is_ready_) {
    out_response->Return(
        RespondDaemonNotReady<UpdateConfigInMissiveResponse>());
    return;
  }
  missive_->UpdateConfigInMissive(in_request, std::move(out_response));
}

void DBusAdaptor::UpdateEncryptionKey(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        UpdateEncryptionKeyResponse>> out_response,
    const UpdateEncryptionKeyRequest& in_request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  UpdateEncryptionKeyResponse response_body;
  if (!daemon_is_ready_) {
    out_response->Return(RespondDaemonNotReady<UpdateEncryptionKeyResponse>());
    return;
  }
  missive_->UpdateEncryptionKey(in_request, std::move(out_response));
}
}  // namespace reporting
