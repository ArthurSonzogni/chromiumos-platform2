// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DNS_PROXY_DOH_CURL_CLIENT_H_
#define DNS_PROXY_DOH_CURL_CLIENT_H_

#include <curl/curl.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/time/time.h>

namespace dns_proxy {

// List of HTTP status codes.
constexpr int64_t kHTTPOk = 200;

// DoHCurlClient receives a wire-format DNS query and re-send it using secure
// DNS (DNS-over-HTTPS). The caller of DoHCurlClient will get a wire-format
// response done through CURL. Given multiple DoH servers, DoHCurlClient will
// query each servers concurrently. It will return only the first successful
// response OR the last failing response.
class DoHCurlClient {
 public:
  // Callback to be invoked back to the client upon request completion.
  // |ctx| is an argument passed by the caller of `Resolve(...)` and passed
  // back to the caller as-is through this callback. |ctx| is owned by the
  // caller of `Resolve(...)` and the caller is responsible of its lifecycle.
  // DoHCurlClient does not own |ctx| and must not interact with |ctx|.
  // |http_code| stores the HTTP code of the CURL query.
  // |msg| and |len| respectively stores the response and length of the
  // response of the CURL query.
  using QueryCallback = base::OnceCallback<void(
      void* ctx, int64_t http_code, uint8_t* msg, size_t len)>;

  explicit DoHCurlClient(base::TimeDelta timeout);
  ~DoHCurlClient();

  // Resolve DNS address through DNS-over-HTTPS using DNS query |msg| of size
  // |len|. |callback| will be called with |ctx| as its parameter upon query
  // completion. `SetNameServers(...)` and SetDoHProviders(...)` must be called
  // before calling this function.
  // |msg| and |ctx| is owned by the caller of this function. The caller is
  // responsible for their lifecycle.
  bool Resolve(const char* msg, int len, QueryCallback callback, void* ctx);

  // Set standard DNS and DoH servers for running `Resolve(...)`.
  void SetNameServers(const std::vector<std::string>& name_servers);
  void SetDoHProviders(const std::vector<std::string>& doh_providers);

 private:
  // State of a request.
  struct State {
    State(CURL* curl, QueryCallback callback, void* ctx);
    ~State();

    // Fetch the necessary response and run |callback|.
    void RunCallback();

    // Stores the CURL handle for the request.
    CURL* curl;

    // Stores the response and length of a request. |response| needs to be
    // allocated before use and freed after use.
    std::vector<uint8_t> response;

    // |callback| given from the client will be called with |ctx| as its
    // parameter. |ctx| is owned by the caller of `Resolve(...)` and will
    // be returned to the caller as-is through the parameter of |callback|.
    // |ctx| is owned by the caller of `Resolve(...)` and must not be changed
    // here.
    QueryCallback callback;
    void* ctx;

    // |header_list| is owned by this struct. It is stored here in order to
    // free it when the request is done.
    curl_slist* header_list;
  };

  // Callback informed about what to wait for. When called, register or remove
  // the socket given from watchers.
  // This method signature matches CURL `socket_callback(...)`.
  // This callback is registered through CURL.
  // |userp| is owned by DoHCurlClient and should be cleaned properly upon
  // query completion.
  static int SocketCallback(CURL* easy,
                            curl_socket_t socket_fd,
                            int what,
                            void* userp,
                            void* socketp);

  // Callback necessary for CURL to write data, method signature matches CURL
  // `write_callback(...)`. This callback is registered through CURL.
  //
  // This callback function gets called by libcurl as soon as there is data
  // received that needs to be saved. For most transfers, this callback gets
  // called many times and each invoke delivers another chunk of data.
  // |ptr| points to the delivered data, and the size of that data is |nmemb|.
  // |size| is always 1. |userdata| refers to the data given to CURL through
  // CURLOPT_WRITEDATA option.
  //
  // |ptr| is owned by CURL and must not be altered in this function.
  // |userdata| is owned by DoHCurlClient and should be cleaned properly upon
  // query completion.
  static size_t WriteCallback(char* ptr,
                              size_t size,
                              size_t nmemb,
                              void* userdata);

  // Callback informed to start the request and to handle timeout, method
  // signature matches CURL `timer_callback(...)`. This callback is registered
  // through CURL.
  //
  // |timeout_ms| value of -1 passed to this callback means the caller should
  // delete the timer. All other values are valid expire times in number of
  // milliseconds. |userp| refers to the pointer given to CURL through
  // CURLMOPT_TIMERDATA option.
  //
  // |userp| is owned by DoHCurlClient and should be cleaned properly upon
  // query completion.
  static int TimerCallback(CURLM* multi, int64_t timeout_ms, void* userp);

  // Methods to update CURL socket watchers for asynchronous CURL events.
  // When an action is observed, `CheckMultiInfo()` will be called.
  void AddReadWatcher(curl_socket_t socket_fd);
  void AddWriteWatcher(curl_socket_t socket_fd);
  void RemoveWatcher(curl_socket_t socket_fd);

  // Callback called whenever an event is ready to be handled by CURL on
  // |socket_fd|. This callback is registered through the core event loop.
  void OnFileCanReadWithoutBlocking(curl_socket_t socket_fd);
  void OnFileCanWriteWithoutBlocking(curl_socket_t socket_fd);

  // Checks for request completion and handles its result.
  // These functions are called when CURL just finished processing an event.
  // |curl_msg| is owned by CURL, DoHCurlClient should not care about its
  // lifecycle.
  void CheckMultiInfo();
  void HandleResult(CURLMsg* curl_msg);

  // Timeout for a CURL query in seconds.
  int64_t timeout_seconds_;

  // Watchers for available event to be handled by CURL.
  std::map<curl_socket_t,
           std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      read_watchers_;
  std::map<curl_socket_t,
           std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      write_watchers_;

  // |name_servers_| to resolve |doh_providers_| address.
  std::string name_servers_;

  // |doh_providers_| to resolve domain name using DoH.
  std::vector<std::string> doh_providers_;

  // Current requests' states keyed by it's CURL handle.
  std::map<CURL*, std::unique_ptr<State>> states_;

  // CURL multi handle to do asynchronous requests.
  CURLM* curlm_;

  base::WeakPtrFactory<DoHCurlClient> weak_factory_{this};
};
}  // namespace dns_proxy

#endif  // DNS_PROXY_DOH_CURL_CLIENT_H_
