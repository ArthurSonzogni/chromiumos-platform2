// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SYSTEM_PROXY_PROXY_CONNECT_JOB_H_
#define SYSTEM_PROXY_PROXY_CONNECT_JOB_H_

#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <curl/curl.h>

#include <base/callback_forward.h>
#include <base/cancelable_callback.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

namespace patchpanel {
class SocketForwarder;
class Socket;
}  // namespace patchpanel

namespace system_proxy {
// ProxyConnectJob asynchronously sets up a connection to a remote target on
// behalf of a client using the following steps:
// 1. Gets the target url from the client request;
// 2. Asks the parent to resolve the proxy for the target url via
//    |resolve_proxy_callback_|;
// 3. Connects to the target url trough the remote proxy server returned by the
//    parent.
// 3. 1. On success, it will return a SocketForwarder to the parent, which
//       forwards data between the Chrome OS client and the remote server.
// 3. 2. On error, it will check the HTTP status code from the server's reply.
// 3. 2. 1. If the status code means credentials are required, it asks the
//          parent for authentication credentials via |auth_required_callback|.
//          - If the parent returns credentials for the proxy server challenge,
//          it attempts to reconnect (step 3);
//          - Otherwise it will forward the status code to the client.
// 3.2.1.2. Other status codes are forwarded to the Chrome OS clients and the
// connection is closed.
// Note: Reconnecting to the server with credentials (step 3. 2. 1.) will create
// a new connection to the remote server, while the connection to the local
// client is still open and in a waiting state during the authentication
// process.
// TODO(acostinas,crbug.com/1098200): Cancel the proxy connect job if the
// request for credentials is not resolved after a certain time.
class ProxyConnectJob {
 public:
  using OnConnectionSetupFinishedCallback = base::OnceCallback<void(
      std::unique_ptr<patchpanel::SocketForwarder>, ProxyConnectJob*)>;

  // Will be invoked by ProxyConnectJob to resolve the proxy for |target_url_|.
  // The passed |callback| is expected to be called with the list of proxy
  // servers, which will always contain at least one entry, the default proxy.
  using ResolveProxyCallback = base::OnceCallback<void(
      const std::string& url,
      base::OnceCallback<void(const std::list<std::string>&)>
          on_proxy_resolution_callback)>;
  // Will be invoked by ProxyConnectJob to request the credentials for requests
  // that fail with code 407. If |bad_cached_credentials| is true, the
  // credentials previously acquired for proxy authentication are incorrect.
  using AuthenticationRequiredCallback = base::RepeatingCallback<void(
      const std::string& proxy_url,
      const std::string& scheme,
      const std::string& realm,
      const std::string& bad_cached_credentials,
      base::RepeatingCallback<void(const std::string& credentials)>
          on_auth_acquired_callback)>;

  ProxyConnectJob(std::unique_ptr<patchpanel::Socket> socket,
                  const std::string& credentials,
                  ResolveProxyCallback resolve_proxy_callback,
                  AuthenticationRequiredCallback auth_required_callback,
                  OnConnectionSetupFinishedCallback setup_finished_callback);
  ProxyConnectJob(const ProxyConnectJob&) = delete;
  ProxyConnectJob& operator=(const ProxyConnectJob&) = delete;
  virtual ~ProxyConnectJob();

  // Marks |client_socket_| as non-blocking and adds a watcher that calls
  // |OnClientReadReady| when the socket is read ready.
  virtual bool Start();
  void OnProxyResolution(const std::list<std::string>& proxy_servers);

  friend std::ostream& operator<<(std::ostream& stream,
                                  const ProxyConnectJob& job);

 private:
  friend class ServerProxyTest;
  friend class ProxyConnectJobTest;
  FRIEND_TEST(ServerProxyTest, HandlePendingJobs);
  FRIEND_TEST(ServerProxyTest, HandleConnectRequest);
  FRIEND_TEST(ProxyConnectJobTest, SuccessfulConnection);
  FRIEND_TEST(ProxyConnectJobTest, SuccessfulConnectionAltEnding);
  FRIEND_TEST(ProxyConnectJobTest, BadHttpRequestWrongMethod);
  FRIEND_TEST(ProxyConnectJobTest, BadHttpRequestNoEmptyLine);
  FRIEND_TEST(ProxyConnectJobTest, ResendWithCredentials);
  FRIEND_TEST(ProxyConnectJobTest, NoCredentials);
  FRIEND_TEST(ProxyConnectJobTest, KerberosAuth);
  FRIEND_TEST(ProxyConnectJobTest, AuthenticationTimeout);

  // Reads data from the socket into |raw_request| until the first empty line,
  // which would mark the end of the HTTP request header.
  // This method does not validate the headers.
  bool TryReadHttpHeader(std::vector<char>* raw_request);

  // Called when the client socket is ready for reading.
  void OnClientReadReady();

  // Called from |OnProxyResolution|, after the proxy for |target_url_| is
  // resolved.
  void DoCurlServerConnection();

  void OnError(const std::string_view& http_error_message);

  void OnClientConnectTimeout();

  // Requests the credentials to authenticate to the remote proxy server. The
  // credentials are bound to the protection space extracted from the challenge
  // sent by the remote server.
  void AuthenticationRequired(const std::vector<char>& http_response_headers);
  void OnAuthCredentialsProvided(const std::string& credentials);
  // Proxy credentials are asynchronously requested from the Browser. The user
  // can ignore the authentication request. This method will forward the
  // authentication failure message to the client and is triggered when the
  // waiting time for the credentials has expired.
  void OnAuthenticationTimeout();
  // Checks if the HTTP CONNECT request has failed because of missing proxy
  // authentication credentials.
  bool AreAuthCredentialsRequired(CURL* easyhandle);

  // Sends the server response to the client. Returns true if the headers and
  // body were sent successfully, false otherwise. In case of failure, calls
  // |OnError|. The response headers and body can be empty if the libcurl
  // connection fails. In this case, this will send the client an error message
  // based on the HTTP status code |http_response_code_|.
  bool SendHttpResponseToClient(const std::vector<char>& http_response_headers,
                                const std::vector<char>& http_response_body);

  std::string target_url_;
  // HTTP proxy response code to the CONNECT request.
  int64_t http_response_code_ = 0;

  // Indicates that the timer for waiting for authentication credentials has
  // started. The timer is started the first time the credentials are requested.
  // Subsequent authentication attempts will not re-start the timer.
  bool authentication_timer_started_ = false;

  std::string credentials_;
  std::list<std::string> proxy_servers_;
  ResolveProxyCallback resolve_proxy_callback_;
  AuthenticationRequiredCallback auth_required_callback_;
  OnConnectionSetupFinishedCallback setup_finished_callback_;
  base::CancelableClosure client_connect_timeout_callback_;
  // Started the first time credentials are requested and cancelled when the
  // proxy server sends any HTTP code other than 407 (proxy authentication
  // required).
  base::CancelableClosure credentials_request_timeout_callback_;

  std::unique_ptr<patchpanel::Socket> client_socket_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> read_watcher_;
  base::WeakPtrFactory<ProxyConnectJob> weak_ptr_factory_{this};
};
}  // namespace system_proxy

#endif  // SYSTEM_PROXY_PROXY_CONNECT_JOB_H_
