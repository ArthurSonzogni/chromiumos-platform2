// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/doh_curl_client.h"

#include <utility>

#include <base/bind.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>

namespace dns_proxy {
namespace {
constexpr char kLinuxUserAgent[] =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (kHTML, like Gecko) "
    "Chrome/7.0.38.09.132 Safari/537.36";
constexpr std::array<const char*, 2> kDoHHeaderList{
    {"Accept: application/dns-message",
     "Content-Type: application/dns-message"}};
}  // namespace

DoHCurlClient::CurlResult::CurlResult(CURLcode curl_code,
                                      int64_t http_code,
                                      int64_t retry_delay_ms)
    : curl_code(curl_code),
      http_code(http_code),
      retry_delay_ms(retry_delay_ms) {}

DoHCurlClient::State::State(CURL* curl, QueryCallback callback, void* ctx)
    : curl(curl),
      callback(std::move(callback)),
      ctx(ctx),
      header_list(nullptr) {}

DoHCurlClient::State::~State() {
  curl_easy_cleanup(curl);
  curl_slist_free_all(header_list);
}

void DoHCurlClient::State::RunCallback(CURLMsg* curl_msg) {
  int64_t http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  // TODO(jasongustaman): Use HTTP 429, Retry-After header value.
  CurlResult res(curl_msg->data.result, http_code, 0 /* retry_delay_ms */);
  std::move(callback).Run(ctx, res, response.data(), response.size());
}

void DoHCurlClient::State::SetResponse(char* msg, size_t len) {
  if (len <= 0) {
    LOG(ERROR) << "Unexpected length: " << len;
    return;
  }
  response.insert(response.end(), msg, msg + len);
}

DoHCurlClient::DoHCurlClient(base::TimeDelta timeout)
    : timeout_seconds_(timeout.InSeconds()) {
  // Initialize CURL.
  curl_global_init(CURL_GLOBAL_DEFAULT);
  curlm_ = curl_multi_init();

  // Set socket callback to `SocketCallback(...)`. This function will be called
  // whenever a CURL socket state is changed. DoHCurlClient class |this| will
  // passed as a parameter of the callback.
  curl_multi_setopt(curlm_, CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(curlm_, CURLMOPT_SOCKETFUNCTION,
                    &DoHCurlClient::SocketCallback);

  // Set timer callback to `TimerCallback(...)`. This function will be called
  // whenever a timeout change happened. DoHCurlClient class |this| will be
  // passed as a parameter of the callback.
  curl_multi_setopt(curlm_, CURLMOPT_TIMERDATA, this);
  curl_multi_setopt(curlm_, CURLMOPT_TIMERFUNCTION,
                    &DoHCurlClient::TimerCallback);
}

DoHCurlClient::~DoHCurlClient() {
  curl_global_cleanup();
}

void DoHCurlClient::HandleResult(CURLMsg* curl_msg) {
  if (!base::Contains(states_, curl_msg->easy_handle)) {
    LOG(ERROR) << "Curl handle not found";
    return;
  }

  CURL* curl = curl_msg->easy_handle;
  curl_multi_remove_handle(curlm_, curl);

  State* state = states_[curl].get();
  state->RunCallback(curl_msg);

  // TODO(jasongustaman): Get and save curl metrics.
  states_.erase(curl);
}

void DoHCurlClient::CheckMultiInfo() {
  CURLMsg* curl_msg = nullptr;
  int msgs_left = 0;
  while ((curl_msg = curl_multi_info_read(curlm_, &msgs_left))) {
    if (curl_msg->msg != CURLMSG_DONE) {
      continue;
    }
    HandleResult(curl_msg);
  }
}

void DoHCurlClient::OnFileCanReadWithoutBlocking(curl_socket_t socket_fd) {
  int still_running;
  CURLMcode rc = curl_multi_socket_action(curlm_, socket_fd, CURL_CSELECT_IN,
                                          &still_running);
  if (rc != CURLM_OK) {
    LOG(INFO) << "Failed to read from socket: " << curl_multi_strerror(rc);
    return;
  }
  CheckMultiInfo();
}

void DoHCurlClient::OnFileCanWriteWithoutBlocking(curl_socket_t socket_fd) {
  int still_running;
  CURLMcode rc = curl_multi_socket_action(curlm_, socket_fd, CURL_CSELECT_OUT,
                                          &still_running);
  if (rc != CURLM_OK) {
    LOG(INFO) << "Failed to write to socket: " << curl_multi_strerror(rc);
    return;
  }
  CheckMultiInfo();
}

void DoHCurlClient::AddReadWatcher(curl_socket_t socket_fd) {
  if (!base::Contains(read_watchers_, socket_fd)) {
    read_watchers_.emplace(
        socket_fd,
        base::FileDescriptorWatcher::WatchReadable(
            socket_fd,
            base::BindRepeating(&DoHCurlClient::OnFileCanReadWithoutBlocking,
                                weak_factory_.GetWeakPtr(), socket_fd)));
  }
}

void DoHCurlClient::AddWriteWatcher(curl_socket_t socket_fd) {
  if (!base::Contains(write_watchers_, socket_fd)) {
    write_watchers_.emplace(
        socket_fd,
        base::FileDescriptorWatcher::WatchReadable(
            socket_fd,
            base::BindRepeating(&DoHCurlClient::OnFileCanWriteWithoutBlocking,
                                weak_factory_.GetWeakPtr(), socket_fd)));
  }
}

void DoHCurlClient::RemoveWatcher(curl_socket_t socket_fd) {
  read_watchers_.erase(socket_fd);
  write_watchers_.erase(socket_fd);
}

int DoHCurlClient::SocketCallback(
    CURL* easy, curl_socket_t socket_fd, int what, void* userp, void* socketp) {
  DoHCurlClient* client = static_cast<DoHCurlClient*>(userp);
  switch (what) {
    case CURL_POLL_IN:
      client->AddReadWatcher(socket_fd);
      return 0;
    case CURL_POLL_OUT:
      client->AddWriteWatcher(socket_fd);
      return 0;
    case CURL_POLL_INOUT:
      client->AddReadWatcher(socket_fd);
      client->AddWriteWatcher(socket_fd);
      return 0;
    case CURL_POLL_REMOVE:
      client->RemoveWatcher(socket_fd);
      return 0;
    default:
      return 0;
  }
}

void DoHCurlClient::TimeoutCallback() {
  int still_running;
  curl_multi_socket_action(curlm_, CURL_SOCKET_TIMEOUT, 0, &still_running);
  CheckMultiInfo();
}

int DoHCurlClient::TimerCallback(CURLM* multi,
                                 int64_t timeout_ms,
                                 void* userp) {
  DoHCurlClient* client = static_cast<DoHCurlClient*>(userp);
  if (timeout_ms > 0) {
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::BindRepeating(&DoHCurlClient::TimeoutCallback,
                            base::Unretained(client)),
        base::TimeDelta::FromMilliseconds(timeout_ms));
  } else if (timeout_ms == 0) {
    client->TimeoutCallback();
  }
  return 0;
}

size_t DoHCurlClient::WriteCallback(char* ptr,
                                    size_t size,
                                    size_t nmemb,
                                    void* userdata) {
  State* state = static_cast<State*>(userdata);
  size_t len = size * nmemb;
  state->SetResponse(ptr, len);
  return len;
}

size_t DoHCurlClient::HeaderCallback(void* data,
                                     size_t size,
                                     size_t nitems,
                                     void* userp) {
  State* state = static_cast<State*>(userp);
  size_t len = size * nitems;
  std::string header(static_cast<char*>(data), len);
  state->header.emplace_back(header);
  return len;
}

void DoHCurlClient::SetNameServers(
    const std::vector<std::string>& name_servers) {
  name_servers_ = base::JoinString(name_servers, ",");
}

void DoHCurlClient::SetDoHProviders(
    const std::vector<std::string>& doh_providers) {
  doh_providers_ = doh_providers;
}

bool DoHCurlClient::Resolve(const char* msg,
                            int len,
                            QueryCallback callback,
                            void* ctx) {
  if (name_servers_.empty() || doh_providers_.empty()) {
    LOG(DFATAL) << "DNS and DoH server must not be empty";
    return false;
  }

  CURL* curl;
  curl = curl_easy_init();
  if (!curl) {
    LOG(ERROR) << "Failed to initialize curl";
    return false;
  }

  std::unique_ptr<State> state =
      std::make_unique<State>(curl, std::move(callback), ctx);
  states_.emplace(curl, std::move(state));

  // TODO(jasongustaman): Query all servers concurrently.

  // Set the target URL which is the DoH provider to query to.
  curl_easy_setopt(curl, CURLOPT_URL, doh_providers_.front().c_str());

  // Set the DNS name servers to resolve the URL(s) / DoH provider(s).
  // This uses ares and will be done asynchronously.
  curl_easy_setopt(curl, CURLOPT_DNS_SERVERS, name_servers_.c_str());

  // Set the HTTP header to the needed DoH header. The stored value needs to
  // be released when query is finished.
  for (int i = 0; i < kDoHHeaderList.size(); i++) {
    state.get()->header_list =
        curl_slist_append(state.get()->header_list, kDoHHeaderList[i]);
  }
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, state.get()->header_list);

  // Stores the data to be sent through HTTP POST and its length.
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, msg);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);

  // Set the user agent for the query.
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kLinuxUserAgent);

  // Ignore signals SIGPIPE to be sent when the other end of CURL socket is
  // closed.
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 0);

  // Set timeout of the query.
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds_);

  // Set the callback to be called whenever CURL got a response. The data
  // needs to be copied to the write data.
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &DoHCurlClient::WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, state.get());

  // Handle redirection automatically.
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);

  // Runs the query asynchronously.
  curl_multi_add_handle(curlm_, curl);
  return true;
}
}  // namespace dns_proxy
