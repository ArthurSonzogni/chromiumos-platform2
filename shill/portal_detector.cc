// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/portal_detector.h"

#include <ostream>
#include <string>
#include <vector>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/pattern.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/dns_client.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/metrics.h"

namespace {
constexpr char kLinuxUserAgent[] =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/89.0.4389.114 Safari/537.36";
const brillo::http::HeaderList kHeaders{
    {brillo::http::request_header::kUserAgent, kLinuxUserAgent},
};

// Base time interval between two portal detection attempts. Should be doubled
// at every new attempt.
constexpr base::TimeDelta kPortalCheckInterval = base::Seconds(3);
// Min time delay between two portal detection attempts.
constexpr base::TimeDelta kMinPortalCheckDelay = base::Seconds(0);
// Max time interval between two portal detection attempts.
constexpr base::TimeDelta kMaxPortalCheckInterval = base::Minutes(5);

bool IsRedirectResponse(int status_code) {
  return status_code == brillo::http::status_code::Redirect ||
         status_code == brillo::http::status_code::RedirectKeepVerb;
}
}  // namespace

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kPortal;
static std::string ObjectID(const PortalDetector* pd) {
  return pd->LoggingTag();
}
}  // namespace Logging

// static
PortalDetector::ProbingConfiguration
PortalDetector::DefaultProbingConfiguration() {
  ProbingConfiguration config;
  config.portal_http_url = *HttpUrl::CreateFromString(kDefaultHttpUrl);
  config.portal_https_url = *HttpUrl::CreateFromString(kDefaultHttpsUrl);
  for (const auto& url_string : kDefaultFallbackHttpUrls) {
    config.portal_fallback_http_urls.push_back(
        *HttpUrl::CreateFromString(url_string));
  }
  for (const auto& url_string : kDefaultFallbackHttpsUrls) {
    config.portal_fallback_https_urls.push_back(
        *HttpUrl::CreateFromString(url_string));
  }
  return config;
}

PortalDetector::PortalDetector(
    EventDispatcher* dispatcher,
    const ProbingConfiguration& probing_configuration,
    base::RepeatingCallback<void(const Result&)> callback)
    : dispatcher_(dispatcher),
      portal_result_callback_(callback),
      probing_configuration_(probing_configuration) {}

PortalDetector::~PortalDetector() {
  Stop();
}

const HttpUrl& PortalDetector::PickProbeUrl(
    const HttpUrl& default_url,
    const std::vector<HttpUrl>& fallback_urls) const {
  if (attempt_count_ == 0 || fallback_urls.empty()) {
    return default_url;
  }
  uint32_t index = base::RandInt(0, fallback_urls.size());
  return index < fallback_urls.size() ? fallback_urls[index] : default_url;
}

