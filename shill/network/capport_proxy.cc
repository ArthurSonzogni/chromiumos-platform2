// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/capport_proxy.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/values.h>
#include <base/json/json_reader.h>
#include <brillo/http/http_request.h>
#include <brillo/http/http_transport.h>
#include <net-base/http_url.h>

namespace shill {
namespace {

// The Accept HTTP header for Capport API.
constexpr char kAcceptHeader[] = "application/captive+json";

// The keys of the JSON returned by the Capport API, specified in
// RFC 8908 section 5.
constexpr char kIsCaptiveKey[] = "captive";
constexpr char kUserPortalUrlKey[] = "user-portal-url";
constexpr char kVenueInfoUrlKey[] = "venue-info-url";
constexpr char kCanExtendSessionKey[] = "can-extend-session";
constexpr char kSecondsRemainingKey[] = "seconds-remaining";
constexpr char kBytesRemainingKey[] = "bytes-remaining";

}  // namespace

std::optional<CapportStatus> CapportStatus::ParseFromJson(
    std::string_view json_str) {
  const std::optional<base::Value::Dict> dict =
      base::JSONReader::ReadDict(json_str);
  if (!dict.has_value()) {
    return std::nullopt;
  }

  // Parse the mandatory field.
  auto status = std::make_optional<CapportStatus>();
  if (const std::optional<bool> is_captive = dict->FindBool(kIsCaptiveKey);
      is_captive.has_value()) {
    status->is_captive = *is_captive;
  } else {
    LOG(WARNING) << "The mandatory field `" << kIsCaptiveKey << "` is missing";
    return std::nullopt;
  }

  // Parse the optional fields.
  if (const std::string* value = dict->FindString(kUserPortalUrlKey);
      value != nullptr) {
    const auto url = net_base::HttpUrl::CreateFromString(*value);
    if (!url.has_value() ||
        url->protocol() != net_base::HttpUrl::Protocol::kHttps) {
      LOG(WARNING) << "User portal URL is not valid: " << *value;
      return std::nullopt;
    }
    status->user_portal_url = *url;
  }
  if (const std::string* value = dict->FindString(kVenueInfoUrlKey);
      value != nullptr) {
    const auto url = net_base::HttpUrl::CreateFromString(*value);
    if (!url.has_value()) {
      LOG(WARNING) << "Venue info URL is not valid: " << *value;
      return std::nullopt;
    }
    status->venue_info_url = *url;
  }
  if (const std::optional<bool> value = dict->FindBool(kCanExtendSessionKey);
      value.has_value()) {
    status->can_extend_session = *value;
  }
  if (const std::optional<int> value = dict->FindInt(kSecondsRemainingKey);
      value.has_value() && *value >= 0) {
    status->seconds_remaining = base::Seconds(*value);
  }
  if (const std::optional<int> value = dict->FindInt(kBytesRemainingKey);
      value.has_value() && *value >= 0) {
    status->bytes_remaining = *value;
  }

  if (status->is_captive && !status->user_portal_url.has_value()) {
    LOG(WARNING) << "user_portal_url field is empty when is_captive is true";
    return std::nullopt;
  }

  // Clear the fields for the open state when the portal is captive.
  if (status->is_captive && status->seconds_remaining.has_value()) {
    LOG(WARNING) << "seconds_remaining should be empty when is_captive is true";
    status->seconds_remaining = std::nullopt;
  }
  if (status->is_captive && status->bytes_remaining.has_value()) {
    LOG(WARNING) << "bytes_remaining should be empty when is_captive is true";
    status->bytes_remaining = std::nullopt;
  }

  return status;
}

std::unique_ptr<CapportProxy> CapportProxy::Create(
    Metrics* metrics,
    std::string_view interface,
    const net_base::HttpUrl& api_url,
    std::shared_ptr<brillo::http::Transport> http_transport,
    base::TimeDelta transport_timeout) {
  if (api_url.protocol() != net_base::HttpUrl::Protocol::kHttps) {
    LOG(ERROR) << "The URL of CAPPORT API is not valid: " << api_url.ToString();
    return nullptr;
  }

  http_transport->SetInterface(std::string(interface));
  http_transport->SetDefaultTimeout(transport_timeout);
  return std::make_unique<CapportProxy>(metrics, api_url,
                                        std::move(http_transport),
                                        std::string(interface) + ": ");
}

CapportProxy::CapportProxy(
    Metrics* metrics,
    const net_base::HttpUrl& api_url,
    std::shared_ptr<brillo::http::Transport> http_transport,
    std::string_view logging_tag)
    : metrics_(metrics),
      api_url_(api_url),
      http_transport_(std::move(http_transport)),
      logging_tag_(logging_tag) {}

CapportProxy::~CapportProxy() {
  if (has_venue_info_url_.has_value()) {
    metrics_->SendBoolToUMA(Metrics::kMetricCapportContainsVenueInfoUrl,
                            *has_venue_info_url_);
  }
  if (has_seconds_remaining_.has_value()) {
    metrics_->SendBoolToUMA(Metrics::kMetricCapportContainsSecondsRemaining,
                            *has_seconds_remaining_);
  }
  if (max_seconds_remaining_.has_value()) {
    metrics_->SendToUMA(Metrics::kMetricCapportMaxSecondsRemaining,
                        *max_seconds_remaining_);
  }
}

void CapportProxy::SendRequest(StatusCallback callback) {
  LOG_IF(DFATAL, IsRunning())
      << logging_tag_ << "The previous request is still running";
  callback_ = std::move(callback);

  // TODO(b/305129516): Add metrics to record latency and success/failure count.
  LOG_IF(WARNING, http_request_)
      << logging_tag_ << "The pending request is not clear";
  auto http_request_ = std::make_optional<brillo::http::Request>(
      api_url_.ToString(), brillo::http::request_type::kGet, http_transport_);
  http_request_->SetAccept(kAcceptHeader);
  http_request_->GetResponse(base::BindOnce(&CapportProxy::OnRequestSuccess,
                                            weak_ptr_factory_.GetWeakPtr()),
                             base::BindOnce(&CapportProxy::OnRequestError,
                                            weak_ptr_factory_.GetWeakPtr()));
}

void CapportProxy::Stop() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  callback_.Reset();
}

