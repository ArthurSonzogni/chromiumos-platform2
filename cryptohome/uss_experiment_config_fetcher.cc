// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/uss_experiment_config_fetcher.h"

#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_util.h>
#include <base/system/sys_info.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <base/values.h>
#include <brillo/http/http_transport.h>
#include <brillo/http/http_utils.h>
#include <shill/dbus-constants.h>
#include <shill/dbus-proxies.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/user_secret_stash.h"

namespace cryptohome {

namespace {

constexpr char kGstaticUrlPrefix[] =
    "https://www.gstatic.com/uss-experiment/v1.json";

constexpr char kConnectionStateOnline[] = "online";

constexpr char kDefaultConfigKey[] = "default";
constexpr char kConfigPopulationKey[] = "population";
constexpr char kConfigLastInvalidKey[] = "last_invalid";

constexpr char kMaxRetries = 9;

}  // namespace

std::unique_ptr<UssExperimentConfigFetcher> UssExperimentConfigFetcher::Create(
    const scoped_refptr<dbus::Bus>& bus) {
  auto uss_experiment_config_fetcher =
      std::make_unique<UssExperimentConfigFetcher>();
  uss_experiment_config_fetcher->Initialize(bus);
  return uss_experiment_config_fetcher;
}

void UssExperimentConfigFetcher::Initialize(
    const scoped_refptr<dbus::Bus>& bus) {
  base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_TRACK",
                                    &chromeos_release_track_);
  transport_ = brillo::http::Transport::CreateDefault();
  manager_proxy_ = std::make_unique<org::chromium::flimflam::ManagerProxy>(bus);
  manager_proxy_->RegisterPropertyChangedSignalHandler(
      base::BindRepeating(&UssExperimentConfigFetcher::OnManagerPropertyChange,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(
          &UssExperimentConfigFetcher::OnManagerPropertyChangeRegistration,
          weak_factory_.GetWeakPtr()));
}

void UssExperimentConfigFetcher::OnManagerPropertyChangeRegistration(
    const std::string& /*interface*/,
    const std::string& /*signal_name*/,
    bool success) {
  if (!success) {
    LOG(WARNING) << "Unable to register for shill manager change events.";
    return;
  }

  brillo::VariantDictionary properties;
  if (!manager_proxy_->GetProperties(&properties, nullptr)) {
    LOG(WARNING) << "Unable to get shill manager properties.";
    return;
  }

  auto it = properties.find(shill::kConnectionStateProperty);
  if (it == properties.end()) {
    return;
  }
  OnManagerPropertyChange(shill::kConnectionStateProperty, it->second);
}

void UssExperimentConfigFetcher::OnManagerPropertyChange(
    const std::string& property_name, const brillo::Any& property_value) {
  if (fetch_initiated_) {
    return;
  }
  // Only handle changes to the connection state.
  if (property_name != shill::kConnectionStateProperty) {
    return;
  }

  std::string connection_state;
  if (!property_value.GetValue(&connection_state)) {
    LOG(WARNING)
        << "Connection state fetched from shill manager is not a string.";
    return;
  }

  if (base::EqualsCaseInsensitiveASCII(connection_state,
                                       kConnectionStateOnline)) {
    Fetch(base::BindRepeating(&UssExperimentConfigFetcher::SetUssExperimentFlag,
                              weak_factory_.GetWeakPtr()));
    fetch_initiated_ = true;
  }
}

void UssExperimentConfigFetcher::Fetch(
    UssExperimentConfigFetcher::FetchSuccessCallback success_callback) {
  brillo::ErrorPtr error;
  std::unique_ptr<brillo::http::Response> response;

  // TODO(https://crbug.com/714018): This should actually be a OnceCallback but
  // the brillo http interface hasn't migrated. Switch to BindOnce after
  // migrated.
  brillo::http::Get(
      kGstaticUrlPrefix, {}, transport_,
      base::BindRepeating(&UssExperimentConfigFetcher::OnFetchSuccess,
                          weak_factory_.GetWeakPtr(), success_callback),
      base::BindRepeating(&UssExperimentConfigFetcher::RetryFetchOnGetError,
                          weak_factory_.GetWeakPtr(), success_callback));
}

void UssExperimentConfigFetcher::OnFetchSuccess(
    UssExperimentConfigFetcher::FetchSuccessCallback success_callback,
    brillo::http::RequestID /*request_id*/,
    std::unique_ptr<brillo::http::Response> response) {
  // If we didn't successfully parse the device's release track, we can't
  // determine which channel we are in to parse corresponding config fields.
  if (chromeos_release_track_.empty()) {
    LOG(WARNING) << "Failed to determine which channel the device is in.";
    ReportFetchUssExperimentConfigStatus(
        FetchUssExperimentConfigStatus::kNoReleaseTrack);
    return;
  }

  int status_code = response->GetStatusCode();
  if (status_code != brillo::http::status_code::Ok) {
    LOG(WARNING) << "Fetch USS config failed with status code: " << status_code;
    RetryFetch(success_callback);
    return;
  }

  // The fetched config should be a valid json file.
  brillo::ErrorPtr error;
  const std::optional<base::Value::Dict> json =
      brillo::http::ParseJsonResponse(response.get(), nullptr, &error);
  if (error || !json.has_value()) {
    LOG(WARNING) << "The fetched USS config is not a valid json file.";
    ReportFetchUssExperimentConfigStatus(
        FetchUssExperimentConfigStatus::kParseError);
    return;
  }

  // Check whether the `last_invalid` field is present in the config that
  // corresponds to this device's channel. If not, fallback to the default
  // config.
  const std::string last_invalid_path =
      base::JoinString({chromeos_release_track_, kConfigLastInvalidKey}, ".");
  std::optional<int> last_invalid =
      json->FindIntByDottedPath(last_invalid_path);
  if (!last_invalid.has_value()) {
    const std::string default_last_invalid_path =
        base::JoinString({kDefaultConfigKey, kConfigLastInvalidKey}, ".");
    last_invalid = json->FindIntByDottedPath(default_last_invalid_path);
  }

  // Check whether the `population` field is present in the config that
  // corresponds to this device's channel. If not, fallback to the default
  // config.
  const std::string population_path =
      base::JoinString({chromeos_release_track_, kConfigPopulationKey}, ".");
  std::optional<double> population =
      json->FindDoubleByDottedPath(population_path);
  if (!population.has_value()) {
    const std::string default_population_path =
        base::JoinString({kDefaultConfigKey, kConfigPopulationKey}, ".");
    population = json->FindDoubleByDottedPath(default_population_path);
  }

  // Check that both fields are parsed successfully.
  if (!last_invalid.has_value()) {
    LOG(WARNING)
        << "Failed to parse `last_inavlid` field in the fetched USS config.";
    ReportFetchUssExperimentConfigStatus(
        FetchUssExperimentConfigStatus::kParseError);
    return;
  }
  if (!population.has_value()) {
    LOG(WARNING)
        << "Failed to parse `population` field in the fetched USS config.";
    ReportFetchUssExperimentConfigStatus(
        FetchUssExperimentConfigStatus::kParseError);
    return;
  }
  success_callback.Run(*last_invalid, *population);
}

void UssExperimentConfigFetcher::RetryFetchOnGetError(
    FetchSuccessCallback success_callback,
    brillo::http::RequestID,
    const brillo::Error* error) {
  LOG(ERROR) << "GET USS config failed: " << error->GetMessage();
  RetryFetch(success_callback);
}

void UssExperimentConfigFetcher::RetryFetch(
    FetchSuccessCallback success_callback) {
  if (retries_ == kMaxRetries) {
    LOG(ERROR) << "Retry attempt limit reached for fetching USS config, "
                  "reporting fetch error.";
    ReportFetchUssExperimentConfigStatus(
        FetchUssExperimentConfigStatus::kFetchError);
    return;
  }
  retries_++;
  base::SequencedTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&UssExperimentConfigFetcher::Fetch,
                     weak_factory_.GetWeakPtr(), success_callback),
      base::Seconds(1));
}

void UssExperimentConfigFetcher::SetUssExperimentFlag(int last_invalid,
                                                      double population) {
  bool enabled;
  if (last_invalid >= UserSecretStashExperimentVersion()) {
    enabled = false;
  } else {
    // `population` is directly interpreted as the probability to enable the
    // experiment. This will result in roughly `population` portion of the total
    // population enabling the experiment.
    enabled = base::RandDouble() < population;
  }

  FetchUssExperimentConfigStatus status =
      enabled ? FetchUssExperimentConfigStatus::kEnabled
              : FetchUssExperimentConfigStatus::kDisabled;
  ReportFetchUssExperimentConfigStatus(status);
  ReportFetchUssExperimentConfigRetries(retries_);

  SetUserSecretStashExperimentFlag(enabled);
}

void UssExperimentConfigFetcher::SetReleaseTrackForTesting(std::string track) {
  chromeos_release_track_ = track;
}

void UssExperimentConfigFetcher::SetTransportForTesting(
    std::shared_ptr<brillo::http::Transport> transport) {
  transport_ = transport;
}

void UssExperimentConfigFetcher::SetProxyForTesting(
    std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
        manager_proxy) {
  manager_proxy_ = std::move(manager_proxy);
}

}  // namespace cryptohome
