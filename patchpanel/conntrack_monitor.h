// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_CONNTRACK_MONITOR_H_
#define PATCHPANEL_CONNTRACK_MONITOR_H_

#include <stdint.h>
#include <sys/types.h>

#include <memory>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <libnetfilter_conntrack/libnetfilter_conntrack.h>
#include <net-base/ip_address.h>
#include <net-base/socket.h>

namespace patchpanel {

// This class manages a conntrack monitor that can observe changes of socket
// connections in conntrack table in a non-blocking way. Other components can
// get notifications of socket connection updates by registering a callback.
// The type of socket events (new, update, or destroy) to monitor can be set
// with |events| when the monitor is created.
// Currently the monitor only supports: TCP, UDP.
class ConntrackMonitor {
 public:
  // enum for conntrack events we want to monitor.
  enum class EventType {
    kNew,
    kUpdate,
    kDestroy,
  };

  // Struct for a conntrack table socket event.
  struct Event {
    net_base::IPAddress src;
    net_base::IPAddress dst;
    uint16_t sport;
    uint16_t dport;
    uint8_t proto;
    // Type for this event, one of kNew, kUpdate, kDestroy.
    EventType type;
    // State for the socket. One of TCP_CONNTRACK_* like constant.
    uint8_t state;
  };

  // Callback for listening to conntrack table socket connection changes
  // of specified types set in `Create()`.
  using ConntrackEventHandler =
      base::RepeatingCallback<void(const Event& sock_event)>;

  // Create conntrack monitor. Event types we want to monitor can be specified.
  // The caller must pass a valid |factory| to the method.
  static std::unique_ptr<ConntrackMonitor> Create(
      base::span<const EventType> events,
      std::unique_ptr<net_base::SocketFactory> factory =
          std::make_unique<net_base::SocketFactory>());

  ~ConntrackMonitor() = default;
  ConntrackMonitor(const ConntrackMonitor&) = delete;
  ConntrackMonitor& operator=(const ConntrackMonitor&) = delete;

  // Register a callback to listen to conntrack updates.
  void RegisterConntrackEventHandler(const ConntrackEventHandler& handler);

 protected:
  explicit ConntrackMonitor(std::unique_ptr<net_base::Socket>);

 private:
  static constexpr int kDefaultBufSize = 4096;

  // Receive and parse buffer from socket when socket is readable.
  void OnSocketReadable();
  // Parse buffer received from sockets and notify registered handlers of
  // conntrack table updates.
  void Process(ssize_t len);

  // Buffer used to receive message from netlink socket.
  uint8_t buf_[kDefaultBufSize];
  // The netlink socket used to get conntrack events.
  std::unique_ptr<net_base::Socket> sock_;
  // File descriptor watcher to watch for readability of file descriptor of
  // |sock_|. Note that |watcher_| needs to be reset before |sock_| is reset.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
  // List of callbacks that listen to conntrack table socket connection
  // changes.
  std::vector<ConntrackEventHandler> event_handlers_;

  base::WeakPtrFactory<ConntrackMonitor> weak_factory_{this};
};
}  // namespace patchpanel

#endif  // PATCHPANEL_CONNTRACK_MONITOR_H_
