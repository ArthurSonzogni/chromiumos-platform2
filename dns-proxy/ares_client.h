// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DNS_PROXY_ARES_CLIENT_H_
#define DNS_PROXY_ARES_CLIENT_H_

#include <ares.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/time/time.h>

namespace dns_proxy {

// AresClient resolves DNS queries by forwarding wire-format DNS queries to the
// assigned servers, concurrently.
// The caller of AresClient will get a wire-format response done through ares.
// Given multiple DNS servers, AresClient will query each servers concurrently.
// It will return only the first successful response OR the last failing
// response.
class AresClient {
 public:
  // Callback to be invoked back to the client upon request completion.
  // |ctx| is an argument passed by the caller of `Resolve(...)` and passed
  // back upon completion. |ctx| is owned by the caller of `Resolve(...)` and
  // the caller is responsible of its lifecycle. DoHCurlClient does not own
  // |ctx| and must not interact with |ctx|.
  // |status| stores the ares result of the ares query.
  // |msg| and |len| respectively stores the response and length of the
  // response of the ares query.
  // This function follows ares's ares_callback signature.
  using QueryCallback =
      base::OnceCallback<void(void* ctx, int status, uint8_t* msg, size_t len)>;

  explicit AresClient(base::TimeDelta timeout);
  ~AresClient();

  // Resolve DNS address using wire-format data |data| of size |len|.
  // |callback| will be called with |ctx| upon query completion.
  // |msg| and |ctx| is owned by the caller of this function. The caller is
  // responsible for their lifecycle.
  // The callback will return the wire-format response.
  // See: |QueryCallback|
  //
  // `SetNameServers(...)` must be called before calling this function.
  bool Resolve(const unsigned char* msg,
               size_t len,
               QueryCallback callback,
               void* ctx);

  // Set the target name servers to resolve DNS to.
  void SetNameServers(const std::vector<std::string>& name_servers);

 private:
  // State of a request.
  struct State {
    State(QueryCallback callback, void* ctx);

    // |callback| given from the client will be called with |ctx| as its
    // parameter. |ctx| is owned by the caller of `Resolve(...)` and will
    // be returned to the caller as-is through the parameter of |callback|.
    // |ctx| is owned by the caller of `Resolve(...)` and must not be changed
    // here.
    QueryCallback callback;
    void* ctx;
  };

  // Callback informed about what to wait for. When called, register or remove
  // the socket given from watchers.
  // |ctx| is owned by the caller of `Resolve(...)` and will
  // be returned to the caller as-is through the parameter of |callback|.
  // |ctx| must not be changed here.
  // |msg| is owned by ares, AresClient and the caller of `Resolve(...)` do not
  // need to handle the lifecycle of |msg|.
  static void AresCallback(
      void* ctx, int status, int timeouts, uint8_t* msg, int len);

  // Callback called whenever an event is ready to be handled by ares on
  // |socket_fd|.
  void OnFileCanReadWithoutBlocking(ares_socket_t socket_fd);
  void OnFileCanWriteWithoutBlocking(ares_socket_t socket_fd);

  // Update file descriptors to be watched.
  // |read_watchers_| and |write_watchers_| stores the watchers.
  // Because there is no callback to know unused ares sockets, update the
  // watchers whenever:
  // - a query is started,
  // - an action is done for any ares socket.
  //
  // Whenever this is called, |read_watchers_| and |write_watchers_| will
  // be cleared and reset to sockets that needs to be watched.
  void UpdateWatchers();

  // Vector of watchers. This will be reconstructed on each ares action.
  // See `UpdateWatchers(...)` on how the values are set and cleared.
  std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      read_watchers_;
  std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      write_watchers_;

  // |name_servers_| endpoint to resolve addresses.
  std::vector<std::string> name_servers_;

  // Communications channel for name service lookups.
  ares_channel channel_;

  base::WeakPtrFactory<AresClient> weak_factory_{this};
};
}  // namespace dns_proxy

#endif  // DNS_PROXY_ARES_CLIENT_H_
