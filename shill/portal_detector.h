// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_PORTAL_DETECTOR_H_
#define SHILL_PORTAL_DETECTOR_H_

#include <array>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <brillo/http/http_request.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <net-base/ip_address.h>

#include "shill/http_request.h"
#include "shill/http_url.h"

namespace shill {

class EventDispatcher;

// The PortalDetector class implements the portal detection facility in shill,
// which is responsible for checking to see if a connection has "general
// internet connectivity".
//
// This information can be used for ranking one connection against another, or
// for informing UI and other components outside the connection manager whether
// the connection seems available for "general use" or if further user action
// may be necessary (e.g, click through of a WiFi Hotspot's splash page).
//
// This is achieved by using one or more trial attempts to access a URL and
// expecting a specific response.  Any result that deviates from this result
// (DNS or HTTP errors, as well as deviations from the expected content) are
// considered failures.
//
// In case of an inclusive attempt (the network was not validated and a portal
// was not found), the retry logic is controlled by the class owning the
// instance of PortalDetector. To avoid unnecessary network activity, retries
// should be separated from each other by a delay that progressively increases,
// starting with fast retries. PortalDetector provides the GetNextAttemptDelay()
// function which computes a delay to reinject into Start() and implements the
// following exponential backoff strategy:
//   - the first attempt is started immediately when Start() is called if the
//   default value for |delay| is used (0 second).
//   - to obtain the next value of |delay|, GetNextAttemptDelay() should be
//   called just before the next call to Start(). This is because
//   GetNextAttemptDelay() takes into account the total elapsed time since the
//   beginning of the previous attempt.
//   - the value returned by GetNextAttemptDelay() is guaranteed to be bound
//   within [|kMinPortalCheckDelay|,|kMaxPortalCheckInterval|] (see
//   implementation file).
//   - the value returned by GetNextAttemptDelay() will grow exponentially based
//   on the number of previous attempts, until it saturates at
//   kMaxPortalCheckInterval. The growth factor is controlled by the
//   |kPortalCheckInterval| parameter.
class PortalDetector {
 public:
  // Default URL used for the first HTTP probe sent by PortalDetector on a new
  // network connection.
  static constexpr char kDefaultHttpUrl[] =
      "http://www.gstatic.com/generate_204";
  // Default URL used for the first HTTPS probe sent by PortalDetector on a new
  // network connection.
  static constexpr char kDefaultHttpsUrl[] =
      "https://www.google.com/generate_204";
  // Set of fallback URLs used for retrying the HTTP probe when portal detection
  // is not conclusive.
  static constexpr std::array<const char*, 3> kDefaultFallbackHttpUrls = {
      "http://safebrowsing.google.com/generate_204",
      "http://www.googleapis.com/generate_204",
      "http://connectivitycheck.gstatic.com/generate_204",
  };
  // Set of fallback URLs used for retrying the HTTPS probe when portal
  // detection is not conclusive.
  static constexpr std::array<const char*, 3> kDefaultFallbackHttpsUrls = {
      "https://www.gstatic.com/generate_204",
      "https://accounts.google.com/generate_204",
      "https://www.googleapis.com/generate_204",
  };
  // Default comma separated list of technologies for which portal detection is
  // enabled.
  static constexpr char kDefaultCheckPortalList[] = "ethernet,wifi,cellular";

  // List of URLs to use for portal detection probes.
  struct ProbingConfiguration {
    // URL used for the first HTTP probe sent by PortalDetector on a new network
    // connection.
    HttpUrl portal_http_url;
    // URL used for the first HTTPS probe sent by PortalDetector on a new
    // network connection.
    HttpUrl portal_https_url;
    // Set of fallback URLs used for retrying the HTTP probe when portal
    // detection is not conclusive.
    std::vector<HttpUrl> portal_fallback_http_urls;
    // Set of fallback URLs used for retrying the HTTPS probe when portal
    // detection is not conclusive.
    std::vector<HttpUrl> portal_fallback_https_urls;
  };

  // Returns the default ProbingConfiguration using the default URLs defined in
  // PortalDetector.
  static ProbingConfiguration DefaultProbingConfiguration();

  // The Phase enum indicates the phase at which the probe fails.
  enum class Phase {
    kUnknown,
    kConnection,  // Failure to establish connection with server
    kDNS,         // Failure to resolve hostname or DNS server failure
    kHTTP,        // Failure to read or write to server
    kContent      // Content mismatch in response
  };

  enum class Status { kFailure, kSuccess, kTimeout, kRedirect };

