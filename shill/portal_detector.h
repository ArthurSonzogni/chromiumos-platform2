// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_PORTAL_DETECTOR_H_
#define SHILL_PORTAL_DETECTOR_H_

#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/cancelable_callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <brillo/http/http_request.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/http_request.h"
#include "shill/http_url.h"
#include "shill/net/shill_time.h"
#include "shill/net/sockets.h"
#include "shill/refptr_types.h"
#include "shill/service.h"

namespace shill {

class EventDispatcher;
class Metrics;
class Time;

// The PortalDetector class implements the portal detection
// facility in shill, which is responsible for checking to see
// if a connection has "general internet connectivity".
//
// This information can be used for ranking one connection
// against another, or for informing UI and other components
// outside the connection manager whether the connection seems
// available for "general use" or if further user action may be
// necessary (e.g, click through of a WiFi Hotspot's splash
// page).
//
// This is achieved by using one or more trial attempts
// to access a URL and expecting a specific response.  Any result
// that deviates from this result (DNS or HTTP errors, as well as
// deviations from the expected content) are considered failures.
class PortalDetector {
 public:
  // The Phase enum indicates the phase at which the probe fails.
  enum class Phase {
    kUnknown,
    kConnection,  // Failure to establish connection with server
    kDNS,         // Failure to resolve hostname or DNS server failure
    kHTTP,        // Failure to read or write to server
    kContent      // Content mismatch in response
  };

  enum class Status { kFailure, kSuccess, kTimeout, kRedirect };

  struct Properties {
    Properties()
        : http_url_string(kDefaultHttpUrl),
          https_url_string(kDefaultHttpsUrl),
          fallback_http_url_strings(kDefaultFallbackHttpUrls) {}
    Properties(const std::string& http_url_string,
               const std::string& https_url_string,
               const std::vector<std::string>& fallback_http_url_strings)
        : http_url_string(http_url_string),
          https_url_string(https_url_string),
          fallback_http_url_strings(fallback_http_url_strings) {}
    bool operator==(const Properties& b) const {
      return http_url_string == b.http_url_string &&
             https_url_string == b.https_url_string &&
             std::set<std::string>(fallback_http_url_strings.begin(),
                                   fallback_http_url_strings.end()) ==
                 std::set<std::string>(b.fallback_http_url_strings.begin(),
                                       b.fallback_http_url_strings.end());
    }

    std::string http_url_string;
    std::string https_url_string;
    std::vector<std::string> fallback_http_url_strings;
  };

  // Represents the result of a complete portal detection attempt (DNS
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

    // Returns true if both http and https probes have completed, successfully
    // or not.
    bool IsComplete() const;

    // Returns the Service ConnectionState value inferred from this captive
    // portal detection result.
    Service::ConnectState GetConnectionState() const;
  };

  static const char kDefaultHttpUrl[];
  static const char kDefaultHttpsUrl[];
  static const std::vector<std::string> kDefaultFallbackHttpUrls;
  static const int kInitialCheckIntervalSeconds;
  static const int kMaxPortalCheckIntervalSeconds;
  static const char kDefaultCheckPortalList[];

  PortalDetector(ConnectionRefPtr connection,
                 EventDispatcher* dispatcher,
                 Metrics* metrics,
                 base::Callback<void(const Result&)> callback);
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

  // Static method mapping from HttpRequest responses to PortalDetection
  // Phases. For example, if the HttpRequest result is kResultDNSFailure,
  // this method returns Phase::kDNS.
  static Phase GetPortalPhaseForRequestResult(HttpRequest::Result result);

  // Static method mapping from HttpRequest responses to PortalDetection
  // Status. For example, if the HttpRequest result is kResultDNSFailure,
  // this method returns Status::kFailure.
  static Status GetPortalStatusForRequestResult(HttpRequest::Result result);

  // Start a portal detection test.  Returns true if |props.http_url_string| and
  // |props.https_url_string| correctly parse as URLs.  Returns false (and does
  // not start) if they fail to parse.
  //
  // As each attempt completes the callback handed to the constructor will
  // be called.
  // TODO(b/184036481): Change |delay_seconds| type to base::TimeDelay.
  virtual bool StartAfterDelay(const Properties& props, int delay_seconds);

  // End the current portal detection process if one exists, and do not call
  // the callback.
  virtual void Stop();

  // Returns whether portal request is "in progress".
  virtual bool IsInProgress();

  // Method used to adjust the start delay in the event of a retry.
  // Calculates the elapsed time between the most recent attempt and the point
  // the retry is scheduled.  Adjusts the delay to the difference between
  // |delay| and the elapsed time so that the retry starts |delay| seconds after
  // the previous attempt.
  virtual int AdjustStartDelay(int init_delay_seconds);

 private:
  friend class PortalDetectorTest;
  FRIEND_TEST(PortalDetectorTest, StartAttemptFailed);
  FRIEND_TEST(PortalDetectorTest, AdjustStartDelayImmediate);
  FRIEND_TEST(PortalDetectorTest, AdjustStartDelayAfterDelay);
  FRIEND_TEST(PortalDetectorTest, AttemptCount);
  FRIEND_TEST(PortalDetectorTest, RequestSuccess);
  FRIEND_TEST(PortalDetectorTest, RequestHTTPFailureHTTPSSuccess);
  FRIEND_TEST(PortalDetectorTest, RequestFail);
  FRIEND_TEST(PortalDetectorTest, InvalidURL);
  FRIEND_TEST(PortalDetectorTest, IsActive);

  // Picks the HTTP probe URL based on |attempt_count_|. Returns kDefaultHttpUrl
  // if this is the first attempt. Otherwise, randomly returns an element of
  // kDefaultFallbackHttpUrls.
  const std::string PickHttpProbeUrl(const Properties& props);

  // Internal method to update the start time of the next event.  This is used
  // to keep attempts spaced by the right duration in the event of a retry.
  void UpdateAttemptTime(int delay_seconds);

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

  // Method to return if the connection is being actively tested.
  virtual bool IsActive();

  // Return |logging_tag_| appended with the |attempt_count_|.
  std::string LoggingTag() const;

  std::string logging_tag_;
  int attempt_count_;
  struct timeval attempt_start_time_;
  ConnectionRefPtr connection_;
  EventDispatcher* dispatcher_;
  Metrics* metrics_;
  base::WeakPtrFactory<PortalDetector> weak_ptr_factory_;
  base::Callback<void(const Result&)> portal_result_callback_;
  Time* time_;
  std::unique_ptr<HttpRequest> http_request_;
  std::unique_ptr<HttpRequest> https_request_;
  std::unique_ptr<Result> result_;

  std::string http_url_string_;
  std::string https_url_string_;
  base::CancelableClosure trial_;
  bool is_active_;
};

std::ostream& operator<<(std::ostream& stream, PortalDetector::Phase phase);
std::ostream& operator<<(std::ostream& stream, PortalDetector::Status status);

}  // namespace shill

#endif  // SHILL_PORTAL_DETECTOR_H_
