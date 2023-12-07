// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/portal_detector.h"

#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/pattern.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/http/http_request.h>
#include <chromeos/dbus/service_constants.h>

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
constexpr base::TimeDelta kMaxPortalCheckInterval = base::Minutes(1);

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
  config.portal_http_url =
      *net_base::HttpUrl::CreateFromString(kDefaultHttpUrl);
  config.portal_https_url =
      *net_base::HttpUrl::CreateFromString(kDefaultHttpsUrl);
  for (const auto& url_string : kDefaultFallbackHttpUrls) {
    config.portal_fallback_http_urls.push_back(
        *net_base::HttpUrl::CreateFromString(url_string));
  }
  for (const auto& url_string : kDefaultFallbackHttpsUrls) {
    config.portal_fallback_https_urls.push_back(
        *net_base::HttpUrl::CreateFromString(url_string));
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

const net_base::HttpUrl& PortalDetector::PickProbeUrl(
    const net_base::HttpUrl& default_url,
    const std::vector<net_base::HttpUrl>& fallback_urls) const {
  // Always start with the default URL.
  if (attempt_count_ == 0 || fallback_urls.empty()) {
    return default_url;
  }
  // Once the default URL has been used, always visit all fallback URLs in
  // order. |attempt_count_| is guaranteed superior or equal to 1.
  if (static_cast<uint32_t>(attempt_count_ - 1) < fallback_urls.size()) {
    return fallback_urls[attempt_count_ - 1];
  }
  // Otherwise, pick a URL at random with equal probability. Picking URLs at
  // random makes it harder for evasive portals to count probes.
  // TODO(b/309175584): Reavaluate if this behavior is really needed after m121
  // with the Network.Shill.PortalDetector.AttemptsToRedirectFound metric.
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

  // If this is not the first attempt and if the previous attempt found a
  // captive portal, reuse the same HTTP URL probe to ensure the same
  // kPortalRedirect result is returned.
  if (previous_result_ && previous_result_->IsHTTPProbeRedirected()) {
    http_url_ = *previous_result_->probe_url;
  } else {
    http_url_ = PickProbeUrl(probing_configuration_.portal_http_url,
                             probing_configuration_.portal_fallback_http_urls);
  }
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
  http_request_ = CreateHTTPRequest(ifname, ip_family, dns_list,
                                    /*allow_non_google_https=*/false);
  // For non-default URLs, allow for secure communication with both Google and
  // non-Google servers.
  bool allow_non_google_https = https_url_.ToString() != kDefaultHttpsUrl;
  https_request_ =
      CreateHTTPRequest(ifname, ip_family, dns_list, allow_non_google_https);
  trial_.Reset(base::BindOnce(&PortalDetector::StartTrialTask,
                              weak_ptr_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(FROM_HERE, trial_.callback(), delay);
}

void PortalDetector::StartTrialTask() {
  attempt_count_++;
  // Ensure that every trial increases the delay backoff exponent to prevent a
  // systematic failure from creating a hot loop.
  delay_backoff_exponent_++;
  result_ = Result();
  is_active_ = true;
  last_attempt_start_time_ = base::TimeTicks::Now();
  LOG(INFO) << LoggingTag()
            << ": Starting trial. HTTP probe: " << http_url_.host()
            << ". HTTPS probe: " << https_url_.host();
  http_request_->Start(
      LoggingTag() + " HTTP probe", http_url_, kHeaders,
      base::BindOnce(&PortalDetector::HttpRequestSuccessCallback,
                     weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&PortalDetector::HttpRequestErrorCallback,
                     weak_ptr_factory_.GetWeakPtr()));
  https_request_->Start(
      LoggingTag() + " HTTPS probe", https_url_, kHeaders,
      base::BindOnce(&PortalDetector::HttpsRequestSuccessCallback,
                     weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&PortalDetector::HttpsRequestErrorCallback,
                     weak_ptr_factory_.GetWeakPtr()));
}

void PortalDetector::CompleteTrial(Result result) {
  LOG(INFO) << LoggingTag() << ": Trial result: " << result.GetValidationState()
            << ". HTTP probe: dest=" << http_url_.host()
            << ", result=" << result.http_result
            << ", duration=" << result.http_duration
            << ". HTTPS probe: dest=" << https_url_.host()
            << ", result=" << result.https_error
            << ", duration=" << result.https_duration;
  result.num_attempts = attempt_count_;
  CleanupTrial();
  previous_result_ = result;
  portal_result_callback_.Run(result);
}

void PortalDetector::CleanupTrial() {
  trial_.Cancel();
  result_ = std::nullopt;
  http_request_.reset();
  https_request_.reset();
  is_active_ = false;
}

void PortalDetector::Stop() {
  SLOG(this, 3) << "In " << __func__;
  attempt_count_ = 0;
  delay_backoff_exponent_ = 0;
  previous_result_ = std::nullopt;
  CleanupTrial();
}

void PortalDetector::HttpRequestSuccessCallback(
    std::shared_ptr<brillo::http::Response> response) {
  int status_code = response->GetStatusCode();
  result_->http_probe_completed = true;
  result_->http_status_code = status_code;
  result_->http_content_length = GetContentLength(response);
  if (status_code == brillo::http::status_code::NoContent) {
    result_->http_result = HTTPProbeResult::kSuccess;
  } else if (status_code == brillo::http::status_code::Ok) {
    // 200 responses are treated as 204 responses if there is no content. This
    // is consistent with AOSP and helps support networks that transparently
    // proxy or redirect web content but do not handle 204 content completely
    // correctly. See b/33498325 for an example. In addition, single byte
    // answers are also considered as 204 responses (b/122999481).
    if (result_->http_content_length == 0 ||
        result_->http_content_length == 1) {
      result_->http_result = HTTPProbeResult::kSuccess;
    } else if (result_->http_content_length.value_or(0) > 1) {
      // Any 200 response including some content in the response body is a
      // strong indication of an evasive portal indirectly redirecting the HTTP
      // probe without a 302 response code.
      // TODO(b/309175584): Validate that the response is a valid HTML page
      result_->probe_url = http_url_;
      result_->http_result = HTTPProbeResult::kPortalSuspected;
    } else {
      result_->http_result = HTTPProbeResult::kFailure;
    }
  } else if (IsRedirectResponse(status_code)) {
    // If a redirection code is received, verify that there is a valid Location
    // redirection URL, otherwise consider the HTTP probe as failed.
    std::string redirect_url_string =
        response->GetHeader(brillo::http::response_header::kLocation);
    if (!redirect_url_string.empty()) {
      result_->probe_url = http_url_;
      result_->redirect_url =
          net_base::HttpUrl::CreateFromString(redirect_url_string);
      if (result_->redirect_url) {
        LOG(INFO) << LoggingTag() << ": Redirect response, Redirect URL: "
                  << redirect_url_string
                  << ", response status code: " << status_code;
        result_->http_result = HTTPProbeResult::kPortalRedirect;
      } else {
        // Do not log the Location header if it is not a valid URL and cannot
        // be obfuscated by the redaction tool.
        LOG(INFO) << LoggingTag() << ": Received redirection status code "
                  << status_code << " but Location header was not a valid URL.";
        result_->http_result = HTTPProbeResult::kPortalInvalidRedirect;
      }
    } else {
      LOG(INFO) << LoggingTag() << ": Received redirection status code "
                << status_code << " but there was no Location header";
      result_->http_result = HTTPProbeResult::kPortalInvalidRedirect;
    }
  } else {
    // Any other result is considered a failure.
    result_->http_result = HTTPProbeResult::kFailure;
  }
  result_->http_duration = base::TimeTicks::Now() - last_attempt_start_time_;
  LOG(INFO) << LoggingTag() << ": HTTP probe " << http_url_.host()
            << " result=" << result_->http_result
            << " response status code=" << status_code
            << " content_length=" << result_->http_content_length.value_or(0)
            << " duration=" << result_->http_duration;
  if (result_->IsComplete()) {
    CompleteTrial(*result_);
  }
}

void PortalDetector::HttpsRequestSuccessCallback(
    std::shared_ptr<brillo::http::Response> response) {
  // Assume that HTTPS prevent any tempering with the content of the response
  // and always consider the HTTPS probe as successful if the request completed.
  result_->https_probe_completed = true;
  result_->https_duration = base::TimeTicks::Now() - last_attempt_start_time_;
  LOG(INFO) << LoggingTag() << ": HTTPS probe " << https_url_.host()
            << " succeessful, duration=" << result_->https_duration;
  if (result_->IsComplete())
    CompleteTrial(*result_);
}

void PortalDetector::HttpRequestErrorCallback(HttpRequest::Error http_error) {
  result_->http_probe_completed = true;
  result_->http_result = GetHTTPProbeStatusFromRequestError(http_error);
  result_->http_duration = base::TimeTicks::Now() - last_attempt_start_time_;
  LOG(INFO) << LoggingTag() << ": HTTP probe " << http_url_.host()
            << " failed: " << http_error
            << ", duration=" << result_->http_duration;
  if (result_->IsComplete()) {
    CompleteTrial(*result_);
  }
}

void PortalDetector::HttpsRequestErrorCallback(HttpRequest::Error https_error) {
  result_->https_error = https_error;
  result_->https_probe_completed = true;
  result_->https_duration = base::TimeTicks::Now() - last_attempt_start_time_;
  LOG(INFO) << LoggingTag() << ": HTTPS probe " << https_url_.host()
            << " failed: " << https_error
            << ", duration=" << result_->https_duration;
  if (result_->IsComplete()) {
    CompleteTrial(*result_);
  }
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

std::optional<size_t> PortalDetector::GetContentLength(
    std::shared_ptr<brillo::http::Response> response) const {
  std::string content_length_string =
      response->GetHeader(brillo::http::response_header::kContentLength);
  if (content_length_string.empty()) {
    LOG(WARNING) << LoggingTag() << "Missing Content-Length";
    // If there is no Content-Length header, use the size of the actual response
    // data.
    return response->ExtractData().size();
  }
  size_t content_length = 0;
  if (!base::StringToSizeT(content_length_string, &content_length)) {
    LOG(WARNING) << LoggingTag()
                 << "Invalid Content-Length: " << content_length_string;
    return std::nullopt;
  }
  return content_length;
}

// static
std::string_view PortalDetector::HTTPProbeResultName(HTTPProbeResult result) {
  switch (result) {
    case HTTPProbeResult::kNoResult:
      return "No result";
    case HTTPProbeResult::kDNSFailure:
      return "DNS failure";
    case HTTPProbeResult::kDNSTimeout:
      return "DNS timeout";
    case HTTPProbeResult::kConnectionFailure:
      return "Connection failure";
    case HTTPProbeResult::kHTTPTimeout:
      return "Request timeout";
    case HTTPProbeResult::kSuccess:
      return "Success";
    case HTTPProbeResult::kPortalSuspected:
      return "Portal suspected";
    case HTTPProbeResult::kPortalRedirect:
      return "Portal redirect";
    case HTTPProbeResult::kPortalInvalidRedirect:
      return "Portal invalid redirect";
    case HTTPProbeResult::kFailure:
      return "Failure";
  }
}

// static
std::string_view PortalDetector::ValidationStateToString(
    ValidationState state) {
  switch (state) {
    case ValidationState::kInternetConnectivity:
      return "internet-connectivity";
    case ValidationState::kNoConnectivity:
      return "no-connectivity";
    case ValidationState::kPortalSuspected:
      return "portal-suspected";
    case ValidationState::kPortalRedirect:
      return "portal-redirect";
  }
}

// static
PortalDetector::HTTPProbeResult
PortalDetector::GetHTTPProbeStatusFromRequestError(HttpRequest::Error error) {
  switch (error) {
    case HttpRequest::Error::kDNSFailure:
      return HTTPProbeResult::kDNSFailure;
    case HttpRequest::Error::kDNSTimeout:
      return HTTPProbeResult::kDNSTimeout;
    case HttpRequest::Error::kHTTPTimeout:
      return HTTPProbeResult::kHTTPTimeout;
    case HttpRequest::Error::kInternalError:
    case HttpRequest::Error::kConnectionFailure:
    case HttpRequest::Error::kTLSFailure:
    case HttpRequest::Error::kIOError:
      return HTTPProbeResult::kConnectionFailure;
  }
}

PortalDetector::ValidationState PortalDetector::Result::GetValidationState()
    const {
  // If both probes succeed as expected, classify the result as "Internet
  // connectivity".
  if (IsHTTPSProbeSuccessful() && IsHTTPProbeSuccessful()) {
    return ValidationState::kInternetConnectivity;
  }
  // If the HTTP probe is cleanly redirected, classify the result as "portal
  // redirect".
  if (IsHTTPProbeRedirected()) {
    return ValidationState::kPortalRedirect;
  }
  // Check if the HTTP answer is suspected to originate from a captive portal.
  if (IsHTTPProbeRedirectionSuspected()) {
    return ValidationState::kPortalSuspected;
  }
  // Any other result is considered as "no connectivity".
  return ValidationState::kNoConnectivity;
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
  if (IsRedirectResponse(http_status_code) && !IsHTTPProbeRedirected()) {
    return Metrics::kPortalDetectorHTTPResponseCodeIncompleteRedirect;
  }
  // Count 200 responses with an invalid Content-Length separately.
  if (http_status_code == brillo::http::status_code::Ok &&
      !http_content_length) {
    return Metrics::kPortalDetectorHTTPResponseCodeNoContentLength200;
  }
  // Otherwise, return the response code directly.
  return http_status_code;
}

std::string PortalDetector::LoggingTag() const {
  return logging_tag_ + " attempt=" + std::to_string(attempt_count_);
}

std::unique_ptr<HttpRequest> PortalDetector::CreateHTTPRequest(
    const std::string& ifname,
    net_base::IPFamily ip_family,
    const std::vector<net_base::IPAddress>& dns_list,
    bool allow_non_google_https) const {
  return std::make_unique<HttpRequest>(dispatcher_, ifname, ip_family, dns_list,
                                       allow_non_google_https);
}

bool PortalDetector::Result::IsComplete() const {
  // Any HTTP probe result that triggers the Chrome sign-in portal UX flow
  // (portal redirect or portal suspected results) is enough to complete the
  // trial immediately. When the captive portal is silently dropping HTTPS
  // traffic, this allows to avoid waiting the full duration of the HTTPS probe
  // timeout and terminating the socket connection of the HTTPS probe early by
  // triggering CleanupTrial. Otherwise, the results of both probes is needed.
  return IsHTTPProbeRedirected() || IsHTTPProbeRedirectionSuspected() ||
         (http_probe_completed && https_probe_completed);
}

bool PortalDetector::Result::IsHTTPSProbeSuccessful() const {
  return https_probe_completed && !https_error;
}

bool PortalDetector::Result::IsHTTPProbeSuccessful() const {
  return http_probe_completed && http_result == HTTPProbeResult::kSuccess;
}

bool PortalDetector::Result::IsHTTPProbeRedirectionSuspected() const {
  if (!http_probe_completed) {
    return false;
  }
  // Any 200 response including some content in the response body is a strong
  // indication of an evasive portal indirectly redirecting the HTTP probe
  // without a 302 response code.
  if (http_result == HTTPProbeResult::kPortalSuspected) {
    return true;
  }
  // Any incomplete redirect 302 or 307 response without a Location header
  // is considered as misbehaving captive portal.
  if (http_result == HTTPProbeResult::kPortalInvalidRedirect) {
    return true;
  }
  return false;
}

bool PortalDetector::Result::IsHTTPProbeRedirected() const {
  return http_probe_completed &&
         http_result == HTTPProbeResult::kPortalRedirect && redirect_url;
}

Metrics::PortalDetectorResult PortalDetector::Result::GetResultMetric() const {
  switch (http_result) {
    case PortalDetector::HTTPProbeResult::kNoResult:
      return Metrics::kPortalDetectorResultUnknown;
    case PortalDetector::HTTPProbeResult::kDNSFailure:
      return Metrics::kPortalDetectorResultDNSFailure;
    case PortalDetector::HTTPProbeResult::kDNSTimeout:
      return Metrics::kPortalDetectorResultDNSTimeout;
    case PortalDetector::HTTPProbeResult::kConnectionFailure:
      return Metrics::kPortalDetectorResultConnectionFailure;
    case PortalDetector::HTTPProbeResult::kHTTPTimeout:
      return Metrics::kPortalDetectorResultHTTPTimeout;
    case PortalDetector::HTTPProbeResult::kSuccess:
      if (IsHTTPSProbeSuccessful()) {
        return Metrics::kPortalDetectorResultOnline;
      } else {
        return Metrics::kPortalDetectorResultHTTPSFailure;
      }
    case PortalDetector::HTTPProbeResult::kPortalSuspected:
      if (IsHTTPSProbeSuccessful()) {
        return Metrics::kPortalDetectorResultNoConnectivity;
      } else {
        return Metrics::kPortalDetectorResultHTTPSFailure;
      }
    case PortalDetector::HTTPProbeResult::kPortalRedirect:
      return Metrics::kPortalDetectorResultRedirectFound;
    case PortalDetector::HTTPProbeResult::kPortalInvalidRedirect:
      return Metrics::kPortalDetectorResultRedirectNoUrl;
    case PortalDetector::HTTPProbeResult::kFailure:
      return Metrics::kPortalDetectorResultHTTPFailure;
  }
}

bool operator==(const PortalDetector::ProbingConfiguration& lhs,
                const PortalDetector::ProbingConfiguration& rhs) = default;

bool PortalDetector::Result::operator==(
    const PortalDetector::Result& rhs) const {
  // Probe durations |http_duration| and |https_duration| are ignored.
  return http_result == rhs.http_result &&
         http_status_code == rhs.http_status_code &&
         http_content_length == rhs.http_content_length &&
         num_attempts == rhs.num_attempts && https_error == rhs.https_error &&
         http_probe_completed == rhs.http_probe_completed &&
         https_probe_completed == rhs.https_probe_completed &&
         redirect_url == rhs.redirect_url && probe_url == rhs.probe_url;
}

std::unique_ptr<PortalDetector> PortalDetectorFactory::Create(
    EventDispatcher* dispatcher,
    const PortalDetector::ProbingConfiguration& probing_configuration,
    base::RepeatingCallback<void(const PortalDetector::Result&)> callback) {
  return std::make_unique<PortalDetector>(dispatcher, probing_configuration,
                                          std::move(callback));
}

std::ostream& operator<<(std::ostream& stream,
                         PortalDetector::HTTPProbeResult result) {
  return stream << PortalDetector::HTTPProbeResultName(result);
}

std::ostream& operator<<(std::ostream& stream,
                         PortalDetector::ValidationState state) {
  return stream << PortalDetector::ValidationStateToString(state);
}

std::ostream& operator<<(std::ostream& stream, PortalDetector::Result result) {
  stream << "{ num_attempts=" << result.num_attempts << ", HTTP probe "
         << (result.http_probe_completed ? "completed" : "in-flight")
         << " result=" << result.http_result
         << " code=" << result.http_status_code;
  if (result.http_content_length) {
    stream << " content-length=" << *result.http_content_length;
  }
  stream << " duration=" << result.http_duration << ", HTTPS probe "
         << (result.https_probe_completed ? "completed" : "in-flight")
         << " result=" << result.https_error
         << " duration=" << result.https_duration;
  if (result.redirect_url) {
    stream << ", redirect_url=" << result.redirect_url->ToString();
  }
  if (result.probe_url) {
    stream << ", probe_url=" << result.probe_url->ToString();
  }
  return stream << "}";
}

}  // namespace shill
