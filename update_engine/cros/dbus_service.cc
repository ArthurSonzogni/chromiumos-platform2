//
// Copyright (C) 2012 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/cros/dbus_service.h"

#include <string>
#include <utility>
#include <vector>

#include <update_engine/dbus-constants.h>

#include "update_engine/cros/dbus_connection.h"
#include "update_engine/proto_bindings/update_engine.pb.h"
#include "update_engine/update_status_utils.h"

namespace chromeos_update_engine {

using brillo::ErrorPtr;
using chromeos_update_engine::UpdateEngineService;
using std::string;
using std::vector;
using update_engine::Operation;
using update_engine::StatusResult;
using update_engine::UpdateEngineStatus;
using update_engine::UpdateUrgency;

namespace {
// Converts the internal |UpdateEngineStatus| to the protobuf |StatusResult|.
void ConvertToStatusResult(const UpdateEngineStatus& ue_status,
                           StatusResult* out_status) {
  out_status->set_last_checked_time(ue_status.last_checked_time);
  out_status->set_progress(ue_status.progress);
  out_status->set_current_operation(static_cast<Operation>(ue_status.status));
  out_status->set_new_version(ue_status.new_version);
  out_status->set_new_size(ue_status.new_size_bytes);
  out_status->set_is_enterprise_rollback(ue_status.is_enterprise_rollback);
  out_status->set_is_install(ue_status.is_install);
  out_status->set_eol_date(ue_status.eol_date);
  out_status->set_will_powerwash_after_reboot(
      ue_status.will_powerwash_after_reboot);
  out_status->set_last_attempt_error(ue_status.last_attempt_error);
  out_status->set_update_urgency(
      static_cast<UpdateUrgency>(ue_status.update_urgency_internal));
  for (const auto& feature : ue_status.features) {
    auto* out_feature = out_status->add_features();
    out_feature->set_name(feature.name);
    out_feature->set_enabled(feature.enabled);
  }
  out_status->set_is_interactive(ue_status.is_interactive);
  out_status->set_will_defer_update(ue_status.will_defer_update);
}
}  // namespace

DBusUpdateEngineService::DBusUpdateEngineService()
    : common_(new UpdateEngineService()) {}

// org::chromium::UpdateEngineInterfaceInterface methods implementation.
bool DBusUpdateEngineService::Update(
    ErrorPtr* error, const update_engine::UpdateParams& in_update_params) {
  bool result;
  return common_->Update(error, in_update_params, &result);
}

bool DBusUpdateEngineService::ApplyDeferredUpdate(ErrorPtr* error) {
  return common_->ApplyDeferredUpdate(error, /*shutdown=*/false);
}

bool DBusUpdateEngineService::ApplyDeferredUpdateAdvanced(
    ErrorPtr* error, const update_engine::ApplyUpdateConfig& config) {
  return common_->ApplyDeferredUpdate(
      error, config.done_action() == update_engine::UpdateDoneAction::SHUTDOWN);
}

bool DBusUpdateEngineService::AttemptInstall(ErrorPtr* error,
                                             const string& in_omaha_url,
                                             const vector<string>& dlc_ids) {
  return common_->AttemptInstall(error, in_omaha_url, dlc_ids);
}

bool DBusUpdateEngineService::Install(
    ErrorPtr* error, const update_engine::InstallParams& install_params) {
  return common_->Install(error, install_params);
}

bool DBusUpdateEngineService::AttemptRollback(ErrorPtr* error,
                                              bool in_powerwash) {
  return common_->AttemptRollback(error, in_powerwash);
}

bool DBusUpdateEngineService::CanRollback(ErrorPtr* error,
                                          bool* out_can_rollback) {
  return common_->CanRollback(error, out_can_rollback);
}

bool DBusUpdateEngineService::ResetStatus(ErrorPtr* error) {
  return common_->ResetStatus(error);
}

bool DBusUpdateEngineService::SetDlcActiveValue(brillo::ErrorPtr* error,
                                                bool is_active,
                                                const string& dlc_id) {
  return common_->SetDlcActiveValue(error, is_active, dlc_id);
}

bool DBusUpdateEngineService::GetStatusAdvanced(ErrorPtr* error,
                                                StatusResult* out_status) {
  UpdateEngineStatus status;
  if (!common_->GetStatus(error, &status)) {
    return false;
  }

  ConvertToStatusResult(status, out_status);
  return true;
}

bool DBusUpdateEngineService::SetStatus(ErrorPtr* error,
                                        int32_t update_status) {
  if (update_status < 0 ||
      update_status > static_cast<int32_t>(update_engine::UpdateStatus::MAX)) {
    LOG(ERROR) << "Passed value is not a valid update state.";
    return false;
  }
  return common_->SetStatus(
      error, static_cast<update_engine::UpdateStatus>(update_status));
}

bool DBusUpdateEngineService::RebootIfNeeded(ErrorPtr* error) {
  return common_->RebootIfNeeded(error);
}

bool DBusUpdateEngineService::SetChannel(ErrorPtr* error,
                                         const string& in_target_channel,
                                         bool in_is_powerwash_allowed) {
  return common_->SetChannel(error, in_target_channel, in_is_powerwash_allowed);
}

bool DBusUpdateEngineService::GetChannel(ErrorPtr* error,
                                         bool in_get_current_channel,
                                         string* out_channel) {
  return common_->GetChannel(error, in_get_current_channel, out_channel);
}

bool DBusUpdateEngineService::GetCohortHint(ErrorPtr* error,
                                            string* out_cohort_hint) {
  return common_->GetCohortHint(error, out_cohort_hint);
}

bool DBusUpdateEngineService::SetCohortHint(ErrorPtr* error,
                                            const string& in_cohort_hint) {
  return common_->SetCohortHint(error, in_cohort_hint);
}

bool DBusUpdateEngineService::SetP2PUpdatePermission(ErrorPtr* error,
                                                     bool in_enabled) {
  return common_->SetP2PUpdatePermission(error, in_enabled);
}

bool DBusUpdateEngineService::GetP2PUpdatePermission(ErrorPtr* error,
                                                     bool* out_enabled) {
  return common_->GetP2PUpdatePermission(error, out_enabled);
}

bool DBusUpdateEngineService::SetUpdateOverCellularPermission(ErrorPtr* error,
                                                              bool in_allowed) {
  return common_->SetUpdateOverCellularPermission(error, in_allowed);
}

bool DBusUpdateEngineService::SetUpdateOverCellularTarget(
    brillo::ErrorPtr* error,
    const std::string& target_version,
    int64_t target_size) {
  return common_->SetUpdateOverCellularTarget(error, target_version,
                                              target_size);
}

bool DBusUpdateEngineService::GetUpdateOverCellularPermission(
    ErrorPtr* error, bool* out_allowed) {
  return common_->GetUpdateOverCellularPermission(error, out_allowed);
}

bool DBusUpdateEngineService::ToggleFeature(brillo::ErrorPtr* error,
                                            const string& feature,
                                            bool enable) {
  return common_->ToggleFeature(error, feature, enable);
}

bool DBusUpdateEngineService::IsFeatureEnabled(brillo::ErrorPtr* error,
                                               const string& feature,
                                               bool* out_enabled) {
  return common_->IsFeatureEnabled(error, feature, out_enabled);
}

bool DBusUpdateEngineService::GetDurationSinceUpdate(
    ErrorPtr* error, int64_t* out_usec_wallclock) {
  return common_->GetDurationSinceUpdate(error, out_usec_wallclock);
}

bool DBusUpdateEngineService::GetPrevVersion(ErrorPtr* error,
                                             string* out_prev_version) {
  return common_->GetPrevVersion(error, out_prev_version);
}

bool DBusUpdateEngineService::GetRollbackPartition(
    ErrorPtr* error, string* out_rollback_partition_name) {
  return common_->GetRollbackPartition(error, out_rollback_partition_name);
}

bool DBusUpdateEngineService::GetLastAttemptError(
    ErrorPtr* error, int32_t* out_last_attempt_error) {
  return common_->GetLastAttemptError(error, out_last_attempt_error);
}

UpdateEngineAdaptor::UpdateEngineAdaptor()
    : org::chromium::UpdateEngineInterfaceAdaptor(&dbus_service_),
      bus_(DBusConnection::Get()->GetDBus()),
      dbus_service_(),
      dbus_object_(nullptr,
                   bus_,
                   dbus::ObjectPath(update_engine::kUpdateEngineServicePath)) {}

void UpdateEngineAdaptor::RegisterAsync(
    base::OnceCallback<void(bool)> completion_callback) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(completion_callback));
}

bool UpdateEngineAdaptor::RequestOwnership() {
  return bus_->RequestOwnershipAndBlock(update_engine::kUpdateEngineServiceName,
                                        dbus::Bus::REQUIRE_PRIMARY);
}

void UpdateEngineAdaptor::SendStatusUpdate(
    const UpdateEngineStatus& update_engine_status) {
  StatusResult status;
  ConvertToStatusResult(update_engine_status, &status);

  // Send |StatusUpdateAdvanced| signal.
  SendStatusUpdateAdvancedSignal(status);
}

}  // namespace chromeos_update_engine
