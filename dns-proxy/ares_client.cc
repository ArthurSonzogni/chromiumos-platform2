// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/ares_client.h"

#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/threading/thread_task_runner_handle.h>

namespace dns_proxy {

AresClient::State::State(QueryCallback callback, void* ctx)
    : callback(std::move(callback)), ctx(ctx) {}

AresClient::AresClient(base::TimeDelta timeout) {
  if (ares_library_init(ARES_LIB_INIT_ALL) != ARES_SUCCESS) {
    LOG(DFATAL) << "Failed to initialize ares library";
    return;
  }

  struct ares_options options;
  memset(&options, 0, sizeof(options));
  int optmask = 0;

  // Set option timeout.
  optmask |= ARES_OPT_TIMEOUTMS;
  options.timeout = timeout.InMilliseconds();

  if (ares_init_options(&channel_, &options, optmask) != ARES_SUCCESS) {
    LOG(DFATAL) << "Failed to initialize ares channel";
    ares_destroy(channel_);
    return;
  }
}

AresClient::~AresClient() {
  ares_destroy(channel_);
  ares_library_cleanup();
}

void AresClient::OnFileCanReadWithoutBlocking(ares_socket_t socket_fd) {
  ares_process_fd(channel_, socket_fd, ARES_SOCKET_BAD);
  UpdateWatchers();
}

void AresClient::OnFileCanWriteWithoutBlocking(ares_socket_t socket_fd) {
  ares_process_fd(channel_, ARES_SOCKET_BAD, socket_fd);
  UpdateWatchers();
}

void AresClient::UpdateWatchers() {
  ares_socket_t sockets[ARES_GETSOCK_MAXNUM];
  int action_bits = ares_getsock(channel_, sockets, ARES_GETSOCK_MAXNUM);

  read_watchers_.clear();
  write_watchers_.clear();

  for (int i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
    if (ARES_GETSOCK_READABLE(action_bits, i)) {
      read_watchers_.emplace_back(base::FileDescriptorWatcher::WatchReadable(
          sockets[i],
          base::BindRepeating(&AresClient::OnFileCanReadWithoutBlocking,
                              weak_factory_.GetWeakPtr(), sockets[i])));
    }
    if (ARES_GETSOCK_WRITABLE(action_bits, i)) {
      write_watchers_.emplace_back(base::FileDescriptorWatcher::WatchReadable(
          sockets[i],
          base::BindRepeating(&AresClient::OnFileCanWriteWithoutBlocking,
                              weak_factory_.GetWeakPtr(), sockets[i])));
    }
  }
}

void AresClient::SetNameServers(const std::vector<std::string>& name_servers) {
  name_servers_ = name_servers;
  std::string server_addresses = base::JoinString(name_servers_, ",");
  int status = ares_set_servers_csv(channel_, server_addresses.c_str());
  if (status != ARES_SUCCESS) {
    LOG(ERROR) << "Failed to set name servers";
    return;
  }
}

void AresClient::AresCallback(
    void* ctx, int status, int timeouts, uint8_t* msg, int len) {
  State* state = static_cast<State*>(ctx);
  std::move(state->callback).Run(state->ctx, status, msg, len);
  delete state;
}

bool AresClient::Resolve(const unsigned char* msg,
                         size_t len,
                         QueryCallback callback,
                         void* ctx) {
  if (name_servers_.empty()) {
    LOG(ERROR) << "Name servers must not be empty";
    return false;
  }

  // TODO(jasongustaman): Query all servers concurrently.
  State* state = new State(std::move(callback), ctx);
  ares_send(channel_, msg, len, &AresClient::AresCallback, state);

  UpdateWatchers();
  return true;
}
}  // namespace dns_proxy
