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

#include "update_engine/cros/omaha_request_action.h"

#include <inttypes.h>

#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/key_value_store.h>
#include <metrics/metrics_library.h>
#include <policy/libpolicy.h>

#include "update_engine/common/action_pipe.h"
#include "update_engine/common/constants.h"
#include "update_engine/common/hardware_interface.h"
#include "update_engine/common/hash_calculator.h"
#include "update_engine/common/metrics_reporter_interface.h"
#include "update_engine/common/platform_constants.h"
#include "update_engine/common/prefs.h"
#include "update_engine/common/prefs_interface.h"
#include "update_engine/common/system_state.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/connection_manager_interface.h"
#include "update_engine/cros/omaha_parser_data.h"
#include "update_engine/cros/omaha_parser_xml.h"
#include "update_engine/cros/omaha_request_builder_xml.h"
#include "update_engine/cros/omaha_request_params.h"
#include "update_engine/cros/p2p_manager.h"
#include "update_engine/cros/payload_state_interface.h"
#include "update_engine/cros/update_attempter.h"
#include "update_engine/metrics_utils.h"

using base::Time;
using base::TimeDelta;
using chromeos_update_manager::kRollforwardInfinity;
using std::string;

namespace chromeos_update_engine {
namespace {

constexpr char kCriticalAppVersion[] = "ForcedUpdate";

// Parses |str| and returns |true| iff its value is "true".
bool ParseBool(const string& str) {
  return str == "true";
}
}  // namespace

OmahaRequestAction::OmahaRequestAction(
    OmahaEvent* event,
    std::unique_ptr<HttpFetcher> http_fetcher,
    bool ping_only,
    const string& session_id)
    : event_(event),
      http_fetcher_(std::move(http_fetcher)),
      ping_only_(ping_only),
      ping_active_days_(0),
      ping_roll_call_days_(0),
      session_id_(session_id) {}

OmahaRequestAction::~OmahaRequestAction() {}

// Calculates the value to use for the ping days parameter.
int OmahaRequestAction::CalculatePingDays(const string& key) {
  int days = kPingNeverPinged;
  int64_t last_ping = 0;
  if (SystemState::Get()->prefs()->GetInt64(key, &last_ping) &&
      last_ping >= 0) {
    days = (Time::Now() - Time::FromInternalValue(last_ping)).InDays();
    if (days < 0) {
      // If |days| is negative, then the system clock must have jumped
      // back in time since the ping was sent. Mark the value so that
      // it doesn't get sent to the server but we still update the
      // last ping daystart preference. This way the next ping time
      // will be correct, hopefully.
      days = kPingTimeJump;
      LOG(WARNING)
          << "System clock jumped back in time. Resetting ping daystarts.";
    }
  }
  return days;
}

void OmahaRequestAction::InitPingDays() {
  // We send pings only along with update checks, not with events.
  if (IsEvent()) {
    return;
  }
  // TODO(petkov): Figure a way to distinguish active use pings
  // vs. roll call pings. Currently, the two pings are identical. A
  // fix needs to change this code as well as UpdateLastPingDays and ShouldPing.
  ping_active_days_ = CalculatePingDays(kPrefsLastActivePingDay);
  ping_roll_call_days_ = CalculatePingDays(kPrefsLastRollCallPingDay);
}

bool OmahaRequestAction::ShouldPing() const {
  if (ping_active_days_ == kPingNeverPinged &&
      ping_roll_call_days_ == kPingNeverPinged) {
    int powerwash_count = SystemState::Get()->hardware()->GetPowerwashCount();
    if (powerwash_count > 0) {
      LOG(INFO) << "Not sending ping with a=-1 r=-1 to omaha because "
                << "powerwash_count is " << powerwash_count;
      return false;
    }
    if (SystemState::Get()->hardware()->GetFirstActiveOmahaPingSent()) {
      LOG(INFO) << "Not sending ping with a=-1 r=-1 to omaha because "
                << "the first_active_omaha_ping_sent is true.";
      return false;
    }
    return true;
  }
  return ping_active_days_ > 0 || ping_roll_call_days_ > 0;
}

// static
int OmahaRequestAction::GetInstallDate() {
  auto* prefs = SystemState::Get()->prefs();
  // If we have the value stored on disk, just return it.
  int64_t stored_value;
  if (prefs->GetInt64(kPrefsInstallDateDays, &stored_value)) {
    // Convert and validity-check.
    int install_date_days = static_cast<int>(stored_value);
    if (install_date_days >= 0)
      return install_date_days;
    LOG(ERROR) << "Dropping stored Omaha InstallData since its value num_days="
               << install_date_days << " looks suspicious.";
    prefs->Delete(kPrefsInstallDateDays);
  }

  // Otherwise, if OOBE is not complete then do nothing and wait for
  // ParseResponse() to call ParseInstallDate() and then
  // PersistInstallDate() to set the kPrefsInstallDateDays state
  // variable. Once that is done, we'll then report back in future
  // Omaha requests.  This works exactly because OOBE triggers an
  // update check.
  //
  // However, if OOBE is complete and the kPrefsInstallDateDays state
  // variable is not set, there are two possibilities
  //
  //   1. The update check in OOBE failed so we never got a response
  //      from Omaha (no network etc.); or
  //
  //   2. OOBE was done on an older version that didn't write to the
  //      kPrefsInstallDateDays state variable.
  //
  // In both cases, we approximate the install date by simply
  // inspecting the timestamp of when OOBE happened.

  Time time_of_oobe;
  if (!SystemState::Get()->hardware()->IsOOBEEnabled() ||
      !SystemState::Get()->hardware()->IsOOBEComplete(&time_of_oobe)) {
    LOG(INFO) << "Not generating Omaha InstallData as we have "
              << "no prefs file and OOBE is not complete or not enabled.";
    return -1;
  }

  int num_days;
  if (!utils::ConvertToOmahaInstallDate(time_of_oobe, &num_days)) {
    LOG(ERROR) << "Not generating Omaha InstallData from time of OOBE "
               << "as its value '" << utils::ToString(time_of_oobe)
               << "' looks suspicious.";
    return -1;
  }

  // Persist this to disk, for future use.
  if (!OmahaRequestAction::PersistInstallDate(num_days,
                                              kProvisionedFromOOBEMarker))
    return -1;

  LOG(INFO) << "Set the Omaha InstallDate from OOBE time-stamp to " << num_days
            << " days.";

  return num_days;
}

void OmahaRequestAction::StorePingReply() const {
  const auto* params = SystemState::Get()->request_params();
  for (const auto& app : parser_data_.apps) {
    auto it = params->dlc_apps_params().find(app.id);
    if (it == params->dlc_apps_params().end())
      continue;

    const OmahaRequestParams::AppParams& dlc_params = it->second;
    const string& dlc_id = dlc_params.name;
    // Skip if the ping for this DLC was not sent.
    if (!dlc_params.send_ping)
      continue;

    auto* prefs = SystemState::Get()->prefs();
    // Reset the active metadata value to |kPingInactiveValue|.
    auto active_key =
        prefs->CreateSubKey({kDlcPrefsSubDir, dlc_id, kPrefsPingActive});
    if (!prefs->SetInt64(active_key, kPingInactiveValue))
      LOG(ERROR) << "Failed to set the value of ping metadata '" << active_key
                 << "'.";

    auto last_rollcall_key =
        prefs->CreateSubKey({kDlcPrefsSubDir, dlc_id, kPrefsPingLastRollcall});
    if (!prefs->SetString(last_rollcall_key,
                          parser_data_.daystart.elapsed_days))
      LOG(ERROR) << "Failed to set the value of ping metadata '"
                 << last_rollcall_key << "'.";

    if (dlc_params.ping_active) {
      // Write the value of elapsed_days into |kPrefsPingLastActive| only if
      // the previous ping was an active one.
      auto last_active_key =
          prefs->CreateSubKey({kDlcPrefsSubDir, dlc_id, kPrefsPingLastActive});
      if (!prefs->SetString(last_active_key,
                            parser_data_.daystart.elapsed_days))
        LOG(ERROR) << "Failed to set the value of ping metadata '"
                   << last_active_key << "'.";
    }
  }
}

void OmahaRequestAction::PerformAction() {
  http_fetcher_->set_delegate(this);
  InitPingDays();
  if (ping_only_ && !ShouldPing()) {
    processor_->ActionComplete(this, ErrorCode::kSuccess);
    return;
  }

  OmahaRequestBuilderXml omaha_request(event_.get(),
                                       ping_only_,
                                       ShouldPing(),  // include_ping
                                       ping_active_days_,
                                       ping_roll_call_days_,
                                       GetInstallDate(),
                                       session_id_);
  string request_post = omaha_request.GetRequest();

  // Set X-Goog-Update headers.
  const auto* params = SystemState::Get()->request_params();
  http_fetcher_->SetHeader(kXGoogleUpdateInteractivity,
                           params->interactive() ? "fg" : "bg");
  http_fetcher_->SetHeader(kXGoogleUpdateAppId, params->GetAppId());
  http_fetcher_->SetHeader(
      kXGoogleUpdateUpdater,
      base::StringPrintf(
          "%s-%s", constants::kOmahaUpdaterID, kOmahaUpdaterVersion));

  http_fetcher_->SetPostData(
      request_post.data(), request_post.size(), kHttpContentTypeTextXml);
  LOG(INFO) << "Posting an Omaha request to " << params->update_url();
  LOG(INFO) << "Request: " << request_post;
  http_fetcher_->BeginTransfer(params->update_url());
}

void OmahaRequestAction::TerminateProcessing() {
  http_fetcher_->TerminateTransfer();
}

// We just store the response in the buffer. Once we've received all bytes,
// we'll look in the buffer and decide what to do.
bool OmahaRequestAction::ReceivedBytes(HttpFetcher* fetcher,
                                       const void* bytes,
                                       size_t length) {
  const uint8_t* byte_ptr = reinterpret_cast<const uint8_t*>(bytes);
  response_buffer_.insert(response_buffer_.end(), byte_ptr, byte_ptr + length);
  return true;
}

bool OmahaRequestAction::UpdateLastPingDays() {
  int64_t elapsed_seconds = 0;
  TEST_AND_RETURN_FALSE(base::StringToInt64(
      parser_data_.daystart.elapsed_seconds, &elapsed_seconds));
  TEST_AND_RETURN_FALSE(elapsed_seconds >= 0);

  // Remember the local time that matches the server's last midnight
  // time.
  auto* prefs = SystemState::Get()->prefs();
  Time daystart = Time::Now() - base::Seconds(elapsed_seconds);
  prefs->SetInt64(kPrefsLastActivePingDay, daystart.ToInternalValue());
  prefs->SetInt64(kPrefsLastRollCallPingDay, daystart.ToInternalValue());
  return true;
}

bool OmahaRequestAction::ParsePackage(OmahaParserData::App* app,
                                      bool can_exclude,
                                      ScopedActionCompleter* completer) {
  if (app->updatecheck.status.empty() ||
      app->updatecheck.status == kValNoUpdate) {
    if (!app->packages.empty()) {
      LOG(ERROR) << "No update in this <app> but <package> is not empty.";
      completer->set_code(ErrorCode::kOmahaResponseInvalid);
      return false;
    }
    return true;
  }
  if (app->packages.empty()) {
    LOG(ERROR) << "Omaha Response has no packages.";
    completer->set_code(ErrorCode::kOmahaResponseInvalid);
    return false;
  }
  if (app->urls.empty()) {
    LOG(ERROR) << "No Omaha Response URLs.";
    completer->set_code(ErrorCode::kOmahaResponseInvalid);
    return false;
  }
  for (size_t i = 0; i < app->packages.size(); i++) {
    const auto& package = app->packages[i];
    if (package.name.empty()) {
      LOG(ERROR) << "Omaha Response has empty package name.";
      completer->set_code(ErrorCode::kOmahaResponseInvalid);
      return false;
    }

    OmahaResponse::Package out_package;
    out_package.app_id = app->id;
    out_package.can_exclude = can_exclude;
    for (const auto& url : app->urls) {
      if (url.codebase.empty()) {
        LOG(ERROR) << "Omaha Response URL has empty codebase.";
        completer->set_code(ErrorCode::kOmahaResponseInvalid);
        return false;
      }
      out_package.payload_urls.push_back(url.codebase + package.name);
    }

    base::StringToUint64(package.size, &out_package.size);
    if (out_package.size <= 0) {
      LOG(ERROR) << "Omaha Response has invalid payload size: " << package.size;
      completer->set_code(ErrorCode::kOmahaResponseInvalid);
      return false;
    }

    if (i < app->postinstall_action->metadata_sizes.size())
      base::StringToUint64(app->postinstall_action->metadata_sizes[i],
                           &out_package.metadata_size);

    if (i < app->postinstall_action->metadata_signature_rsas.size())
      out_package.metadata_signature =
          app->postinstall_action->metadata_signature_rsas[i];

    out_package.hash = package.hash;
    if (out_package.hash.empty()) {
      LOG(ERROR) << "Omaha Response has empty hash_sha256 value.";
      completer->set_code(ErrorCode::kOmahaResponseInvalid);
      return false;
    }

    out_package.fp = package.fp;

    if (i < app->postinstall_action->is_delta_payloads.size())
      out_package.is_delta =
          ParseBool(app->postinstall_action->is_delta_payloads[i]);

    response_.packages.push_back(std::move(out_package));
  }

  return true;
}

void OmahaRequestAction::ProcessExclusions(OmahaRequestParams* params,
                                           ExcluderInterface* excluder) {
  for (auto package_it = response_.packages.begin();
       package_it != response_.packages.end();
       /* Increment logic in loop */) {
    // If package cannot be excluded, quickly continue.
    if (!package_it->can_exclude) {
      ++package_it;
      continue;
    }
    // Remove the excluded payload URLs.
    for (auto payload_url_it = package_it->payload_urls.begin();
         payload_url_it != package_it->payload_urls.end();
         /* Increment logic in loop */) {
      auto exclusion_name = utils::GetExclusionName(*payload_url_it);
      // If payload URL is not excluded, quickly continue.
      if (!excluder->IsExcluded(exclusion_name)) {
        ++payload_url_it;
        continue;
      }
      LOG(INFO) << "Excluding payload URL=" << *payload_url_it
                << " for payload hash=" << package_it->hash;
      payload_url_it = package_it->payload_urls.erase(payload_url_it);
    }
    // If there are no candidate payload URLs, remove the package.
    if (package_it->payload_urls.empty()) {
      LOG(INFO) << "Excluding payload hash=" << package_it->hash;
      // Need to set DLC as not updated so correct metrics can be sent when an
      // update is completed.
      if (params->IsDlcAppId(package_it->app_id)) {
        params->SetDlcNoUpdate(package_it->app_id);
      } else if (params->IsMiniOSAppId(package_it->app_id)) {
        params->SetMiniOSUpdate(false);
      }
      package_it = response_.packages.erase(package_it);
      continue;
    }
    ++package_it;
  }
}

void OmahaRequestAction::ParseRollbackVersions(
    const OmahaParserData::App& platform_app, int allowed_milestones) {
  // Defaults to false if attribute is not present.
  response_.is_rollback = ParseBool(platform_app.updatecheck.rollback);

  utils::ParseRollbackKeyVersion(platform_app.updatecheck.firmware_version,
                                 &response_.rollback_key_version.firmware_key,
                                 &response_.rollback_key_version.firmware);
  utils::ParseRollbackKeyVersion(platform_app.updatecheck.kernel_version,
                                 &response_.rollback_key_version.kernel_key,
                                 &response_.rollback_key_version.kernel);

  string firmware_version = platform_app.updatecheck.past_firmware_version;
  string kernel_version = platform_app.updatecheck.past_kernel_version;

  LOG(INFO) << "For milestone N-" << allowed_milestones
            << " firmware_key_version=" << firmware_version
            << " kernel_key_version=" << kernel_version;

  OmahaResponse::RollbackKeyVersion version;
  utils::ParseRollbackKeyVersion(
      firmware_version, &version.firmware_key, &version.firmware);
  utils::ParseRollbackKeyVersion(
      kernel_version, &version.kernel_key, &version.kernel);

  response_.past_rollback_key_version = std::move(version);
}

void OmahaRequestAction::PersistEolInfo(
    const OmahaParserData::App& platform_app) {
  // If EOL date attribute is not sent, don't delete the old persisted EOL
  // date information.
  if (!platform_app.updatecheck.eol_date.empty() &&
      !SystemState::Get()->prefs()->SetString(
          kPrefsOmahaEolDate, platform_app.updatecheck.eol_date)) {
    LOG(ERROR) << "Setting EOL date failed.";
  }
}

void OmahaRequestAction::PersistDisableMarketSegment(const string& value) {
  auto prefs = SystemState::Get()->prefs();
  if (ParseBool(value)) {
    if (!prefs->Exists(kPrefsMarketSegmentDisabled))
      if (!prefs->SetBoolean(kPrefsMarketSegmentDisabled, true))
        LOG(ERROR) << "Failed to disable sending market segment info.";
  } else {
    // Normally the pref doesn't exist, so this becomes just a no-op;
    prefs->Delete(kPrefsMarketSegmentDisabled);
  }
}

bool OmahaRequestAction::ParseResponse(ScopedActionCompleter* completer) {
  if (parser_data_.apps.empty()) {
    completer->set_code(ErrorCode::kOmahaResponseInvalid);
    return false;
  }

  // Locate the platform App since it's an important one that has specific
  // information attached to it that may not be available from other Apps.
  const auto* params = SystemState::Get()->request_params();
  auto platform_app = std::find_if(parser_data_.apps.begin(),
                                   parser_data_.apps.end(),
                                   [&params](const OmahaParserData::App& app) {
                                     return app.id == params->GetAppId();
                                   });
  if (platform_app == parser_data_.apps.end()) {
    LOG(WARNING) << "Platform App is missing.";
  } else {
    // chromium-os:37289: The PollInterval is not supported by Omaha server
    // currently.  But still keeping this existing code in case we ever decide
    // to slow down the request rate from the server-side. Note that the
    // PollInterval is not persisted, so it has to be sent by the server on
    // every response to guarantee that the scheduler uses this value
    // (otherwise, if the device got rebooted after the last server-indicated
    // value, it'll revert to the default value). Also kDefaultMaxUpdateChecks
    // value for the scattering logic is based on the assumption that we perform
    // an update check every hour so that the max value of 8 will roughly be
    // equivalent to one work day. If we decide to use PollInterval permanently,
    // we should update the max_update_checks_allowed to take PollInterval into
    // account.  Note: The parsing for PollInterval happens even before parsing
    // of the status because we may want to specify the PollInterval even when
    // there's no update.
    base::StringToInt(platform_app->updatecheck.poll_interval,
                      &response_.poll_interval);

    PersistEolInfo(*platform_app);

    // Parses the rollback versions of the current image. If the fields do not
    // exist they default to 0xffff for the 4 key versions.
    ParseRollbackVersions(*platform_app, params->rollback_allowed_milestones());

    PersistDisableMarketSegment(
        platform_app->updatecheck.disable_market_segment);

    response_.invalidate_last_update =
        ParseBool(platform_app->updatecheck.invalidate_last_update);
  }

  // Check for the "elapsed_days" attribute in the "daystart"
  // element. This is the number of days since Jan 1 2007, 0:00
  // PST. If we don't have a persisted value of the Omaha InstallDate,
  // we'll use it to calculate it and then persist it.
  if (ParseInstallDate() && !HasInstallDate()) {
    // Since response_.install_date_days is never negative, the
    // elapsed_days -> install-date calculation is reduced to simply
    // rounding down to the nearest number divisible by 7.
    int remainder = response_.install_date_days % 7;
    int install_date_days_rounded = response_.install_date_days - remainder;
    if (PersistInstallDate(install_date_days_rounded,
                           kProvisionedFromOmahaResponse)) {
      LOG(INFO) << "Set the Omaha InstallDate from Omaha Response to "
                << install_date_days_rounded << " days.";
    }
  }

  // We persist the cohorts sent by omaha even if the status is "noupdate".
  PersistCohorts();

  if (!ParseStatus(completer))
    return false;

  if (!ParseParams(completer))
    return false;

  // Package has to be parsed after Params now because ParseParams need to make
  // sure that postinstall action exists.
  for (auto& app : parser_data_.apps) {
    // Only allow exclusions for a non-critical package during an update. For
    // non-critical package installations, let the errors propagate instead
    // of being handled inside update_engine as installations are a dlcservice
    // specific feature.
    bool can_exclude =
        (!params->is_install() && params->IsDlcAppId(app.id) &&
         !params->dlc_apps_params().at(app.id).critical_update) ||
        params->IsMiniOSAppId(app.id);
    if (!ParsePackage(&app, can_exclude, completer))
      return false;
  }

  return true;
}

bool OmahaRequestAction::ParseStatus(ScopedActionCompleter* completer) {
  response_.update_exists = false;
  auto* params = SystemState::Get()->request_params();
  for (const auto& app : parser_data_.apps) {
    const string& status = app.updatecheck.status;
    if (status == kValNoUpdate) {
      // If the app is a DLC, allow status "noupdate" to support DLC
      // deprecations.
      if (params->IsDlcAppId(app.id)) {
        LOG(INFO) << "No update for App " << app.id
                  << " but update continuing since a DLC.";
        params->SetDlcNoUpdate(app.id);
        continue;
      } else if (params->IsMiniOSAppId(app.id)) {
        // Platform updates can happen even when MiniOS is "noupdate" so do not
        // modify `update_exists`.
        LOG(INFO) << "Ignoring noupdate for MiniOS App ID: " << app.id;
        params->SetMiniOSUpdate(false);
        continue;
      }
      // Don't update if any app has status="noupdate".
      LOG(INFO) << "No update for App " << app.id;
      LOG(INFO) << "Reason for no update: " << app.updatecheck.no_update_reason;
      response_.no_update_reason = app.updatecheck.no_update_reason;
      response_.update_exists = false;
      break;
    } else if (status == "ok") {
      if (ParseBool(app.postinstall_action->no_update)) {
        // noupdate="true" in postinstall attributes means it's an update to
        // self, only update if there's at least one app really have update.
        LOG(INFO) << "Update to self for App " << app.id;
      } else {
        response_.update_exists = true;
      }
    } else if (status.empty() && params->is_install() &&
               params->GetAppId() == app.id) {
      // Skips the platform app for install operation.
      LOG(INFO) << "No payload (and ignore) for App " << app.id;
    } else if (status.empty() && params->IsMiniOSAppId(app.id)) {
      // MiniOS errors should not block updates.
      LOG(INFO) << "No payload for MiniOS partition.";
      params->SetMiniOSUpdate(false);
      continue;
    } else {
      LOG(ERROR) << "Unknown Omaha response status: " << status;
      completer->set_code(ErrorCode::kOmahaResponseInvalid);
      return false;
    }
  }
  if (!response_.update_exists) {
    SetOutputObject(response_);
    completer->set_code(ErrorCode::kSuccess);
  }

  return response_.update_exists;
}

bool OmahaRequestAction::ParseParams(ScopedActionCompleter* completer) {
  const auto* params = SystemState::Get()->request_params();
  const OmahaParserData::App* main_app = nullptr;
  for (const auto& app : parser_data_.apps) {
    if (app.id == params->GetAppId() && app.postinstall_action) {
      main_app = &app;
    } else if (params->is_install()) {
      if (app.manifest.version != params->app_version()) {
        LOG(WARNING) << "An app has a version: " << app.manifest.version
                     << " that is different than platform app version: "
                     << params->app_version();
      }
    }
    if (app.postinstall_action && main_app == nullptr) {
      main_app = &app;
    }
  }

  if (main_app == nullptr) {
    LOG(ERROR) << "Omaha Response has no postinstall event action.";
    completer->set_code(ErrorCode::kOmahaResponseInvalid);
    return false;
  }

  const OmahaParserData::App& app = *main_app;
  // Get the optional properties one by one.
  response_.version = app.manifest.version;
  response_.more_info_url = app.postinstall_action->more_info_url;
  response_.prompt = ParseBool(app.postinstall_action->prompt);
  response_.deadline = app.postinstall_action->deadline;
  if (!base::StringToInt(app.postinstall_action->max_days_to_scatter,
                         &response_.max_days_to_scatter)) {
    response_.max_days_to_scatter = 0;
  }
  response_.disable_p2p_for_downloading =
      ParseBool(app.postinstall_action->disable_p2p_for_downloading);
  response_.disable_p2p_for_sharing =
      ParseBool(app.postinstall_action->disable_p2p_for_sharing);
  response_.disable_hash_checks =
      ParseBool(app.postinstall_action->disable_hash_checks);
  response_.disable_repeated_updates =
      ParseBool(app.postinstall_action->disable_repeated_updates);
  response_.public_key_rsa = app.postinstall_action->public_key_rsa;

  if (!base::StringToUint(app.postinstall_action->max_failure_count_per_url,
                          &response_.max_failure_count_per_url))
    response_.max_failure_count_per_url = kDefaultMaxFailureCountPerUrl;

  response_.disable_payload_backoff =
      ParseBool(app.postinstall_action->disable_payload_backoff);
  response_.powerwash_required =
      ParseBool(app.postinstall_action->powerwash_required);

  if (response_.version.empty()) {
    LOG(ERROR) << "Omaha Response does not have version in manifest!";
    completer->set_code(ErrorCode::kOmahaResponseInvalid);
    return false;
  }

  return true;
}

// If the transfer was successful, this uses expat to parse the response
// and fill in the appropriate fields of the output object. Also, notifies
// the processor that we're done.
void OmahaRequestAction::TransferComplete(HttpFetcher* fetcher,
                                          bool successful) {
  ScopedActionCompleter completer(processor_, this);
  string current_response(response_buffer_.begin(), response_buffer_.end());
  LOG(INFO) << "Omaha request response: " << current_response;

  PayloadStateInterface* const payload_state =
      SystemState::Get()->payload_state();

  // Events are best effort transactions -- assume they always succeed.
  if (IsEvent()) {
    CHECK(!HasOutputPipe()) << "No output pipe allowed for event requests.";
    completer.set_code(ErrorCode::kSuccess);
    return;
  }

  ErrorCode aux_error_code = fetcher->GetAuxiliaryErrorCode();
  if (aux_error_code != ErrorCode::kSuccess) {
    metrics::DownloadErrorCode download_error_code =
        metrics_utils::GetDownloadErrorCode(aux_error_code);
    SystemState::Get()->metrics_reporter()->ReportUpdateCheckMetrics(
        metrics::CheckResult::kUnset,
        metrics::CheckReaction::kUnset,
        download_error_code);
  }

  if (!successful) {
    int code = GetHTTPResponseCode();
    LOG(ERROR) << "Omaha request network transfer failed with HTTPResponseCode="
               << code;
    // Makes sure we send proper error values.
    if (code < 0 || code >= 1000) {
      code = 999;
      LOG(WARNING) << "Converting to proper HTTPResponseCode=" << code;
    }
    completer.set_code(static_cast<ErrorCode>(
        static_cast<int>(ErrorCode::kOmahaRequestHTTPResponseBase) + code));
    return;
  }

  OmahaParserXml parser(
      &parser_data_,
      reinterpret_cast<const char*>(response_buffer_.data()),
      response_buffer_.size(),
      SystemState::Get()->request_params()->rollback_allowed_milestones());
  ErrorCode error_code;
  if (!parser.Parse(&error_code)) {
    completer.set_code(error_code);
    return;
  }

  // Update the last ping day preferences based on the server daystart response
  // even if we didn't send a ping. Omaha always includes the daystart in the
  // response, but log the error if it didn't.
  LOG_IF(ERROR, !UpdateLastPingDays())
      << "Failed to update the last ping day preferences!";

  // Sets first_active_omaha_ping_sent to true (vpd in CrOS). We only do this if
  // we have got a response from omaha and if its value has never been set to
  // true before. Failure of this function should be ignored. There should be no
  // need to check if a=-1 has been sent because older devices have already sent
  // their a=-1 in the past and we have to set first_active_omaha_ping_sent for
  // future checks.
  if (!SystemState::Get()->hardware()->GetFirstActiveOmahaPingSent()) {
    if (!SystemState::Get()->hardware()->SetFirstActiveOmahaPingSent()) {
      SystemState::Get()->metrics_reporter()->ReportInternalErrorCode(
          ErrorCode::kFirstActiveOmahaPingSentPersistenceError);
    }
  }

  // Create/update the metadata files for each DLC app received.
  StorePingReply();

  if (!HasOutputPipe()) {
    // Just set success to whether or not the http transfer succeeded,
    // which must be true at this point in the code.
    completer.set_code(ErrorCode::kSuccess);
    return;
  }

  if (!ParseResponse(&completer))
    return;
  ProcessExclusions(SystemState::Get()->request_params(),
                    SystemState::Get()->update_attempter()->GetExcluder());
  response_.update_exists = true;
  SetOutputObject(response_);

  LoadOrPersistUpdateFirstSeenAtPref();

  ErrorCode error = ErrorCode::kSuccess;
  if (ShouldIgnoreUpdate(&error)) {
    // No need to change response_.update_exists here, since the value
    // has been output to the pipe.
    completer.set_code(error);
    return;
  }

  // If Omaha says to disable p2p, respect that
  if (response_.disable_p2p_for_downloading) {
    LOG(INFO) << "Forcibly disabling use of p2p for downloading as "
              << "requested by Omaha.";
    payload_state->SetUsingP2PForDownloading(false);
  }
  if (response_.disable_p2p_for_sharing) {
    LOG(INFO) << "Forcibly disabling use of p2p for sharing as "
              << "requested by Omaha.";
    payload_state->SetUsingP2PForSharing(false);
  }

  // Update the payload state with the current response. The payload state
  // will automatically reset all stale state if this response is different
  // from what's stored already. We are updating the payload state as late
  // as possible in this method so that if a new release gets pushed and then
  // got pulled back due to some issues, we don't want to clear our internal
  // state unnecessarily.
  payload_state->SetResponse(response_);

  // It could be we've already exceeded the deadline for when p2p is
  // allowed or that we've tried too many times with p2p. Check that.
  if (payload_state->GetUsingP2PForDownloading()) {
    payload_state->P2PNewAttempt();
    if (!payload_state->P2PAttemptAllowed()) {
      LOG(INFO) << "Forcibly disabling use of p2p for downloading because "
                << "of previous failures when using p2p.";
      payload_state->SetUsingP2PForDownloading(false);
    }
  }

  // From here on, we'll complete stuff in CompleteProcessing() so
  // disable |completer| since we'll create a new one in that
  // function.
  completer.set_should_complete(false);

  // If we're allowed to use p2p for downloading we do not pay
  // attention to wall-clock-based waiting if the URL is indeed
  // available via p2p. Therefore, check if the file is available via
  // p2p before deferring...
  if (payload_state->GetUsingP2PForDownloading()) {
    LookupPayloadViaP2P();
  } else {
    CompleteProcessing();
  }
}

void OmahaRequestAction::CompleteProcessing() {
  ScopedActionCompleter completer(processor_, this);
  PayloadStateInterface* payload_state = SystemState::Get()->payload_state();

  if (ShouldDeferDownload()) {
    response_.update_exists = false;
    LOG(INFO) << "Ignoring Omaha updates as updates are deferred by policy.";
    completer.set_code(ErrorCode::kOmahaUpdateDeferredPerPolicy);
    return;
  }

  if (payload_state->ShouldBackoffDownload()) {
    response_.update_exists = false;
    LOG(INFO) << "Ignoring Omaha updates in order to backoff our retry "
              << "attempts.";
    completer.set_code(ErrorCode::kOmahaUpdateDeferredForBackoff);
    return;
  }
  completer.set_code(ErrorCode::kSuccess);
}

void OmahaRequestAction::OnLookupPayloadViaP2PCompleted(const string& url) {
  LOG(INFO) << "Lookup complete, p2p-client returned URL '" << url << "'";
  if (!url.empty()) {
    SystemState::Get()->payload_state()->SetP2PUrl(url);
  } else {
    LOG(INFO) << "Forcibly disabling use of p2p for downloading "
              << "because no suitable peer could be found.";
    SystemState::Get()->payload_state()->SetUsingP2PForDownloading(false);
  }
  CompleteProcessing();
}

void OmahaRequestAction::LookupPayloadViaP2P() {
  // If the device is in the middle of an update, the state variables
  // kPrefsUpdateStateNextDataOffset, kPrefsUpdateStateNextDataLength
  // tracks the offset and length of the operation currently in
  // progress. The offset is based from the end of the manifest which
  // is kPrefsManifestMetadataSize bytes long.
  //
  // To make forward progress and avoid deadlocks, we need to find a
  // peer that has at least the entire operation we're currently
  // working on. Otherwise we may end up in a situation where two
  // devices bounce back and forth downloading from each other,
  // neither making any forward progress until one of them decides to
  // stop using p2p (via kMaxP2PAttempts and kMaxP2PAttemptTime safe-guards).
  // See http://crbug.com/297170 for an example)
  size_t minimum_size = 0;
  int64_t manifest_metadata_size = 0;
  int64_t manifest_signature_size = 0;
  int64_t next_data_offset = 0;
  int64_t next_data_length = 0;
  if (SystemState::Get()->prefs()->GetInt64(kPrefsManifestMetadataSize,
                                            &manifest_metadata_size) &&
      manifest_metadata_size != -1 &&
      SystemState::Get()->prefs()->GetInt64(kPrefsManifestSignatureSize,
                                            &manifest_signature_size) &&
      manifest_signature_size != -1 &&
      SystemState::Get()->prefs()->GetInt64(kPrefsUpdateStateNextDataOffset,
                                            &next_data_offset) &&
      next_data_offset != -1 &&
      SystemState::Get()->prefs()->GetInt64(kPrefsUpdateStateNextDataLength,
                                            &next_data_length)) {
    minimum_size = manifest_metadata_size + manifest_signature_size +
                   next_data_offset + next_data_length;
  }

  // TODO(senj): Fix P2P for multiple package.
  brillo::Blob raw_hash;
  if (!base::HexStringToBytes(response_.packages[0].hash, &raw_hash))
    return;
  string file_id =
      utils::CalculateP2PFileId(raw_hash, response_.packages[0].size);
  if (SystemState::Get()->p2p_manager()) {
    LOG(INFO) << "Checking if payload is available via p2p, file_id=" << file_id
              << " minimum_size=" << minimum_size;
    SystemState::Get()->p2p_manager()->LookupUrlForFile(
        file_id,
        minimum_size,
        kMaxP2PNetworkWaitTime,
        base::BindOnce(&OmahaRequestAction::OnLookupPayloadViaP2PCompleted,
                       base::Unretained(this)));
  }
}

bool OmahaRequestAction::ShouldDeferDownload() {
  const auto* params = SystemState::Get()->request_params();

  if (params->is_install()) {
    LOG(INFO) << "Never defer DLC installations.";
    return false;
  }

  if (params->interactive()) {
    LOG(INFO) << "Not deferring download because update is interactive.";
    return false;
  }

  // If we're using p2p to download _and_ we have a p2p URL, we never
  // defer the download. This is because the download will always
  // happen from a peer on the LAN and we've been waiting in line for
  // our turn.
  const PayloadStateInterface* payload_state =
      SystemState::Get()->payload_state();
  if (payload_state->GetUsingP2PForDownloading() &&
      !payload_state->GetP2PUrl().empty()) {
    LOG(INFO) << "Download not deferred because download "
              << "will happen from a local peer (via p2p).";
    return false;
  }

  // We should defer the downloads only if we've first satisfied the
  // wall-clock-based-waiting period and then the update-check-based waiting
  // period, if required.
  if (!params->wall_clock_based_wait_enabled()) {
    LOG(INFO) << "Wall-clock-based waiting period is not enabled,"
              << " so no deferring needed.";
    return false;
  }

  switch (IsWallClockBasedWaitingSatisfied()) {
    case kWallClockWaitNotSatisfied:
      // We haven't even satisfied the first condition, passing the
      // wall-clock-based waiting period, so we should defer the downloads
      // until that happens.
      LOG(INFO) << "wall-clock-based-wait not satisfied.";
      return true;

    case kWallClockWaitDoneButUpdateCheckWaitRequired:
      LOG(INFO) << "wall-clock-based-wait satisfied and "
                << "update-check-based-wait required.";
      return !IsUpdateCheckCountBasedWaitingSatisfied();

    case kWallClockWaitDoneAndUpdateCheckWaitNotRequired:
      // Wall-clock-based waiting period is satisfied, and it's determined
      // that we do not need the update-check-based wait. so no need to
      // defer downloads.
      LOG(INFO) << "wall-clock-based-wait satisfied and "
                << "update-check-based-wait is not required.";
      return false;

    default:
      // Returning false for this default case so we err on the
      // side of downloading updates than deferring in case of any bugs.
      NOTREACHED();
      return false;
  }
}

OmahaRequestAction::WallClockWaitResult
OmahaRequestAction::IsWallClockBasedWaitingSatisfied() {
  Time update_first_seen_at = LoadOrPersistUpdateFirstSeenAtPref();
  if (update_first_seen_at == base::Time()) {
    LOG(INFO) << "Not scattering as UpdateFirstSeenAt value cannot be read or "
                 "persisted.";
    return kWallClockWaitDoneAndUpdateCheckWaitNotRequired;
  }

  TimeDelta elapsed_time =
      SystemState::Get()->clock()->GetWallclockTime() - update_first_seen_at;
  TimeDelta max_scatter_period = base::Days(response_.max_days_to_scatter);
  int64_t staging_wait_time_in_days = 0;
  // Use staging and its default max value if staging is on.
  if (SystemState::Get()->prefs()->GetInt64(kPrefsWallClockStagingWaitPeriod,
                                            &staging_wait_time_in_days) &&
      staging_wait_time_in_days > 0)
    max_scatter_period = kMaxWaitTimeStagingIn;

  const auto* params = SystemState::Get()->request_params();
  LOG(INFO) << "Waiting Period = "
            << utils::FormatSecs(params->waiting_period().InSeconds())
            << ", Time Elapsed = "
            << utils::FormatSecs(elapsed_time.InSeconds())
            << ", MaxDaysToScatter = " << max_scatter_period.InDays();

  if (!response_.deadline.empty()) {
    // The deadline is set for all rules which serve a delta update from a
    // previous FSI, which means this update will be applied mostly in OOBE
    // cases. For these cases, we shouldn't scatter so as to finish the OOBE
    // quickly.
    LOG(INFO) << "Not scattering as deadline flag is set.";
    return kWallClockWaitDoneAndUpdateCheckWaitNotRequired;
  }

  if (max_scatter_period.InDays() == 0) {
    // This means the Omaha rule creator decides that this rule
    // should not be scattered irrespective of the policy.
    LOG(INFO) << "Not scattering as MaxDaysToScatter in rule is 0.";
    return kWallClockWaitDoneAndUpdateCheckWaitNotRequired;
  }

  if (elapsed_time > max_scatter_period) {
    // This means we've waited more than the upperbound wait in the rule
    // from the time we first saw a valid update available to us.
    // This will prevent update starvation.
    LOG(INFO) << "Not scattering as we're past the MaxDaysToScatter limit.";
    return kWallClockWaitDoneAndUpdateCheckWaitNotRequired;
  }

  // This means we are required to participate in scattering.
  // See if our turn has arrived now.
  TimeDelta remaining_wait_time = params->waiting_period() - elapsed_time;
  if (remaining_wait_time.InSeconds() <= 0) {
    // Yes, it's our turn now.
    LOG(INFO) << "Successfully passed the wall-clock-based-wait.";

    // But we can't download until the update-check-count-based wait is also
    // satisfied, so mark it as required now if update checks are enabled.
    return params->update_check_count_wait_enabled()
               ? kWallClockWaitDoneButUpdateCheckWaitRequired
               : kWallClockWaitDoneAndUpdateCheckWaitNotRequired;
  }

  // Not our turn yet, so we have to wait until our turn to
  // help scatter the downloads across all clients of the enterprise.
  LOG(INFO) << "Update deferred for another "
            << utils::FormatSecs(remaining_wait_time.InSeconds())
            << " per policy.";
  return kWallClockWaitNotSatisfied;
}

bool OmahaRequestAction::IsUpdateCheckCountBasedWaitingSatisfied() {
  int64_t update_check_count_value;
  const auto* params = SystemState::Get()->request_params();

  if (SystemState::Get()->prefs()->Exists(kPrefsUpdateCheckCount)) {
    if (!SystemState::Get()->prefs()->GetInt64(kPrefsUpdateCheckCount,
                                               &update_check_count_value)) {
      // We are unable to read the update check count from file for some reason.
      // So let's proceed anyway so as to not stall the update.
      LOG(ERROR) << "Unable to read update check count. "
                 << "Skipping update-check-count-based-wait.";
      return true;
    }
  } else {
    // This file does not exist. This means we haven't started our update
    // check count down yet, so this is the right time to start the count down.
    update_check_count_value =
        base::RandInt(params->min_update_checks_needed(),
                      params->max_update_checks_allowed());

    LOG(INFO) << "Randomly picked update check count value = "
              << update_check_count_value;

    // Write out the initial value of update_check_count_value.
    if (!SystemState::Get()->prefs()->SetInt64(kPrefsUpdateCheckCount,
                                               update_check_count_value)) {
      // We weren't able to write the update check count file for some reason.
      // So let's proceed anyway so as to not stall the update.
      LOG(ERROR) << "Unable to write update check count. "
                 << "Skipping update-check-count-based-wait.";
      return true;
    }
  }

  if (update_check_count_value == 0) {
    LOG(INFO) << "Successfully passed the update-check-based-wait.";
    return true;
  }

  if (update_check_count_value < 0 ||
      update_check_count_value > params->max_update_checks_allowed()) {
    // We err on the side of skipping scattering logic instead of stalling
    // a machine from receiving any updates in case of any unexpected state.
    LOG(ERROR) << "Invalid value for update check count detected. "
               << "Skipping update-check-count-based-wait.";
    return true;
  }

  // Legal value, we need to wait for more update checks to happen
  // until this becomes 0.
  LOG(INFO) << "Deferring Omaha updates for another "
            << update_check_count_value << " update checks per policy";
  return false;
}

bool OmahaRequestAction::ParseInstallDate() {
  int64_t elapsed_days = 0;
  if (!base::StringToInt64(parser_data_.daystart.elapsed_days, &elapsed_days))
    return false;

  if (elapsed_days < 0)
    return false;

  response_.install_date_days = elapsed_days;
  return true;
}

// static
bool OmahaRequestAction::HasInstallDate() {
  return SystemState::Get()->prefs()->Exists(kPrefsInstallDateDays);
}

// static
bool OmahaRequestAction::PersistInstallDate(
    int install_date_days, InstallDateProvisioningSource source) {
  TEST_AND_RETURN_FALSE(install_date_days >= 0);

  auto* prefs = SystemState::Get()->prefs();
  if (!prefs->SetInt64(kPrefsInstallDateDays, install_date_days))
    return false;

  SystemState::Get()->metrics_reporter()->ReportInstallDateProvisioningSource(
      static_cast<int>(source),  // Sample.
      kProvisionedMax);          // Maximum.
  return true;
}

void OmahaRequestAction::PersistCohortData(
    const string& prefs_key, const std::optional<string>& new_value) {
  if (!new_value)
    return;
  const string& value = new_value.value();
  if (value.empty() && SystemState::Get()->prefs()->Exists(prefs_key)) {
    if (!SystemState::Get()->prefs()->Delete(prefs_key))
      LOG(ERROR) << "Failed to remove stored " << prefs_key << "value.";
    else
      LOG(INFO) << "Removed stored " << prefs_key << " value.";
  } else if (!value.empty()) {
    if (!SystemState::Get()->prefs()->SetString(prefs_key, value))
      LOG(INFO) << "Failed to store new setting " << prefs_key << " as "
                << value;
    else
      LOG(INFO) << "Stored cohort setting " << prefs_key << " as " << value;
  }
}

void OmahaRequestAction::PersistCohorts() {
  const auto* params = SystemState::Get()->request_params();
  for (const auto& app : parser_data_.apps) {
    // For platform App ID.
    if (app.id == params->GetAppId()) {
      PersistCohortData(kPrefsOmahaCohort, app.cohort);
      PersistCohortData(kPrefsOmahaCohortName, app.cohortname);
      PersistCohortData(kPrefsOmahaCohortHint, app.cohorthint);
    } else if (params->IsDlcAppId(app.id)) {
      string dlc_id;
      if (!params->GetDlcId(app.id, &dlc_id)) {
        LOG(WARNING) << "Skip persisting cohorts for DLC App ID=" << app.id
                     << " as it is not in the request params.";
        continue;
      }
      auto* prefs = SystemState::Get()->prefs();
      PersistCohortData(
          prefs->CreateSubKey({kDlcPrefsSubDir, dlc_id, kPrefsOmahaCohort}),
          app.cohort);
      PersistCohortData(
          prefs->CreateSubKey({kDlcPrefsSubDir, dlc_id, kPrefsOmahaCohortName}),
          app.cohortname);
      PersistCohortData(
          prefs->CreateSubKey({kDlcPrefsSubDir, dlc_id, kPrefsOmahaCohortHint}),
          app.cohorthint);
    } else {
      LOG(WARNING) << "Skip persisting cohorts for unknown App ID=" << app.id;
    }
  }
}

void OmahaRequestAction::ActionCompleted(ErrorCode code) {
  // We only want to report this on "update check".
  if (ping_only_ || event_ != nullptr)
    return;

  metrics::CheckResult result = metrics::CheckResult::kUnset;
  metrics::CheckReaction reaction = metrics::CheckReaction::kUnset;
  metrics::DownloadErrorCode download_error_code =
      metrics::DownloadErrorCode::kUnset;

  // Regular update attempt.
  switch (code) {
    case ErrorCode::kSuccess:
      // OK, we parsed the response successfully but that does
      // necessarily mean that an update is available.
      if (HasOutputPipe()) {
        if (response_.update_exists) {
          result = metrics::CheckResult::kUpdateAvailable;
          reaction = metrics::CheckReaction::kUpdating;
        } else {
          result = metrics::CheckResult::kNoUpdateAvailable;
        }
      } else {
        result = metrics::CheckResult::kNoUpdateAvailable;
      }
      break;

    case ErrorCode::kOmahaUpdateIgnoredPerPolicy:
    case ErrorCode::kOmahaUpdateIgnoredOverCellular:
    case ErrorCode::kOmahaUpdateIgnoredOverMetered:
    case ErrorCode::kUpdateIgnoredRollbackVersion:
      result = metrics::CheckResult::kUpdateAvailable;
      reaction = metrics::CheckReaction::kIgnored;
      break;

    case ErrorCode::kOmahaUpdateDeferredPerPolicy:
      result = metrics::CheckResult::kUpdateAvailable;
      reaction = metrics::CheckReaction::kDeferring;
      break;

    case ErrorCode::kOmahaUpdateDeferredForBackoff:
      result = metrics::CheckResult::kUpdateAvailable;
      reaction = metrics::CheckReaction::kBackingOff;
      break;

    default:
      // We report two flavors of errors, "Download errors" and "Parsing
      // error". Try to convert to the former and if that doesn't work
      // we know it's the latter.
      metrics::DownloadErrorCode tmp_error =
          metrics_utils::GetDownloadErrorCode(code);
      if (tmp_error != metrics::DownloadErrorCode::kInputMalformed) {
        result = metrics::CheckResult::kDownloadError;
        download_error_code = tmp_error;
      } else {
        result = metrics::CheckResult::kParsingError;
      }
      break;
  }

  SystemState::Get()->metrics_reporter()->ReportUpdateCheckMetrics(
      result, reaction, download_error_code);
}

bool OmahaRequestAction::ShouldIgnoreUpdate(ErrorCode* error) const {
  const auto* params = SystemState::Get()->request_params();
  if (params->is_install()) {
    LOG(INFO) << "Never ignore DLC installations.";
    return false;
  }

  const auto* hardware = SystemState::Get()->hardware();
  // Never ignore valid update when running from MiniOS.
  if (hardware->IsRunningFromMiniOs())
    return false;

  // Note: policy decision to not update to a version we rolled back from.
  string rollback_version =
      SystemState::Get()->payload_state()->GetRollbackVersion();
  if (!rollback_version.empty()) {
    LOG(INFO) << "Detected previous rollback from version " << rollback_version;
    if (rollback_version == response_.version) {
      LOG(INFO) << "Received version that we rolled back from. Ignoring.";
      *error = ErrorCode::kUpdateIgnoredRollbackVersion;
      return true;
    }
  }

  if (!CheckForRepeatedFpValues(error)) {
    return true;
  }

  if (hardware->IsOOBEEnabled() && !hardware->IsOOBEComplete(nullptr) &&
      (response_.deadline.empty() ||
       SystemState::Get()->payload_state()->GetRollbackHappened()) &&
      params->app_version() != kCriticalAppVersion) {
    if (!hardware->IsConsumerSegmentSet(hardware->ReadLocalState().get())) {
      LOG(INFO)
          << "Ignoring a non-critical Omaha update before OOBE completion.";
      *error = ErrorCode::kNonCriticalUpdateInOOBE;
      return true;
    }
    LOG(INFO) << "Considering a non-critical Omaha update  for consumer "
                 "segment users before OOBE completion.";
  }

  if (hardware->IsEnrollmentRecoveryModeEnabled(
          hardware->ReadLocalState().get()) &&
      response_.deadline.empty() &&
      params->app_version() != kCriticalAppVersion) {
    LOG(INFO) << "Ignoring non-critical Omaha update as enrollment "
              << "recovery mode is enabled.";
    *error = ErrorCode::kNonCriticalUpdateEnrollmentRecovery;
    return true;
  }

  if (!IsUpdateAllowedOverCurrentConnection(error)) {
    LOG(INFO) << "Update is not allowed over current connection.";
    return true;
  }

  // Currently non-critical updates always update alongside the platform update
  // (a critical update) so this case should never actually be hit if the
  // request to Omaha for updates are correct. In other words, stop the update
  // from happening as there are no packages in the response to process.
  if (response_.packages.empty()) {
    LOG(ERROR) << "All packages were excluded.";
  }

  // Note: We could technically delete the UpdateFirstSeenAt state when we
  // return true. If we do, it'll mean a device has to restart the
  // UpdateFirstSeenAt and thus help scattering take effect when the AU is
  // turned on again. On the other hand, it also increases the chance of update
  // starvation if an admin turns AU on/off more frequently. We choose to err on
  // the side of preventing starvation at the cost of not applying scattering in
  // those cases.
  return false;
}

bool OmahaRequestAction::CheckForRepeatedFpValues(ErrorCode* error) const {
  const auto* params = SystemState::Get()->request_params();
  for (const auto& package : response_.packages) {
    std::string dlc_id;
    if (params->GetDlcId(package.app_id, &dlc_id)) {
      auto it = params->dlc_apps_params().find(package.app_id);
      if (it != params->dlc_apps_params().end() &&
          it->second.last_fp == package.fp) {
        LOG(INFO)
            << "Detected same fingerprint value sent in request for Dlc ID "
            << dlc_id;
        *error = ErrorCode::kRepeatedFpFromOmahaError;
        return false;
      }
    } else if (params->IsMiniOSAppId(package.app_id)) {
      if (package.fp == params->minios_app_params().last_fp) {
        LOG(INFO)
            << "Detected same fingerprint value sent in request for MiniOS ID "
            << package.app_id;
        *error = ErrorCode::kRepeatedFpFromOmahaError;
        return false;
      }
    } else {
      if (package.fp == params->last_fp()) {
        LOG(INFO) << "Detected same fingerprint value sent in request for "
                     "platform ID "
                  << package.app_id;
        *error = ErrorCode::kRepeatedFpFromOmahaError;
        return false;
      }
    }
  }
  return true;
}

bool OmahaRequestAction::IsUpdateAllowedOverCellularByPrefs() const {
  auto* prefs = SystemState::Get()->prefs();
  bool is_allowed;
  if (prefs->Exists(kPrefsUpdateOverCellularPermission) &&
      prefs->GetBoolean(kPrefsUpdateOverCellularPermission, &is_allowed) &&
      is_allowed) {
    LOG(INFO) << "Allowing updates over cellular as permission preference is "
                 "set to true.";
    return true;
  }

  if (!prefs->Exists(kPrefsUpdateOverCellularTargetVersion) ||
      !prefs->Exists(kPrefsUpdateOverCellularTargetSize)) {
    LOG(INFO) << "Disabling updates over cellular as permission preference is "
                 "set to false or does not exist while target does not exist.";
    return false;
  }

  std::string target_version;
  int64_t target_size;

  if (!prefs->GetString(kPrefsUpdateOverCellularTargetVersion,
                        &target_version) ||
      !prefs->GetInt64(kPrefsUpdateOverCellularTargetSize, &target_size)) {
    LOG(INFO) << "Disabling updates over cellular as the target version or "
                 "size is not accessible.";
    return false;
  }

  uint64_t total_packages_size = 0;
  for (const auto& package : response_.packages) {
    total_packages_size += package.size;
  }
  if (target_version == response_.version &&
      static_cast<uint64_t>(target_size) == total_packages_size) {
    LOG(INFO) << "Allowing updates over cellular as the target matches the"
                 "omaha response.";
    return true;
  } else {
    LOG(INFO) << "Disabling updates over cellular as the target does not"
                 "match the omaha response.";
    return false;
  }
}

bool OmahaRequestAction::IsUpdateAllowedOverCurrentConnection(
    ErrorCode* error) const {
  ConnectionType type;
  bool metered = false;
  ConnectionManagerInterface* connection_manager =
      SystemState::Get()->connection_manager();
  if (!connection_manager->GetConnectionProperties(&type, &metered)) {
    LOG(INFO) << "We could not determine our connection type. "
              << "Defaulting to allow updates.";
    return true;
  }

  if (!metered) {
    LOG(INFO) << "We are connected via an unmetered network, type: "
              << connection_utils::StringForConnectionType(type);
    return true;
  }

  bool is_allowed = true;
  if (connection_manager->IsAllowedConnectionTypesForUpdateSet()) {
    // There's no need to further check user preferences as the device policy
    // is set regarding updates over metered network.
    LOG(INFO) << "Current connection is metered, checking device policy.";
    if (!(is_allowed = connection_manager->IsUpdateAllowedOverMetered()))
      *error = ErrorCode::kOmahaUpdateIgnoredPerPolicy;
  } else if (!(is_allowed = IsUpdateAllowedOverCellularByPrefs())) {
    // Deivce policy is not set, so user preferences overwrite whether to
    // allow updates over metered network.
    *error = type == ConnectionType::kCellular
                 ? ErrorCode::kOmahaUpdateIgnoredOverCellular
                 : ErrorCode::kOmahaUpdateIgnoredOverMetered;
  }

  LOG(INFO) << "We are connected via "
            << connection_utils::StringForConnectionType(type)
            << ", Updates allowed: " << (is_allowed ? "Yes" : "No");
  return is_allowed;
}

base::Time OmahaRequestAction::LoadOrPersistUpdateFirstSeenAtPref() const {
  Time update_first_seen_at;
  int64_t update_first_seen_at_int;
  if (SystemState::Get()->prefs()->Exists(kPrefsUpdateFirstSeenAt)) {
    if (SystemState::Get()->prefs()->GetInt64(kPrefsUpdateFirstSeenAt,
                                              &update_first_seen_at_int)) {
      // Note: This timestamp could be that of ANY update we saw in the past
      // (not necessarily this particular update we're considering to apply)
      // but never got to apply because of some reason (e.g. stop AU policy,
      // updates being pulled out from Omaha, changes in target version prefix,
      // new update being rolled out, etc.). But for the purposes of scattering
      // it doesn't matter which update the timestamp corresponds to. i.e.
      // the clock starts ticking the first time we see an update and we're
      // ready to apply when the random wait period is satisfied relative to
      // that first seen timestamp.
      update_first_seen_at = Time::FromInternalValue(update_first_seen_at_int);
      LOG(INFO) << "Using persisted value of UpdateFirstSeenAt: "
                << utils::ToString(update_first_seen_at);
    } else {
      // This seems like an unexpected error where the persisted value exists
      // but it's not readable for some reason.
      LOG(INFO) << "UpdateFirstSeenAt value cannot be read";
      return base::Time();
    }
  } else {
    update_first_seen_at = SystemState::Get()->clock()->GetWallclockTime();
    update_first_seen_at_int = update_first_seen_at.ToInternalValue();
    if (SystemState::Get()->prefs()->SetInt64(kPrefsUpdateFirstSeenAt,
                                              update_first_seen_at_int)) {
      LOG(INFO) << "Persisted the new value for UpdateFirstSeenAt: "
                << utils::ToString(update_first_seen_at);
    } else {
      // This seems like an unexpected error where the value cannot be
      // persisted for some reason.
      LOG(INFO) << "UpdateFirstSeenAt value "
                << utils::ToString(update_first_seen_at)
                << " cannot be persisted";
      return base::Time();
    }
  }
  return update_first_seen_at;
}

}  // namespace chromeos_update_engine
