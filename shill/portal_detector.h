// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_PORTAL_DETECTOR_H_
#define SHILL_PORTAL_DETECTOR_H_

#include <array>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <brillo/http/http_request.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <net-base/http_url.h>
#include <net-base/ip_address.h>

#include "shill/http_request.h"
#include "shill/metrics.h"
#include "shill/mockable.h"

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
  // network connection. AOSP uses the same URL by default (defined in
  // packages/modules/NetworkStack/res/values/config.xml).
  static constexpr std::string_view kDefaultHttpUrl =
      "http://connectivitycheck.gstatic.com/generate_204";
  // Default URL used for the first HTTPS probe sent by PortalDetector on a new
  // network connection.
  static constexpr std::string_view kDefaultHttpsUrl =
      "https://www.google.com/generate_204";
  // Set of fallback URLs used for retrying the HTTP probe when portal detection
  // is not conclusive. AOSP uses the same first two URLS as fallback HTTP URLs
  // (defined in packages/modules/NetworkStack/res/values/config.xml).
  static constexpr std::array<std::string_view, 5> kDefaultFallbackHttpUrls = {
      "http://www.google.com/gen_204",
      "http://www.play.googleapis.com/generate_204",
      "http://www.gstatic.com/generate_204",
      "http://safebrowsing.google.com/generate_204",
      "http://www.googleapis.com/generate_204",
  };
  // Set of fallback URLs used for retrying the HTTPS probe when portal
  // detection is not conclusive.
  static constexpr std::array<std::string_view, 3> kDefaultFallbackHttpsUrls = {
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
    net_base::HttpUrl portal_http_url;
    // URL used for the first HTTPS probe sent by PortalDetector on a new
    // network connection.
    net_base::HttpUrl portal_https_url;
    // Set of fallback URLs used for retrying the HTTP probe when portal
    // detection is not conclusive.
    std::vector<net_base::HttpUrl> portal_fallback_http_urls;
    // Set of fallback URLs used for retrying the HTTPS probe when portal
    // detection is not conclusive.
    std::vector<net_base::HttpUrl> portal_fallback_https_urls;

    friend bool operator==(const ProbingConfiguration& lhs,
                           const ProbingConfiguration& rhs);
  };

  // Returns the default ProbingConfiguration using the default URLs defined in
  // PortalDetector.
  static ProbingConfiguration DefaultProbingConfiguration();

  // Represents the result of an HTTP or HTTPS probe. Both types of probe use
  // the same enum but not all results are relevant to both types:
  //  - HTTP probe: TLS failures are not possible.
  //  - HTTPS probe: The "portal" states are not possible (kPortalSuspected,
  //    kPortalRedirect, kPortalInvalidRedirect).
  enum class ProbeResult {
    // The probe has not completed yet.
    kNoResult,
    // Name resolution failed.
    kDNSFailure,
    // Name resolution timed out.
    kDNSTimeout,
    // HTTPS probe only: the TLS connection failed.
    kTLSFailure,
    // The HTTP connection failed.
    kConnectionFailure,
    // The HTTP request timed out.
    kHTTPTimeout,
    // The probe successfully completed with the expected result.
    kSuccess,
    // HTTP probe only: the probe completed but the result was unexpected
    // and indicates that there is a hidden captive portal network not
    // redirecting the probe.
    kPortalSuspected,
    // HTTP probe only: the probe completed but was redirected.
    kPortalRedirect,
    // HTTP probe only: the probe completed but was redirected and the
    // redirection was invalid.
    kPortalInvalidRedirect,
    // Another unknown error happened.
    kFailure,
  };

  // Static method used to map a portal detection phase to a string. This
  // includes the phases for connection, DNS, HTTP, returned content and
  // unknown.
  static std::string_view ProbeResultName(ProbeResult result);

  // Static method mapping from HttpRequest errors to PortalDetection
  // ProbeResult.
  static ProbeResult GetProbeResultFromRequestError(HttpRequest::Error error);

  // Represents the possible outcomes of a portal detection attempt.
  enum class ValidationState {
    // All validation probes have succeeded with the expected
    // result.
    kInternetConnectivity,
    // Some validation probes have failed or timed out and there was no
    // direct or suspected redirection of the HTTP probe.
    kNoConnectivity,
    // The HTTP probe received an unexpected answer that could be a captive
    // portal login page.
    kPortalSuspected,
    // The HTTP probe has been redirected to a different location.
    kPortalRedirect,
  };

  // Static method to map from the validation state of a portal detection Result
  // to a string.
  static std::string_view ValidationStateToString(ValidationState state);

  // Represents the detailed result of a complete portal detection attempt (DNS
  // resolution, HTTP probe, HTTPS probe).
  struct Result {
    // The total number of trial attempts so far.
    int num_attempts = 0;
    // The result of the HTTP probe.
    ProbeResult http_result = ProbeResult::kNoResult;
    // The HTTP response status code from the HTTP probe.
    int http_status_code = 0;
    // The content length of the HTTP response.
    std::optional<size_t> http_content_length = std::nullopt;
    // The result of the HTTPS probe.
    ProbeResult https_result = ProbeResult::kNoResult;
    // Redirection URL obtained from the Location header when the final
    // ValidationState of the Result if kPortalRedirect.
    std::optional<net_base::HttpUrl> redirect_url = std::nullopt;
    // URL of the HTTP probe when the final ValidationState of the Result is
    // either kPortalRedirect or kPortalSuspected.
    std::optional<net_base::HttpUrl> probe_url = std::nullopt;
    // Total HTTP and HTTPS probe durations, recorded if the respective probe
    // successfully started. The todal duration of the network validation
    // attempt is the longest of the two durations.
    base::TimeDelta http_duration = base::TimeDelta();
    base::TimeDelta https_duration = base::TimeDelta();

    // Returns true if the HTTP has completed, successfully or not.
    bool IsHTTPProbeComplete() const;
    // Returns true if the HTTPS has completed, successfully or not.
    bool IsHTTPSProbeComplete() const;
    // Returns true if both HTTP and HTTPS probes have completed, successfully
    // or not.
    bool IsComplete() const;
    // Returns true if the HTTPS probe was successful.
    bool IsHTTPSProbeSuccessful() const;
    // Returns true if the HTTP probe was successful and obtained a 204 result
    // or a 200 result with no content.
    bool IsHTTPProbeSuccessful() const;
    // Returns true if the HTTP probe was redirected and a redirection URL was
    // received.
    bool IsHTTPProbeRedirected() const;
    // Returns true if
    //  - the response to the HTTP probe indicates that a captive portal is
    //    spoofing the expected 204 result and inserting a login page instead.
    //  - the HTTP probe received a redirection code but the Location URL was
    //    missing or invalid.
    bool IsHTTPProbeRedirectionSuspected() const;

    // Returns the ValidationState value inferred from this captive portal
    // detection result.
    ValidationState GetValidationState() const;

    // If the HTTP probe completed normally, returns a HTTP code equivalent to
    // log with UMA. Used for metrics only.
    std::optional<int> GetHTTPResponseCodeMetricResult() const;

    Metrics::PortalDetectorResult GetResultMetric() const;

    // Compares with |rhs| for equality. Probe duration results |http_duration|
    // and |https_duration| values are ignored in the comparison.
    bool operator==(const Result& rhs) const;
  };

  PortalDetector(EventDispatcher* dispatcher,
                 const ProbingConfiguration& probing_configuration,
                 base::RepeatingCallback<void(const Result&)> callback);
  PortalDetector(const PortalDetector&) = delete;
  PortalDetector& operator=(const PortalDetector&) = delete;

  virtual ~PortalDetector();

  // Starts and schedules a new portal detection attempt according to the value
  // of GetNextAttemptDelay. If an attempt is already scheduled to run but has
  // not run yet, the new attempt will override the old attempt. Nothing happens
  // if an attempt is already running.
  mockable void Start(const std::string& ifname,
                      net_base::IPFamily ip_family,
                      const std::vector<net_base::IPAddress>& dns_list,
                      const std::string& logging_tag);

  // End the current portal detection process if one exists, and do not call
  // the callback.
  mockable void Stop();

  // Resets the delay calculation for scheduling retries requested with
  // Restart(). This has no impact on probe rotation logic.
  mockable void ResetAttemptDelays();

  // Returns the time delay for scheduling the next portal detection attempt
  // with Restart().
  base::TimeDelta GetNextAttemptDelay() const;

  // Returns whether portal request is "in progress".
  mockable bool IsInProgress() const;

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

  // Can be overwritten in tests;
  mockable std::unique_ptr<HttpRequest> CreateHTTPRequest(
      const std::string& ifname,
      net_base::IPFamily ip_family,
      const std::vector<net_base::IPAddress>& dns_list,
      bool allow_non_google_https) const;

 private:
  friend class PortalDetectorTest;
  FRIEND_TEST(PortalDetectorTest, AttemptCount);
  FRIEND_TEST(PortalDetectorTest, AttemptCount);
  FRIEND_TEST(PortalDetectorTest, FailureToStartDoesNotCauseImmediateRestart);
  FRIEND_TEST(PortalDetectorTest, GetNextAttemptDelayUnchangedUntilTrialStarts);
  FRIEND_TEST(PortalDetectorTest, HttpStartAttemptFailed);
  FRIEND_TEST(PortalDetectorTest, HttpsStartAttemptFailed);
  FRIEND_TEST(PortalDetectorTest, IsInProgress);
  FRIEND_TEST(PortalDetectorTest, MultipleRestarts);
  FRIEND_TEST(PortalDetectorTest, PickProbeURLs);
  FRIEND_TEST(PortalDetectorTest, Request200WithContent);
  FRIEND_TEST(PortalDetectorTest, RequestFail);
  FRIEND_TEST(PortalDetectorTest, RequestHTTPFailureHTTPSSuccess);
  FRIEND_TEST(PortalDetectorTest, RequestRedirect);
  FRIEND_TEST(PortalDetectorTest, RequestSuccess);
  FRIEND_TEST(PortalDetectorTest, RequestTempRedirect);
  FRIEND_TEST(PortalDetectorTest, ResetAttemptDelays);
  FRIEND_TEST(PortalDetectorTest, ResetAttemptDelaysAndRestart);
  FRIEND_TEST(PortalDetectorTest, Restart);
  FRIEND_TEST(PortalDetectorTest, RestartAfterRedirect);
  FRIEND_TEST(PortalDetectorTest, RestartAfterSuspectedRedirect);

  // Picks the next probe URL based on |attempt_count_|. Rotates first through
  // |default_url| and |fallback_urls| to pick each URL in order at least once,
  // then return randomely any URL with equal probability.
  const net_base::HttpUrl& PickProbeUrl(
      const net_base::HttpUrl& default_url,
      const std::vector<net_base::HttpUrl>& fallback_urls) const;

  // Internal method used to start the actual connectivity trial, called after
  // the start delay completes.
  void StartTrialTask();

  // Process the HttpRequest Result of the HTTP probe.
  void ProcessHTTPProbeResult(const HttpRequest::Result& result);

  // Process the HttpRequest Result of the HTTPS probe.
  void ProcessHTTPSProbeResult(const HttpRequest::Result& result);

  // Called after each trial to return |result| after attempting to determine
  // connectivity status.
  void CompleteTrial(Result result);

  // Internal method used to cancel the timeout timer and stop an active
  // HttpRequest.
  void CleanupTrial();

  // Extract the Content-Length from |response|.
  std::optional<size_t> GetContentLength(
      std::shared_ptr<brillo::http::Response> response) const;

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
  // PortalDetector::Result for the current on-going attempt. Undefined if there
  // is no portal detection attempt currently running.
  std::optional<Result> result_;
  // PortalDetector::Result of the prior attempt. Undefined for the first
  // attempt. Used to ensure that the same HTTP probe URL is used with a closed
  // captive portal.
  std::optional<Result> previous_result_ = std::nullopt;
  net_base::HttpUrl http_url_;
  net_base::HttpUrl https_url_;
  ProbingConfiguration probing_configuration_;
  base::CancelableOnceClosure trial_;
  base::WeakPtrFactory<PortalDetector> weak_ptr_factory_{this};
};

// The factory class of the PortalDetector, used to derive a mock factory to
// create mock PortalDetector instance at testing.
class PortalDetectorFactory {
 public:
  PortalDetectorFactory() = default;
  virtual ~PortalDetectorFactory() = default;

  // The default factory method, calling PortalDetector's constructor.
  mockable std::unique_ptr<PortalDetector> Create(
      EventDispatcher* dispatcher,
      const PortalDetector::ProbingConfiguration& probing_configuration,
      base::RepeatingCallback<void(const PortalDetector::Result&)> callback);
};

std::ostream& operator<<(std::ostream& stream,
                         PortalDetector::ProbeResult result);
std::ostream& operator<<(std::ostream& stream,
                         PortalDetector::ValidationState state);
std::ostream& operator<<(std::ostream& stream,
                         const PortalDetector::Result& result);

}  // namespace shill

#endif  // SHILL_PORTAL_DETECTOR_H_
