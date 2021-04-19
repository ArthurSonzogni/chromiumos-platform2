// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/portal_detector.h"

#include <string>

#include <base/bind.h>
#include <base/rand_util.h>
#include <base/strings/pattern.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/connection.h"
#include "shill/dns_client.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/metrics.h"
#include "shill/net/ip_address.h"

using base::Bind;
using base::Callback;
using std::string;

namespace {
const char kLinuxUserAgent[] =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/89.0.4389.114 Safari/537.36";
const brillo::http::HeaderList kHeaders{
    {brillo::http::request_header::kUserAgent, kLinuxUserAgent},
};
}  // namespace

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kPortal;
static string ObjectID(const Connection* c) {
  return c->interface_name();
}
}  // namespace Logging

const int PortalDetector::kInitialCheckIntervalSeconds = 3;
const int PortalDetector::kMaxPortalCheckIntervalSeconds = 5 * 60;
const char PortalDetector::kDefaultCheckPortalList[] = "ethernet,wifi,cellular";

const char PortalDetector::kDefaultHttpUrl[] =
    "http://www.gstatic.com/generate_204";
const char PortalDetector::kDefaultHttpsUrl[] =
    "https://www.google.com/generate_204";
const std::vector<string> PortalDetector::kDefaultFallbackHttpUrls{
    "http://www.google.com/gen_204",
    "http://play.googleapis.com/generate_204",
    "http://connectivitycheck.gstatic.com/generate_204",
};

PortalDetector::PortalDetector(
    ConnectionRefPtr connection,
    EventDispatcher* dispatcher,
    Metrics* metrics,
    const Callback<void(const PortalDetector::Result&,
                        const PortalDetector::Result&)>& callback)
    : attempt_count_(0),
      attempt_start_time_((struct timeval){0}),
      connection_(connection),
      dispatcher_(dispatcher),
      metrics_(metrics),
      weak_ptr_factory_(this),
      portal_result_callback_(callback),
      time_(Time::GetInstance()),
      is_active_(false) {}

PortalDetector::~PortalDetector() {
  Stop();
}

const string PortalDetector::PickHttpProbeUrl(const Properties& props) {
  if (attempt_count_ == 0 || props.fallback_http_url_strings.empty()) {
    return props.http_url_string;
  }
  return props.fallback_http_url_strings[base::RandInt(
      0, props.fallback_http_url_strings.size() - 1)];
}

