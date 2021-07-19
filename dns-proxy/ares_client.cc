// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/ares_client.h"

#include <algorithm>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>

namespace dns_proxy {

AresClient::State::State(AresClient* client,
                         ares_channel channel,
                         const QueryCallback& callback,
                         void* ctx)
    : client(client), channel(channel), callback(callback), ctx(ctx) {}

AresClient::AresClient(base::TimeDelta timeout,
                       int max_num_retries,
                       int max_concurrent_queries)
    : timeout_(timeout),
      max_num_retries_(max_num_retries),
      max_concurrent_queries_(max_concurrent_queries) {
  if (ares_library_init(ARES_LIB_INIT_ALL) != ARES_SUCCESS) {
    LOG(DFATAL) << "Failed to initialize ares library";
  }
}

AresClient::~AresClient() {
  // Whenever ares_destroy is called, AresCallback will be called with status
  // equal to ARES_EDESTRUCTION. This callback ensures that the states of the
  // queries are cleared properly.
  for (const auto& channel : channels_inflight_) {
    ares_destroy(channel);
  }
  ares_library_cleanup();
}

void AresClient::OnFileCanReadWithoutBlocking(ares_channel channel,
                                              ares_socket_t socket_fd) {
  ares_process_fd(channel, socket_fd, ARES_SOCKET_BAD);
  UpdateWatchers(channel);
}

void AresClient::OnFileCanWriteWithoutBlocking(ares_channel channel,
                                               ares_socket_t socket_fd) {
  ares_process_fd(channel, ARES_SOCKET_BAD, socket_fd);
  UpdateWatchers(channel);
}

void AresClient::UpdateWatchers(ares_channel channel) {
  ares_socket_t sockets[ARES_GETSOCK_MAXNUM];
  int action_bits = ares_getsock(channel, sockets, ARES_GETSOCK_MAXNUM);

  auto read_watchers = read_watchers_.find(channel);
  auto write_watchers = write_watchers_.find(channel);
  if (read_watchers == read_watchers_.end() ||
      write_watchers == write_watchers_.end()) {
    return;
  }

  // Clear the watchers and rebuild it. This is necessary because ares does not
  // provide a utility to notify unused sockets.
  read_watchers->second.clear();
  write_watchers->second.clear();
  for (int i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
    if (ARES_GETSOCK_READABLE(action_bits, i)) {
      read_watchers->second.emplace_back(
          base::FileDescriptorWatcher::WatchReadable(
              sockets[i],
              base::BindRepeating(&AresClient::OnFileCanReadWithoutBlocking,
                                  weak_factory_.GetWeakPtr(), channel,
                                  sockets[i])));
    }
    if (ARES_GETSOCK_WRITABLE(action_bits, i)) {
      write_watchers->second.emplace_back(
          base::FileDescriptorWatcher::WatchReadable(
              sockets[i],
              base::BindRepeating(&AresClient::OnFileCanWriteWithoutBlocking,
                                  weak_factory_.GetWeakPtr(), channel,
                                  sockets[i])));
    }
  }
}

void AresClient::SetNameServers(const std::vector<std::string>& name_servers) {
  name_servers_ = base::JoinString(name_servers, ",");
  num_name_servers_ = name_servers.size();
}

void AresClient::AresCallback(
    void* ctx, int status, int timeouts, uint8_t* msg, int len) {
  State* state = static_cast<State*>(ctx);
  // The query is cancelled in-flight. Cleanup the state.
  if (status == ARES_ECANCELLED || status == ARES_EDESTRUCTION) {
    delete state;
    return;
  }

  auto buf = std::make_unique<uint8_t[]>(len);
  memcpy(buf.get(), msg, len);
  // Handle the result outside this function to avoid undefined behaviors.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&AresClient::HandleResult,
                                state->client->weak_factory_.GetWeakPtr(),
                                state, status, std::move(buf), len));
}