  // Represents the possible outcomes of a portal detection attempt.
  enum class ValidationState {
    // All validation probes have succeeded with the expected
    // result.
    kInternetConnectivity,
    // Validation probes have all failed or timed out.
    kNoConnectivity,
    // Some validation probes have failed or timed out.
    kPartialConnectivity,
    // The HTTP probe has been redirected to a different location.
    kPortalRedirect,
  };

  // Represents the detailed result of a complete portal detection attempt (DNS
  // resolution, HTTP probe, HTTPS probe).
  struct Result {
    // Final Phase of the HTTP probe when the trial finished.
    Phase http_phase = Phase::kUnknown;
    // Final Status of the HTTP probe when the trial finished.
    Status http_status = Status::kFailure;
    // Final Phase of the HTTPS probe when the trial finished.
    Phase https_phase = Phase::kUnknown;
    // Final Status of the HTTPS probe when the trial finished.
    Status https_status = Status::kFailure;
    // The HTTP response status code from the http probe.
    int http_status_code = 0;
    // The HTTP response status code from the http probe.
    int https_status_code = 0;
    // The total number of trial attempts so far.
    int num_attempts;
    // Non-empty redirect URL if status is kRedirect.
    std::string redirect_url_string;
    // Probe URL used to reach redirect URL if status is kRedirect.
    std::string probe_url_string;

    // Boolean used for tracking the completion state of both http and https
    // probes.
    bool http_probe_completed = false;
    bool https_probe_completed = false;

    // Total HTTP and HTTPS probe durations, recorded if the respective probe
    // successfully started. The todal duration of the network validation
    // attempt is the longest of the two durations.
    base::TimeDelta http_duration = base::TimeDelta();
    base::TimeDelta https_duration = base::TimeDelta();

    // Returns true if both http and https probes have completed, successfully
    // or not.
    bool IsComplete() const;

    // Returns the ValidationState value inferred from this captive portal
    // detection result.
    ValidationState GetValidationState() const;

    // If the HTTP probe completed normally, returns a HTTP code equivalent to
    // log with UMA. Used for metrics only.
    std::optional<int> GetHTTPResponseCodeMetricResult() const;
  };

  PortalDetector(EventDispatcher* dispatcher,
                 const ProbingConfiguration& probing_configuration,
                 base::RepeatingCallback<void(const Result&)> callback);
  PortalDetector(const PortalDetector&) = delete;
  PortalDetector& operator=(const PortalDetector&) = delete;

  virtual ~PortalDetector();

  // Static method used to map a portal detection phase to a string.  This
  // includes the phases for connection, DNS, HTTP, returned content and
  // unknown.
  static const std::string PhaseToString(Phase phase);

  // Static method to map from the result of a portal detection phase to a
  // status string. This method supports success, timeout and failure.
  static const std::string StatusToString(Status status);

  // Static method to map from the validation state of a portal detection Result
  // to a string.
  static const std::string ValidationStateToString(ValidationState state);

  // Static method mapping from HttpRequest responses to PortalDetection
  // Phases. For example, if the HttpRequest result is kResultDNSFailure,
  // this method returns Phase::kDNS.
  static Phase GetPortalPhaseForRequestResult(HttpRequest::Result result);

  // Static method mapping from HttpRequest responses to PortalDetection
  // Status. For example, if the HttpRequest result is kResultDNSFailure,
  // this method returns Status::kFailure.
  static Status GetPortalStatusForRequestResult(HttpRequest::Result result);

  // Starts and schedules a new portal detection attempt according to the value
  // of GetNextAttemptDelay. If an attempt is already scheduled to run but has
  // not run yet, the new attempt will override the old attempt. Nothing happens
  // if an attempt is already running.
  virtual void Start(const std::string& ifname,
                     net_base::IPFamily ip_family,
                     const std::vector<net_base::IPAddress>& dns_list,
                     const std::string& logging_tag);

  // End the current portal detection process if one exists, and do not call
  // the callback.
  virtual void Stop();

  // Resets the delay calculation for scheduling retries requested with
  // Restart(). This has no impact on probe rotation logic.
  virtual void ResetAttemptDelays();

  // Returns the time delay for scheduling the next portal detection attempt
  // with Restart().
  base::TimeDelta GetNextAttemptDelay() const;

  // Returns whether portal request is "in progress".
  virtual bool IsInProgress() const;

  // Returns true if a new trial is scheduled to run but has not started yet.
  bool IsTrialScheduled() const;

  // Return |logging_tag_| appended with the |attempt_count_|.
  std::string LoggingTag() const;