void CapportProxy::OnRequestSuccess(
    brillo::http::RequestID request_id,
    std::unique_ptr<brillo::http::Response> response) {
  LOG_IF(DFATAL, !callback_)
      << logging_tag_ << __func__ << ": callback is missing";
  http_request_.reset();

  if (!response->IsSuccessful()) {
    LOG(ERROR) << logging_tag_
               << "Failed to get a success response, status code="
               << response->GetStatusCode();
    std::move(callback_).Run(std::nullopt);
    return;
  }

  const std::string json_str = response->ExtractDataAsString();
  std::optional<CapportStatus> status = CapportStatus::ParseFromJson(json_str);
  if (!status.has_value()) {
    LOG(ERROR) << logging_tag_ << "The CAPPORT server found by RFC8910 ("
               << api_url_.ToString()
               << ") was not compliant, failed to parse JSON: " << json_str;
    std::move(callback_).Run(std::nullopt);
    return;
  }

  if (status->venue_info_url.has_value()) {
    has_venue_info_url_ = true;
  } else if (!has_venue_info_url_.has_value() && !status->is_captive) {
    has_venue_info_url_ = false;
  }

  // seconds_remaining is only meaningful at is_captive==false.
  if (!status->is_captive) {
    // Once has_seconds_remaining_ is set to true, it will be stick to true.
    if (!has_seconds_remaining_.value_or(false)) {
      has_seconds_remaining_ = status->seconds_remaining.has_value();
    }
    if (status->seconds_remaining.has_value()) {
      max_seconds_remaining_ =
          std::max(static_cast<int>(status->seconds_remaining->InSeconds()),
                   max_seconds_remaining_.value_or(0));
    }
  }

  std::move(callback_).Run(std::move(status));
}

void CapportProxy::OnRequestError(brillo::http::RequestID request_id,
                                  const brillo::Error* error) {
  LOG_IF(DFATAL, !callback_)
      << logging_tag_ << __func__ << ": callback is missing";
  http_request_.reset();

  LOG(ERROR) << logging_tag_ << "Failed to get request from CAPPORT API: "
             << error->GetMessage();
  std::move(callback_).Run(std::nullopt);
}

bool CapportProxy::IsRunning() const {
  return !callback_.is_null();
}

CapportProxyFactory::CapportProxyFactory() = default;

CapportProxyFactory::~CapportProxyFactory() = default;

std::unique_ptr<CapportProxy> CapportProxyFactory::Create(
    Metrics* metrics,
    std::string_view interface,
    const net_base::HttpUrl& api_url,
    std::shared_ptr<brillo::http::Transport> http_transport,
    base::TimeDelta transport_timeout) {
  return CapportProxy::Create(metrics, interface, api_url,
                              std::move(http_transport), transport_timeout);
}

}  // namespace shill