void PortalDetector::Start(const std::string& ifname,
                           net_base::IPFamily ip_family,
                           const std::vector<net_base::IPAddress>& dns_list,
                           const std::string& logging_tag) {
  if (IsInProgress()) {
    LOG(INFO) << LoggingTag() << ": Attempt is already running";
    return;
  }

  logging_tag_ = logging_tag + " " + net_base::ToString(ip_family);

  SLOG(this, 3) << "In " << __func__;

  http_url_ = PickProbeUrl(probing_configuration_.portal_http_url,
                           probing_configuration_.portal_fallback_http_urls);
  https_url_ = PickProbeUrl(probing_configuration_.portal_https_url,
                            probing_configuration_.portal_fallback_https_urls);

  if (!trial_.IsCancelled()) {
    LOG(INFO) << LoggingTag() << ": Cancelling next scheduled trial";
    trial_.Cancel();
  }

  const auto delay = GetNextAttemptDelay();
  if (delay.is_positive()) {
    LOG(INFO) << logging_tag_ << ": Retrying in " << delay;
  }

  // TODO(hugobenichi) Network properties like src address and DNS should be
  // obtained exactly at the time that the trial starts if |GetNextAttemptDelay|
  // > 0.
  http_request_ =
      std::make_unique<HttpRequest>(dispatcher_, ifname, ip_family, dns_list);
  // For non-default URLs, allow for secure communication with both Google and
  // non-Google servers.
  bool allow_non_google_https = https_url_.ToString() == kDefaultHttpsUrl;
  https_request_ = std::make_unique<HttpRequest>(
      dispatcher_, ifname, ip_family, dns_list, allow_non_google_https);
  trial_.Reset(base::BindOnce(&PortalDetector::StartTrialTask,
                              weak_ptr_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(FROM_HERE, trial_.callback(), delay);
}

void PortalDetector::StartTrialTask() {
  attempt_count_++;
  // Ensure that every trial increases the delay backoff exponent to prevent a
  // systematic failure from creating a hot loop.
  delay_backoff_exponent_++;

  last_attempt_start_time_ = base::TimeTicks::Now();
  LOG(INFO) << LoggingTag()
            << ": Starting trial. HTTP probe: " << http_url_.host()
            << ". HTTPS probe: " << https_url_.host();
  HttpRequest::Result http_result = http_request_->Start(
      LoggingTag() + " HTTP probe", http_url_, kHeaders,
      base::BindOnce(&PortalDetector::HttpRequestSuccessCallback,
                     weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&PortalDetector::HttpRequestErrorCallback,
                     weak_ptr_factory_.GetWeakPtr()));
  if (http_result != HttpRequest::kResultInProgress) {
    // If the http probe fails to start, complete the trial with a failure
    // Result for https.
    LOG(ERROR) << LoggingTag()
               << ": HTTP probe failed to start. Aborting trial.";
    PortalDetector::Result result;
    result.http_phase = GetPortalPhaseForRequestResult(http_result);
    result.http_status = GetPortalStatusForRequestResult(http_result);
    result.https_phase = PortalDetector::Phase::kContent;
    result.https_status = PortalDetector::Status::kFailure;
    CompleteTrial(result);
    return;
  }

  result_ = std::make_unique<Result>();

  HttpRequest::Result https_result = https_request_->Start(
      LoggingTag() + " HTTPS probe", https_url_, kHeaders,
      base::BindOnce(&PortalDetector::HttpsRequestSuccessCallback,
                     weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&PortalDetector::HttpsRequestErrorCallback,
                     weak_ptr_factory_.GetWeakPtr()));
  if (https_result != HttpRequest::kResultInProgress) {
    result_->https_phase = GetPortalPhaseForRequestResult(https_result);
    result_->https_status = GetPortalStatusForRequestResult(https_result);
    LOG(ERROR) << LoggingTag() << ": HTTPS probe failed to start";
    // To find the portal sign-in url, wait for the HTTP probe to complete
    // before completing the trial and calling |portal_result_callback_|.
  }

  is_active_ = true;
}

void PortalDetector::CompleteTrial(Result result) {
  LOG(INFO) << LoggingTag() << ": Trial result: " << result.GetValidationState()
            << ". HTTP probe: dest=" << http_url_.host()
            << ", phase=" << result.http_phase
            << ", status=" << result.http_status
            << ", duration=" << result.http_duration
            << ". HTTPS probe: dest=" << https_url_.host()
            << ", phase=" << result.https_phase
            << ", duration=" << result.https_duration;
  result.num_attempts = attempt_count_;
  CleanupTrial();
  portal_result_callback_.Run(result);
}

void PortalDetector::CleanupTrial() {
  trial_.Cancel();
  result_.reset();
  http_request_.reset();
  https_request_.reset();
  is_active_ = false;
}

void PortalDetector::Stop() {
  SLOG(this, 3) << "In " << __func__;
  attempt_count_ = 0;
  delay_backoff_exponent_ = 0;
  CleanupTrial();
}

void PortalDetector::HttpRequestSuccessCallback(
    std::shared_ptr<brillo::http::Response> response) {
  // TODO(matthewmwang): check for 0 length data as well
  int status_code = response->GetStatusCode();
  result_->http_probe_completed = true;
  result_->http_phase = Phase::kContent;
  result_->http_status_code = status_code;
  if (status_code == brillo::http::status_code::NoContent) {
    result_->http_status = Status::kSuccess;
  } else if (IsRedirectResponse(status_code)) {
    result_->http_status = Status::kRedirect;
    std::string redirect_url_string =
        response->GetHeader(brillo::http::response_header::kLocation);
    if (redirect_url_string.empty()) {
      LOG(INFO) << LoggingTag() << ": Received redirection status code "
                << status_code << " but there was no Location header";
    } else {
      const auto redirect_url = HttpUrl::CreateFromString(redirect_url_string);
      if (!redirect_url) {
        // Do not log |redirect_url_string| if it is not a valid URL and cannot
        // be obfuscated by the redaction tool.
        LOG(INFO) << LoggingTag() << ": Received redirection status code "
                  << status_code << " but Location header was not a valid URL.";
        result_->http_status = Status::kFailure;
      } else {
        LOG(INFO) << LoggingTag() << ": Redirect response, Redirect URL: "
                  << redirect_url_string
                  << ", response status code: " << status_code;
        result_->redirect_url_string = redirect_url_string;
        result_->probe_url_string = http_url_.ToString();
      }
    }
  } else {
    result_->http_status = Status::kFailure;
  }
  result_->http_duration = base::TimeTicks::Now() - last_attempt_start_time_;
  LOG(INFO) << LoggingTag() << ": HTTP probe " << http_url_.host()
            << " response status code=" << status_code
            << " status=" << result_->http_status
            << " duration=" << result_->http_duration;
  if (result_->IsComplete())
    CompleteTrial(*result_);
}

void PortalDetector::HttpsRequestSuccessCallback(
    std::shared_ptr<brillo::http::Response> response) {
  int status_code = response->GetStatusCode();
  // The HTTPS probe is successful and indicates no portal was present only if
  // it gets the expected 204 status code. Any other result is a failure.
  result_->https_probe_completed = true;
  result_->https_phase = Phase::kContent;
  result_->https_status = (status_code == brillo::http::status_code::NoContent)
                              ? Status::kSuccess
                              : Status::kFailure;
  result_->https_duration = base::TimeTicks::Now() - last_attempt_start_time_;
  LOG(INFO) << LoggingTag() << ": HTTPS probe " << https_url_.host()
            << " response status code=" << status_code
            << " status=" << result_->https_status
            << " duration=" << result_->https_duration;
  if (result_->IsComplete())
    CompleteTrial(*result_);
}

void PortalDetector::HttpRequestErrorCallback(HttpRequest::Result http_result) {
  result_->http_probe_completed = true;
  result_->http_phase = GetPortalPhaseForRequestResult(http_result);
  result_->http_status = GetPortalStatusForRequestResult(http_result);
  result_->http_duration = base::TimeTicks::Now() - last_attempt_start_time_;
  LOG(INFO) << LoggingTag() << ": HTTP probe " << http_url_.host()
            << " failed with phase=" << result_->http_phase
            << " status=" << result_->http_status
            << " duration=" << result_->http_duration;
  if (result_->IsComplete())
    CompleteTrial(*result_);
}

void PortalDetector::HttpsRequestErrorCallback(
    HttpRequest::Result https_result) {
  result_->https_probe_completed = true;
  result_->https_phase = GetPortalPhaseForRequestResult(https_result);
  result_->https_status = GetPortalStatusForRequestResult(https_result);
  result_->https_duration = base::TimeTicks::Now() - last_attempt_start_time_;
  LOG(INFO) << LoggingTag() << ": HTTPS probe " << https_url_.host()
            << " failed with phase=" << result_->https_phase
            << " status=" << result_->https_status
            << " duration=" << result_->https_duration;
  if (result_->IsComplete())
    CompleteTrial(*result_);
}

bool PortalDetector::IsInProgress() const {
  return is_active_;
}

bool PortalDetector::IsTrialScheduled() const {
  return !is_active_ && !trial_.IsCancelled();
}

void PortalDetector::ResetAttemptDelays() {
  delay_backoff_exponent_ = 0;
}

base::TimeDelta PortalDetector::GetNextAttemptDelay() const {
  if (delay_backoff_exponent_ == 0) {
    return base::TimeDelta();
  }
  base::TimeDelta next_interval =
      kPortalCheckInterval * (1 << (delay_backoff_exponent_ - 1));
  if (next_interval > kMaxPortalCheckInterval) {
    next_interval = kMaxPortalCheckInterval;
  }
  const auto next_attempt = last_attempt_start_time_ + next_interval;
  const auto now = base::TimeTicks::Now();
  auto next_delay = next_attempt - now;
  if (next_delay < kMinPortalCheckDelay) {
    next_delay = kMinPortalCheckDelay;
  }
  return next_delay;
}

// static
const std::string PortalDetector::PhaseToString(Phase phase) {
  switch (phase) {
    case Phase::kConnection:
      return kPortalDetectionPhaseConnection;
    case Phase::kDNS:
      return kPortalDetectionPhaseDns;
    case Phase::kHTTP:
      return kPortalDetectionPhaseHttp;
    case Phase::kContent:
      return kPortalDetectionPhaseContent;
    case Phase::kUnknown:
    default:
      return kPortalDetectionPhaseUnknown;
  }
}

// static
const std::string PortalDetector::StatusToString(Status status) {
  switch (status) {
    case Status::kSuccess:
      return kPortalDetectionStatusSuccess;
    case Status::kTimeout:
      return kPortalDetectionStatusTimeout;
    case Status::kRedirect:
      return kPortalDetectionStatusRedirect;
    case Status::kFailure:
    default:
      return kPortalDetectionStatusFailure;
  }
}

// static
const std::string PortalDetector::ValidationStateToString(
    ValidationState state) {
  switch (state) {
    case ValidationState::kInternetConnectivity:
      return "internet-connectivity";
    case ValidationState::kNoConnectivity:
      return "no-connectivity";
    case ValidationState::kPartialConnectivity:
      return "partial-connectivity";
    case ValidationState::kPortalRedirect:
      return "portal-redirect";
  }
}

// static
PortalDetector::Phase PortalDetector::GetPortalPhaseForRequestResult(
    HttpRequest::Result result) {
  switch (result) {
    case HttpRequest::kResultSuccess:
      return Phase::kContent;
    case HttpRequest::kResultDNSFailure:
      return Phase::kDNS;
    case HttpRequest::kResultDNSTimeout:
      return Phase::kDNS;
    case HttpRequest::kResultConnectionFailure:
      return Phase::kConnection;
    case HttpRequest::kResultHTTPFailure:
      return Phase::kHTTP;
    case HttpRequest::kResultHTTPTimeout:
      return Phase::kHTTP;
    case HttpRequest::kResultInvalidInput:
    case HttpRequest::kResultUnknown:
    default:
      return Phase::kUnknown;
  }
}

// static
PortalDetector::Status PortalDetector::GetPortalStatusForRequestResult(
    HttpRequest::Result result) {
  switch (result) {
    case HttpRequest::kResultSuccess:
      // The request completed without receiving the expected payload.
      return Status::kFailure;
    case HttpRequest::kResultDNSFailure:
      return Status::kFailure;
    case HttpRequest::kResultDNSTimeout:
      return Status::kTimeout;
    case HttpRequest::kResultConnectionFailure:
      return Status::kFailure;
    case HttpRequest::kResultHTTPFailure:
      return Status::kFailure;
    case HttpRequest::kResultHTTPTimeout:
      return Status::kTimeout;
    case HttpRequest::kResultInvalidInput:
    case HttpRequest::kResultUnknown:
    default:
      return Status::kFailure;
  }
}

PortalDetector::ValidationState PortalDetector::Result::GetValidationState()
    const {
  if (http_phase != PortalDetector::Phase::kContent) {
    return ValidationState::kNoConnectivity;
  }
  if (http_status == PortalDetector::Status::kSuccess &&
      https_status == PortalDetector::Status::kSuccess) {
    return ValidationState::kInternetConnectivity;
  }
  if (http_status == PortalDetector::Status::kRedirect) {
    return redirect_url_string.empty() ? ValidationState::kPartialConnectivity
                                       : ValidationState::kPortalRedirect;
  }
  if (http_status == PortalDetector::Status::kTimeout &&
      https_status != PortalDetector::Status::kSuccess) {
    return ValidationState::kNoConnectivity;
  }
  return ValidationState::kPartialConnectivity;
}

std::optional<int> PortalDetector::Result::GetHTTPResponseCodeMetricResult()
    const {
  // Check if the HTTP probe completed.
  if (http_status_code == 0) {
    return std::nullopt;
  }
  // Reject invalid status codes not defined in RFC9110.
  if (http_status_code < 100 || 599 < http_status_code) {
    return Metrics::kPortalDetectorHTTPResponseCodeInvalid;
  }
  // For redirect responses, verify there was a valid redirect URL.
  if (IsRedirectResponse(http_status_code) && redirect_url_string.empty()) {
    return Metrics::kPortalDetectorHTTPResponseCodeIncompleteRedirect;
  }
  // Otherwise, return the response code directly.
  return http_status_code;
}

std::string PortalDetector::LoggingTag() const {
  return logging_tag_ + " attempt=" + std::to_string(attempt_count_);
}

bool PortalDetector::Result::IsComplete() const {
  if (!http_probe_completed) {
    return false;
  }
  // If the HTTP probe was redirected and a Location URL was received, the
  // result is unambiguously kPortalRedirect and the trial can complete
  // immediately. This allows to abort the HTTPS probe and avoids waiting the
  // full duration of the HTTPS probe timeout if the captive portal is silently
  // dropping HTTPS traffic when closed.
  if (!redirect_url_string.empty()) {
    return true;
  }
  return https_probe_completed;
}

std::ostream& operator<<(std::ostream& stream, PortalDetector::Phase phase) {
  return stream << PortalDetector::PhaseToString(phase);
}

std::ostream& operator<<(std::ostream& stream, PortalDetector::Status status) {
  return stream << PortalDetector::StatusToString(status);
}

std::ostream& operator<<(std::ostream& stream,
                         PortalDetector::ValidationState state) {
  return stream << PortalDetector::ValidationStateToString(state);
}

}  // namespace shill