void AresClient::HandleResult(State* state,
                              int status,
                              std::unique_ptr<uint8_t[]> msg,
                              int len) {
  // Set state as unique pointer to force cleanup, the state must be destroyed
  // in this function.
  std::unique_ptr<State> scoped_state(state);

  // `HandleResult(...)` may be called even after ares channel is destroyed
  // This happens if a query is completed while queries are being cancelled.
  // On such case, do nothing, the state will be deleted through unique pointer.
  if (!base::Contains(channels_inflight_, state->channel)) {
    return;
  }

  // Ares will return 0 if no queries are active on the channel.
  // |read_fds| and |write_fds| are unused.
  fd_set read_fds, write_fds;
  int nfds = ares_fds(state->channel, &read_fds, &write_fds);

  // Run the callback if the current request is the first successful request
  // or the current request is the last request.
  if (status != ARES_SUCCESS && nfds > 0) {
    return;
  }
  state->callback.Run(state->ctx, status, msg.get(), len);
  msg.reset();

  // Cancel other queries and destroy the channel. Whenever ares_destroy is
  // called, AresCallback will be called with status equal to ARES_EDESTRUCTION.
  // This callback ensures that the states of the in-flight queries ares cleared
  // properly.
  channels_inflight_.erase(state->channel);
  read_watchers_.erase(state->channel);
  write_watchers_.erase(state->channel);
  ares_destroy(state->channel);
}

void AresClient::ResetTimeout(ares_channel channel) {
  // Check for timeout if the channel is still available.
  if (!base::Contains(channels_inflight_, channel)) {
    return;
  }
  ares_process_fd(channel, ARES_SOCKET_BAD, ARES_SOCKET_BAD);

  struct timeval max_tv, ret_tv;
  struct timeval* tv;
  max_tv.tv_sec = timeout_.InMilliseconds() / 1000;
  max_tv.tv_usec = (timeout_.InMilliseconds() % 1000) * 1000;
  if ((tv = ares_timeout(channel, &max_tv, &ret_tv)) == NULL) {
    LOG(ERROR) << "Failed to get timeout";
    return;
  }
  int timeout_ms = tv->tv_sec * 1000 + tv->tv_usec / 1000;
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindRepeating(&AresClient::ResetTimeout, weak_factory_.GetWeakPtr(),
                          channel),
      base::TimeDelta::FromMilliseconds(timeout_ms));
}

ares_channel AresClient::InitChannel() {
  struct ares_options options;
  memset(&options, 0, sizeof(options));
  int optmask = 0;

  // Set option timeout.
  optmask |= ARES_OPT_TIMEOUTMS;
  options.timeout = timeout_.InMilliseconds();

  // Set maximum number of retries.
  optmask |= ARES_OPT_TRIES;
  options.tries = max_num_retries_;

  // Perform round-robin selection of name servers. This enables Resolve(...)
  // to resolve using multiple servers concurrently.
  optmask |= ARES_OPT_ROTATE;

  ares_channel channel;
  if (ares_init_options(&channel, &options, optmask) != ARES_SUCCESS) {
    LOG(ERROR) << "Failed to initialize ares_channel";
    ares_destroy(channel);
    return nullptr;
  }

  if (ares_set_servers_csv(channel, name_servers_.c_str()) != ARES_SUCCESS) {
    LOG(ERROR) << "Failed to set ares name servers";
    ares_destroy(channel);
    return nullptr;
  }

  // Start timeout handler.
  channels_inflight_.emplace(channel);
  ResetTimeout(channel);
  return channel;
}

bool AresClient::Resolve(const unsigned char* msg,
                         size_t len,
                         const QueryCallback& callback,
                         void* ctx) {
  if (name_servers_.empty()) {
    LOG(ERROR) << "Name servers must not be empty";
    return false;
  }
  ares_channel channel = InitChannel();
  if (!channel) {
    return false;
  }
  // Query multiple name servers concurrently. Selection of name servers is
  // done implicitly through round robin selection. This is enabled by ares
  // option ARES_OPT_ROTATE.
  for (int i = 0; i < std::min(num_name_servers_, max_concurrent_queries_);
       i++) {
    State* state = new State(this, channel, callback, ctx);
    ares_send(channel, msg, len, &AresClient::AresCallback, state);
  }

  // Set up file descriptor watchers.
  read_watchers_.emplace(
      channel,
      std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>());
  write_watchers_.emplace(
      channel,
      std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>());
  UpdateWatchers(channel);
  return true;
}
}  // namespace dns_proxy
