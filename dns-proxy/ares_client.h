// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DNS_PROXY_ARES_CLIENT_H_
#define DNS_PROXY_ARES_CLIENT_H_

#include <ares.h>

#include <map>
#include <memory>
#include <set>
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
  using QueryCallback = base::RepeatingCallback<void(
      void* ctx, int status, uint8_t* msg, size_t len)>;

  AresClient(base::TimeDelta timeout,
             int max_num_retries,
             int max_concurrent_queries);
  virtual ~AresClient();

  // Resolve DNS address using wire-format data |data| of size |len|.
  // |callback| will be called with |ctx| upon query completion.
  // |msg| and |ctx| is owned by the caller of this function. The caller is
  // responsible for their lifecycle.
  // The callback will return the wire-format response.
  // See: |QueryCallback|
  //
  // `SetNameServers(...)` must be called before calling this function.
  virtual bool Resolve(const unsigned char* msg,
                       size_t len,
                       const QueryCallback& callback,
                       void* ctx);

  // Set the target name servers to resolve DNS to.
  virtual void SetNameServers(const std::vector<std::string>& name_servers);

 private:
  // State of an individual request.
  struct State {
    State(AresClient* client,
          ares_channel channel,
          const QueryCallback& callback,
          void* ctx);

    // |client| holds the current class holding this state.
    AresClient* client;

    // Upon calling resolve, all available name servers will be queried
    // concurrently. |channel| is a communications channel that holds the
    // queries.
    ares_channel channel;

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

  // Handle result of `AresCallback(...)`. Running ares functions on the
  // callback results in an undefined behavior, use another function
  // instead.
  void HandleResult(State* state,
                    int status,
                    std::unique_ptr<uint8_t[]> msg,
                    int len);

  // Callback called whenever an event is ready to be handled by ares on
  // |socket_fd|.
  void OnFileCanReadWithoutBlocking(ares_channel channel,
                                    ares_socket_t socket_fd);
  void OnFileCanWriteWithoutBlocking(ares_channel channel,
                                     ares_socket_t socket_fd);

  // Reset the current timeout callback and process all timed out requests.
  void ResetTimeout(ares_channel channel);

  // Initialize an ares channel. This will used for holding multiple concurrent
  // queries.
  ares_channel InitChannel();

  // Update file descriptors to be watched.
  // |read_watchers_| and |write_watchers_| stores the watchers.
  // Because there is no callback to know unused ares sockets, update the
  // watchers whenever:
  // - a query is started,
  // - an action is done for any ares socket.
  //
  // Whenever this is called, |read_watchers_| and |write_watchers_| will
  // be cleared and reset to sockets that needs to be watched.
  void UpdateWatchers(ares_channel channel);

  // Vector of watchers. This will be reconstructed on each ares action.
  // See `UpdateWatchers(...)` on how the values are set and cleared.
  std::map<
      ares_channel,
      std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>>
      read_watchers_;
  std::map<
      ares_channel,
      std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>>
      write_watchers_;

  // Timeout for an ares query.
  base::TimeDelta timeout_;

  // Maximum number of retries for an ares query.
  int max_num_retries_;

  // Maximum number of concurrent queries for a request.
  int max_concurrent_queries_;

  // |channels_inflight_| stores all active channels. Each channel consists of
  // a number of queries as ares runs multiple queries concurrently.
  // A channel will be added to the set when it is created and will be removed
  // from the set when it is destroyed.
  // This will be used for callbacks to know whether a request is completed.
  std::set<ares_channel> channels_inflight_;

  // |name_servers_| endpoint to resolve addresses.
  std::string name_servers_;

  // Number of stored name servers.
  int num_name_servers_;

  base::WeakPtrFactory<AresClient> weak_factory_{this};
};
}  // namespace dns_proxy

#endif  // DNS_PROXY_ARES_CLIENT_H_
