// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBCHROMEOS_CHROMEOS_HTTP_HTTP_TRANSPORT_CURL_H_
#define LIBCHROMEOS_CHROMEOS_HTTP_HTTP_TRANSPORT_CURL_H_

#include <map>
#include <string>
#include <utility>

#include <base/memory/weak_ptr.h>
#include <chromeos/chromeos_export.h>
#include <chromeos/http/curl_api.h>
#include <chromeos/http/http_transport.h>

namespace chromeos {
namespace http {
namespace curl {

class Connection;

///////////////////////////////////////////////////////////////////////////////
// An implementation of http::Transport that uses libcurl for
// HTTP communications. This class (as http::Transport base)
// is used by http::Request and http::Response classes to provide HTTP
// functionality to the clients.
// See http_transport.h for more details.
///////////////////////////////////////////////////////////////////////////////
class CHROMEOS_EXPORT Transport : public http::Transport {
 public:
  // Constructs the transport using the current message loop for async
  // operations.
  explicit Transport(const std::shared_ptr<CurlInterface>& curl_interface);
  // Creates a transport object using a proxy.
  // |proxy| is of the form [protocol://][user:password@]host[:port].
  // If not defined, protocol is assumed to be http://.
  Transport(const std::shared_ptr<CurlInterface>& curl_interface,
            const std::string& proxy);
  ~Transport() override;

  // Overrides from http::Transport.
  std::shared_ptr<http::Connection> CreateConnection(
      const std::string& url,
      const std::string& method,
      const HeaderList& headers,
      const std::string& user_agent,
      const std::string& referer,
      chromeos::ErrorPtr* error) override;

  void RunCallbackAsync(const tracked_objects::Location& from_here,
                        const base::Closure& callback) override;

  RequestID StartAsyncTransfer(http::Connection* connection,
                               const SuccessCallback& success_callback,
                               const ErrorCallback& error_callback) override;

  bool CancelRequest(RequestID request_id) override;

  void SetDefaultTimeout(base::TimeDelta timeout) override;

  // Helper methods to convert CURL error codes (CURLcode and CURLMcode)
  // into chromeos::Error object.
  static void AddEasyCurlError(chromeos::ErrorPtr* error,
                               const tracked_objects::Location& location,
                               CURLcode code,
                               CurlInterface* curl_interface);

  static void AddMultiCurlError(chromeos::ErrorPtr* error,
                                const tracked_objects::Location& location,
                                CURLMcode code,
                                CurlInterface* curl_interface);

 private:
  // Forward-declaration of internal implementation structures.
  struct AsyncRequestData;
  class SocketPollData;

  // Initializes CURL for async operation.
  bool SetupAsyncCurl(chromeos::ErrorPtr* error);

  // Stops CURL's async operations.
  void ShutDownAsyncCurl();

  // Handles all pending async messages from CURL.
  void ProcessAsyncCurlMessages();

  // Processes the transfer completion message (success or failure).
  void OnTransferComplete(http::curl::Connection* connection,
                          CURLcode code);

  // Cleans up internal data for a completed/canceled asynchronous operation
  // on a connection.
  void CleanAsyncConnection(http::curl::Connection* connection);

  // Called periodically to provide CURL a chance to perform asynchronous
  // operations in the background. |timer_id| is the timer ID which was
  // used to start the timer in ScheduleTimer() method.
  void OnTimer(int timer_id);

  // Start a timer with the delay specified in |timer_delay_| and the given
  // |timer_id|. The timer ID is used to identify multiple pending delayed
  // tasks. If the ID matches the latest |current_timer_id_| when OnTimer()
  // is called, the timer task will be re-scheduled automatically. If not,
  // this means that a newer timer even has been scheduled (probably with
  // a different time-out delay) and the old pending task should not be
  // rescheduled.
  void ScheduleTimer(int timer_id);

  // Callback for CURL to handle curl_socket_callback() notifications.
  // The parameters correspond to those of curl_socket_callback().
  static int MultiSocketCallback(CURL* easy,
                                 curl_socket_t s,
                                 int what,
                                 void* userp,
                                 void* socketp);

  // Callback for CURL to handle curl_multi_timer_callback() notifications.
  // The parameters correspond to those of curl_multi_timer_callback().
  // CURL actually uses "long" types in callback signatures, so we must comply.
  static int MultiTimerCallback(CURLM* multi,
                                long timeout_ms,  // NOLINT(runtime/int)
                                void* userp);

  std::shared_ptr<CurlInterface> curl_interface_;
  std::string proxy_;
  // CURL "multi"-handle for processing requests on multiple connections.
  CURLM* curl_multi_handle_{nullptr};
  // A map to find a corresponding Connection* using a request ID.
  std::map<RequestID, Connection*> request_id_map_;
  // Stores the connection-specific asynchronous data (such as the success
  // and error callbacks that need to be called at the end of the async
  // operation).
  std::map<Connection*, std::unique_ptr<AsyncRequestData>> async_requests_;
  // Internal data associated with in-progress asynchronous operations.
  std::map<std::pair<CURL*, curl_socket_t>, SocketPollData*> poll_data_map_;
  // The current ID used to schedule a periodic poll of data on CURL multi-
  // handle. When CURL calls MultiTimerCallback with a new timeout, we post
  // a new timer task with a different ID and let the current one just
  // trigger as needed and not rescheduled.
  int current_timer_id_{0};
  // The timeout delay that CURL asked us to call back in to check on the
  // progress on asynchronous operations.
  base::TimeDelta timer_delay_;
  // The last request ID used for asynchronous operations.
  RequestID last_request_id_{0};
  // The connection timeout for the requests made.
  base::TimeDelta connection_timeout_;

  base::WeakPtrFactory<Transport> weak_ptr_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(Transport);
};

}  // namespace curl
}  // namespace http
}  // namespace chromeos

#endif  // LIBCHROMEOS_CHROMEOS_HTTP_HTTP_TRANSPORT_CURL_H_