  // Return the total number of detection attempts scheduled so far.
  int attempt_count() const { return attempt_count_; }

  // To use in unit tests only.
  void set_probing_configuration_for_testing(
      const ProbingConfiguration& probing_configuration) {
    probing_configuration_ = probing_configuration;
  }

 protected:
  base::RepeatingCallback<void(const Result&)>& portal_result_callback() {
    return portal_result_callback_;
  }

 private:
  friend class PortalDetectorTest;
  FRIEND_TEST(PortalDetectorTest, AttemptCount);
  FRIEND_TEST(PortalDetectorTest, AttemptCount);
  FRIEND_TEST(PortalDetectorTest, FailureToStartDoesNotCauseImmeidateRestart);
  FRIEND_TEST(PortalDetectorTest, GetNextAttemptDelayUnchangedUntilTrialStarts);
  FRIEND_TEST(PortalDetectorTest, HttpStartAttemptFailed);
  FRIEND_TEST(PortalDetectorTest, HttpsStartAttemptFailed);
  FRIEND_TEST(PortalDetectorTest, IsInProgress);
  FRIEND_TEST(PortalDetectorTest, MultipleRestarts);
  FRIEND_TEST(PortalDetectorTest, PickProbeUrlTest);
  FRIEND_TEST(PortalDetectorTest, RequestFail);
  FRIEND_TEST(PortalDetectorTest, RequestHTTPFailureHTTPSSuccess);
  FRIEND_TEST(PortalDetectorTest, RequestRedirect);
  FRIEND_TEST(PortalDetectorTest, RequestSuccess);
  FRIEND_TEST(PortalDetectorTest, RequestTempRedirect);
  FRIEND_TEST(PortalDetectorTest, ResetAttemptDelays);
  FRIEND_TEST(PortalDetectorTest, ResetAttemptDelaysAndRestart);
  FRIEND_TEST(PortalDetectorTest, Restart);

  // Picks the next probe URL based on |attempt_count_|. Returns |default_url|
  // if this is the first attempt. Otherwise, randomly returns with equal
  // probability |default_url| or an element of |fallback_urls|.
  const HttpUrl& PickProbeUrl(const HttpUrl& default_url,
                              const std::vector<HttpUrl>& fallback_urls) const;

  // Internal method used to start the actual connectivity trial, called after
  // the start delay completes.
  void StartTrialTask();

  // Callback used to return data read from the HTTP HttpRequest.
  void HttpRequestSuccessCallback(
      std::shared_ptr<brillo::http::Response> response);

  // Callback used to return data read from the HTTPS HttpRequest.
  void HttpsRequestSuccessCallback(
      std::shared_ptr<brillo::http::Response> response);

  // Callback used to return the error from the HTTP HttpRequest.
  void HttpRequestErrorCallback(HttpRequest::Result result);

  // Callback used to return the error from the HTTPS HttpRequest.
  void HttpsRequestErrorCallback(HttpRequest::Result result);

  // Called after each trial to return |result| after attempting to determine
  // connectivity status.
  void CompleteTrial(Result result);

  // Internal method used to cancel the timeout timer and stop an active
  // HttpRequest.
  void CleanupTrial();

  EventDispatcher* dispatcher_;
  std::string logging_tag_;
  bool is_active_ = false;
  // The total number of detection attempts scheduled so far. Only used in logs
  // for debugging purposes, and for selecting the URL of detection and
  // validation probes.
  int attempt_count_ = 0;
  // The power-of-two exponent used for computing exponentially increasing
  // delays between portal detection attempts.
  int delay_backoff_exponent_ = 0;
  // Timestamp updated when StartTrialTask runs and used to determine when to
  // schedule the next portal detection attempt after this one.
  base::TimeTicks last_attempt_start_time_ = base::TimeTicks();
  base::RepeatingCallback<void(const Result&)> portal_result_callback_;
  std::unique_ptr<HttpRequest> http_request_;
  std::unique_ptr<HttpRequest> https_request_;
  std::unique_ptr<Result> result_;
  HttpUrl http_url_;
  HttpUrl https_url_;
  ProbingConfiguration probing_configuration_;
  base::CancelableOnceClosure trial_;
  base::WeakPtrFactory<PortalDetector> weak_ptr_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, PortalDetector::Phase phase);
std::ostream& operator<<(std::ostream& stream, PortalDetector::Status status);
std::ostream& operator<<(std::ostream& stream,
                         PortalDetector::ValidationState state);

}  // namespace shill

#endif  // SHILL_PORTAL_DETECTOR_H_
