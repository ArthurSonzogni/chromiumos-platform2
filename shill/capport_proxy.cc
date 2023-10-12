// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/capport_proxy.h"

#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/values.h>
#include <base/json/json_reader.h>
#include <brillo/http/http_request.h>
#include <brillo/http/http_transport.h>

#include "shill/http_url.h"

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
    const auto url = HttpUrl::CreateFromString(*value);
    if (!url.has_value() || url->protocol() != HttpUrl::Protocol::kHttps) {
      LOG(WARNING) << "User portal URL is not invalid: " << *value;
      return std::nullopt;
    }
    status->user_portal_url = *url;
  }
  if (const std::string* value = dict->FindString(kVenueInfoUrlKey);
      value != nullptr) {
    const auto url = HttpUrl::CreateFromString(*value);
    if (!url.has_value()) {
      LOG(WARNING) << "Venue info URL is not invalid: " << *value;
      return std::nullopt;
    }
    status->venue_info_url = *url;
  }
  if (const std::optional<bool> value = dict->FindBool(kCanExtendSessionKey);
      value.has_value()) {
    status->can_extend_session = *value;
  }
  if (const std::optional<int> value = dict->FindInt(kSecondsRemainingKey);
      value.has_value()) {
    status->seconds_remaining = base::Seconds(*value);
  }
  if (const std::optional<int> value = dict->FindInt(kBytesRemainingKey);
      value.has_value()) {
    status->bytes_remaining = *value;
  }

  return status;
}

std::unique_ptr<CapportProxy> CapportProxy::Create(
    std::string_view interface,
    std::string_view api_url,
    std::shared_ptr<brillo::http::Transport> http_transport,
    base::TimeDelta transport_timeout) {
  const std::optional<HttpUrl> url = HttpUrl::CreateFromString(api_url);
  if (!url.has_value() || url->protocol() != HttpUrl::Protocol::kHttps) {
    LOG(ERROR) << "The URL of CAPPORT API is invalid: " << api_url;
    return nullptr;
  }

  http_transport->SetInterface(std::string(interface));
  http_transport->SetDefaultTimeout(transport_timeout);
  return std::make_unique<CapportProxy>(api_url, std::move(http_transport),
                                        std::string(interface) + ": ");
}

CapportProxy::CapportProxy(
    std::string_view api_url,
    std::shared_ptr<brillo::http::Transport> http_transport,
    std::string_view logging_tag)
    : api_url_(api_url),
      http_transport_(std::move(http_transport)),
      logging_tag_(logging_tag) {}
CapportProxy::~CapportProxy() = default;

void CapportProxy::SendRequest(StatusCallback callback) {
  LOG_IF(DFATAL, IsRunning())
      << logging_tag_ << "The previous request is still running";
  callback_ = std::move(callback);

  // TODO(b/305129516): Add metrics to record latency and success/failure count.
  LOG_IF(WARNING, http_request_)
      << logging_tag_ << "The pending request is not clear";
  auto http_request_ = std::make_optional<brillo::http::Request>(
      api_url_, brillo::http::request_type::kGet, http_transport_);
  http_request_->SetAccept(kAcceptHeader);
  http_request_->GetResponse(
      base::BindOnce(&CapportProxy::OnRequestSuccess, base::Unretained(this)),
      base::BindOnce(&CapportProxy::OnRequestError, base::Unretained(this)));
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
               << api_url_
               << ") was not compliant, failed to parse JSON: " << json_str;
    std::move(callback_).Run(std::nullopt);
    return;
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

}  // namespace shill