bool PortalDetector::StartAfterDelay(const PortalDetector::Properties& props,
                                     int delay_seconds) {
  SLOG(connection_.get(), 3) << "In " << __func__;

  logging_tag_ = connection_->interface_name() + " " +
                 IPAddress::GetAddressFamilyName(connection_->local().family());

  // This step is rerun on each attempt, but trying it here will allow
  // Start() to abort on any obviously malformed URL strings.
  HttpUrl http_url, https_url;
  http_url_string_ = PickHttpProbeUrl(props);
  https_url_string_ = props.https_url_string;
  if (!http_url.ParseFromString(http_url_string_)) {
    LOG(ERROR) << LoggingTag() << ": Failed to parse HTTP probe URL string: "
               << props.http_url_string;
    return false;
  }

  if (!https_url.ParseFromString(https_url_string_)) {
    LOG(ERROR) << "Failed to parse HTTPS probe URL string: "
               << props.https_url_string;
    return false;
  }

  attempt_count_++;
  if (http_request_ || https_request_) {
    CleanupTrial();
  } else {
    const std::string& iface = connection_->interface_name();
    const IPAddress& src_address = connection_->local();
    const std::vector<std::string>& dns_list = connection_->dns_servers();
    http_request_ =
        std::make_unique<HttpRequest>(dispatcher_, LoggingTag() + " HTTP probe",
                                      iface, src_address, dns_list);
    // For non-default URLs, allow for secure communication with both Google and
    // non-Google servers.
    bool allow_non_google_https = (https_url_string_ != kDefaultHttpsUrl);
    https_request_ = std::make_unique<HttpRequest>(
        dispatcher_, LoggingTag() + " HTTPS probe", iface, src_address,
        dns_list, allow_non_google_https);
  }
  trial_.Reset(
      Bind(&PortalDetector::StartTrialTask, weak_ptr_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(FROM_HERE, trial_.callback(),
                               delay_seconds * 1000);
  // The attempt_start_time_ is calculated based on the current time and
  // |delay_seconds|.  This is used to determine if a portal detection attempt
  // is in progress.
  UpdateAttemptTime(delay_seconds);
  return true;
}

void PortalDetector::StartTrialTask() {
  LOG(INFO) << LoggingTag() << ": Starting trial";
  base::Callback<void(std::shared_ptr<brillo::http::Response>)>
      http_request_success_callback(
          Bind(&PortalDetector::HttpRequestSuccessCallback,
               weak_ptr_factory_.GetWeakPtr()));
  base::Callback<void(HttpRequest::Result)> http_request_error_callback(
      Bind(&PortalDetector::HttpRequestErrorCallback,
           weak_ptr_factory_.GetWeakPtr()));
  HttpRequest::Result http_result = http_request_->Start(
      http_url_string_, kHeaders, http_request_success_callback,
      http_request_error_callback);
  if (http_result != HttpRequest::kResultInProgress) {
    // If the http probe fails to start, complete the trial with a failure
    // Result for https.
    LOG(ERROR) << LoggingTag()
               << ": HTTP probe failed to start. Aborting trial.";
    CompleteTrial(PortalDetector::GetPortalResultForRequestResult(http_result),
                  Result(Phase::kContent, Status::kFailure));
    return;
  }

  base::Callback<void(std::shared_ptr<brillo::http::Response>)>
      https_request_success_callback(
          Bind(&PortalDetector::HttpsRequestSuccessCallback,
               weak_ptr_factory_.GetWeakPtr()));
  base::Callback<void(HttpRequest::Result)> https_request_error_callback(
      Bind(&PortalDetector::HttpsRequestErrorCallback,
           weak_ptr_factory_.GetWeakPtr()));
  HttpRequest::Result https_result = https_request_->Start(
      https_url_string_, kHeaders, https_request_success_callback,
      https_request_error_callback);
  if (https_result != HttpRequest::kResultInProgress) {
    https_result_ =
        std::make_unique<Result>(GetPortalResultForRequestResult(https_result));
    LOG(ERROR) << LoggingTag() << ": HTTPS probe failed to start";
    // To find the portal sign-in url, wait for the HTTP probe to complete
    // before completing the trial and calling |portal_result_callback_|.
  }
  is_active_ = true;
}

bool PortalDetector::IsActive() {
  return is_active_;
}

void PortalDetector::CompleteTrial(Result http_result, Result https_result) {
  LOG(INFO) << LoggingTag()
            << ": Trial completed. HTTP probe: phase=" << http_result.phase
            << ", status=" << http_result.status
            << ". HTTPS probe: phase=" << https_result.phase
            << ", status=" << https_result.status;
  http_result.num_attempts = attempt_count_;
  metrics_->NotifyPortalDetectionMultiProbeResult(http_result, https_result);
  CleanupTrial();
  portal_result_callback_.Run(http_result, https_result);
}

void PortalDetector::CleanupTrial() {
  http_result_.reset();
  https_result_.reset();
  if (http_request_)
    http_request_->Stop();
  if (https_request_)
    https_request_->Stop();

  is_active_ = false;
}

void PortalDetector::Stop() {
  SLOG(connection_.get(), 3) << "In " << __func__;

  attempt_count_ = 0;
  if (!http_request_ && !https_request_)
    return;

  CleanupTrial();
  http_request_.reset();
  https_request_.reset();
}

void PortalDetector::HttpRequestSuccessCallback(
    std::shared_ptr<brillo::http::Response> response) {
  // TODO(matthewmwang): check for 0 length data as well
  int status_code = response->GetStatusCode();
  if (status_code == brillo::http::status_code::NoContent) {
    http_result_ = std::make_unique<Result>(Phase::kContent, Status::kSuccess);
  } else if (status_code == brillo::http::status_code::Redirect) {
    http_result_ = std::make_unique<Result>(Phase::kContent, Status::kRedirect);
    string redirect_url_string =
        response->GetHeader(brillo::http::response_header::kLocation);
    if (redirect_url_string.empty()) {
      LOG(ERROR) << LoggingTag() << ": No Location field in redirect header.";
    } else {
      HttpUrl redirect_url;
      if (!redirect_url.ParseFromString(redirect_url_string)) {
        LOG(ERROR) << LoggingTag()
                   << ": Unable to parse redirect URL: " << redirect_url_string;
        http_result_->status = Status::kFailure;
      } else {
        LOG(INFO) << LoggingTag() << ": Redirect URL: " << redirect_url_string;
        http_result_->redirect_url_string = redirect_url_string;
        http_result_->probe_url_string = http_url_string_;
      }
    }
  } else {
    http_result_ = std::make_unique<Result>(Phase::kContent, Status::kFailure);
  }
  LOG(INFO) << LoggingTag() << ": HTTP probe response code=" << status_code
            << " status=" << http_result_->status;
  http_result_->status_code = status_code;
  if (https_result_)
    CompleteTrial(*http_result_, *https_result_);
}

void PortalDetector::HttpsRequestSuccessCallback(
    std::shared_ptr<brillo::http::Response> response) {
  int status_code = response->GetStatusCode();
  // The HTTPS probe is successful and indicates no portal was present only if
  // it gets the expected 204 status code. Any other result is a failure.
  Status probe_status = (status_code == brillo::http::status_code::NoContent)
                            ? Status::kSuccess
                            : Status::kFailure;
  LOG(INFO) << LoggingTag() << ": HTTPS probe response code=" << status_code
            << " status=" << probe_status;
  https_result_ = std::make_unique<Result>(Phase::kContent, probe_status);
  if (http_result_)
    CompleteTrial(*http_result_, *https_result_);
}

void PortalDetector::HttpRequestErrorCallback(HttpRequest::Result result) {
  http_result_ =
      std::make_unique<Result>(GetPortalResultForRequestResult(result));
  LOG(INFO) << LoggingTag()
            << ": HTTP probe failed with phase=" << http_result_.get()->phase
            << " status=" << http_result_.get()->status;
  if (https_result_)
    CompleteTrial(*http_result_, *https_result_);
}

void PortalDetector::HttpsRequestErrorCallback(HttpRequest::Result result) {
  https_result_ =
      std::make_unique<Result>(GetPortalResultForRequestResult(result));
  LOG(INFO) << LoggingTag()
            << ": HTTPS probe failed with phase=" << https_result_.get()->phase
            << " status=" << https_result_.get()->status;
  if (http_result_)
    CompleteTrial(*http_result_, *https_result_);
}

bool PortalDetector::IsInProgress() {
  return is_active_;
}

void PortalDetector::UpdateAttemptTime(int delay_seconds) {
  time_->GetTimeMonotonic(&attempt_start_time_);
  struct timeval delay_timeval = {delay_seconds, 0};
  timeradd(&attempt_start_time_, &delay_timeval, &attempt_start_time_);
}

int PortalDetector::AdjustStartDelay(int init_delay_seconds) {
  int next_attempt_delay_seconds = 0;
  if (attempt_count_ > 0) {
    struct timeval now, elapsed_time;
    time_->GetTimeMonotonic(&now);
    timersub(&now, &attempt_start_time_, &elapsed_time);
    SLOG(connection_.get(), 4) << "Elapsed time from previous attempt is "
                               << elapsed_time.tv_sec << " seconds.";
    if (elapsed_time.tv_sec < init_delay_seconds) {
      next_attempt_delay_seconds = init_delay_seconds - elapsed_time.tv_sec;
    }
  } else {
    LOG(FATAL) << "AdjustStartDelay in PortalDetector called without "
                  "previous attempts";
  }
  SLOG(connection_.get(), 3)
      << "Adjusting trial start delay from " << init_delay_seconds
      << " seconds to " << next_attempt_delay_seconds << " seconds.";
  return next_attempt_delay_seconds;
}

// static
const string PortalDetector::PhaseToString(Phase phase) {
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
const string PortalDetector::StatusToString(Status status) {
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

PortalDetector::Result PortalDetector::GetPortalResultForRequestResult(
    HttpRequest::Result result) {
  switch (result) {
    case HttpRequest::kResultSuccess:
      // The request completed without receiving the expected payload.
      return Result(Phase::kContent, Status::kFailure);
    case HttpRequest::kResultDNSFailure:
      return Result(Phase::kDNS, Status::kFailure);
    case HttpRequest::kResultDNSTimeout:
      return Result(Phase::kDNS, Status::kTimeout);
    case HttpRequest::kResultConnectionFailure:
      return Result(Phase::kConnection, Status::kFailure);
    case HttpRequest::kResultHTTPFailure:
      return Result(Phase::kHTTP, Status::kFailure);
    case HttpRequest::kResultHTTPTimeout:
      return Result(Phase::kHTTP, Status::kTimeout);
    case HttpRequest::kResultInvalidInput:
    case HttpRequest::kResultUnknown:
    default:
      return Result(Phase::kUnknown, Status::kFailure);
  }
}

std::string PortalDetector::LoggingTag() const {
  return logging_tag_ + " attempt=" + std::to_string(attempt_count_);
}

std::ostream& operator<<(std::ostream& stream, PortalDetector::Phase phase) {
  return stream << PortalDetector::PhaseToString(phase);
}

std::ostream& operator<<(std::ostream& stream, PortalDetector::Status status) {
  return stream << PortalDetector::StatusToString(status);
}

}  // namespace shill
