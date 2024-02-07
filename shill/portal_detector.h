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
    base::TimeDelta http_duration;
    base::TimeDelta https_duration;

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
  using ResultCallback = base::OnceCallback<void(const Result&)>;

  PortalDetector(EventDispatcher* dispatcher,
                 std::string_view ifname,
                 const ProbingConfiguration& probing_configuration,
                 std::string_view logging_tag);
  PortalDetector(const PortalDetector&) = delete;
  PortalDetector& operator=(const PortalDetector&) = delete;

  virtual ~PortalDetector();

  // Starts a new portal detection attempt. |callback| will be executed when the
  // attempt has been finished. Nothing happens and |callback| will be dropped
  // if an attempt is already running.
  // Note: |callback| won't be executed after the PortalDetector is destroyed.
  mockable void Start(net_base::IPFamily ip_family,
                      const std::vector<net_base::IPAddress>& dns_list,
                      ResultCallback callback);

  // Resets the instance to initial state. If the portal detection attempt is
  // running, then stop it and drop the callback.
  mockable void Reset();

  // Returns whether a portal detection attempt is running.
  mockable bool IsRunning() const;

  // Returns |logging_tag_| appended with the |ip_family_| and |attempt_count_|.
  std::string LoggingTag() const;

  // Returns the total number of detection attempts scheduled so far.
  mockable int attempt_count() const { return attempt_count_; }

  // To use in unit tests only.
  void set_probing_configuration_for_testing(
      const ProbingConfiguration& probing_configuration) {
    probing_configuration_ = probing_configuration;
  }

 protected:
  // Can be overwritten in tests;
  mockable std::unique_ptr<HttpRequest> CreateHTTPRequest(
      const std::string& ifname,
      net_base::IPFamily ip_family,
      const std::vector<net_base::IPAddress>& dns_list,
      bool allow_non_google_https) const;

 private:
  friend class PortalDetectorTest;
  FRIEND_TEST(PortalDetectorTest, AttemptCount);
  FRIEND_TEST(PortalDetectorTest, IsInProgress);
  FRIEND_TEST(PortalDetectorTest, PickProbeURLs);
  FRIEND_TEST(PortalDetectorTest, Request200WithContent);
  FRIEND_TEST(PortalDetectorTest, RequestFail);
  FRIEND_TEST(PortalDetectorTest, RequestHTTPFailureHTTPSSuccess);
  FRIEND_TEST(PortalDetectorTest, RequestRedirect);
  FRIEND_TEST(PortalDetectorTest, RequestSuccess);
  FRIEND_TEST(PortalDetectorTest, RequestTempRedirect);
  FRIEND_TEST(PortalDetectorTest, Restart);
  FRIEND_TEST(PortalDetectorTest, RestartAfterRedirect);
  FRIEND_TEST(PortalDetectorTest, RestartAfterSuspectedRedirect);

  // Picks the next probe URL based on |attempt_count_|. Rotates first through
  // |default_url| and |fallback_urls| to pick each URL in order at least once,
  // then return randomely any URL with equal probability.
  const net_base::HttpUrl& PickProbeUrl(
      const net_base::HttpUrl& default_url,
      const std::vector<net_base::HttpUrl>& fallback_urls) const;

  // Process the HttpRequest Result of the HTTP probe.
  void ProcessHTTPProbeResult(const net_base::HttpUrl& http_url,
                              HttpRequest::Result result);

  // Process the HttpRequest Result of the HTTPS probe.
  void ProcessHTTPSProbeResult(HttpRequest::Result result);

  // Called after each probe result to check if the current trial can be
  // stopped and if |portal_result_callback_| can be invoked.
  void StopTrialIfComplete(Result result);

  // Internal method used to cancel the timeout timer and stop an active
  // HttpRequest.
  void CleanupTrial();

  // Extract the Content-Length from |response|.
  std::optional<size_t> GetContentLength(
      brillo::http::Response* response) const;

  // These instances are not changed during the whole lifetime.
  EventDispatcher* dispatcher_;
  std::string ifname_;
  ProbingConfiguration probing_configuration_;
  std::string logging_tag_;

  // The callback that returns the result to the caller. It's not null if and
  // only if there is an attempt running.
  ResultCallback result_callback_;

  // The IP family of the current trial. Used for logging.
  std::optional<net_base::IPFamily> ip_family_ = std::nullopt;
  // The total number of detection attempts scheduled so far. Only used in logs
  // for debugging purposes, and for selecting the URL of detection and
  // validation probes.
  int attempt_count_ = 0;
  // Timestamp updated when StartTrialTask runs and used to determine when to
  // schedule the next portal detection attempt after this one.
  base::TimeTicks last_attempt_start_time_;
  std::unique_ptr<HttpRequest> http_request_;
  std::unique_ptr<HttpRequest> https_request_;
  // PortalDetector::Result for the current on-going attempt. Undefined if there
  // is no portal detection attempt currently running.
  std::optional<Result> result_;
  // PortalDetector::Result of the prior attempt. Undefined for the first
  // attempt. Used to ensure that the same HTTP probe URL is used with a closed
  // captive portal.
  std::optional<Result> previous_result_ = std::nullopt;

  base::WeakPtrFactory<PortalDetector> weak_ptr_factory_{this};
};

std::ostream& operator<<(std::ostream& stream,
                         PortalDetector::ProbeResult result);
std::ostream& operator<<(std::ostream& stream,
                         PortalDetector::ValidationState state);
std::ostream& operator<<(std::ostream& stream,
                         const PortalDetector::Result& result);

}  // namespace shill

#endif  // SHILL_PORTAL_DETECTOR_H_
