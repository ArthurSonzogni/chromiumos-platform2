// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_CONNTRACK_MONITOR_H_
#define PATCHPANEL_CONNTRACK_MONITOR_H_

#include <stdint.h>
#include <sys/types.h>

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/lazy_instance.h>
#include <base/observer_list.h>
#include <base/functional/callback.h>
#include <base/observer_list_types.h>
#include <libnetfilter_conntrack/libnetfilter_conntrack.h>
#include <net-base/ip_address.h>
#include <net-base/mock_socket.h>
#include <net-base/socket.h>

namespace patchpanel {
// This singleton class manages a conntrack monitor that can observe changes of
// socket connections in conntrack table in a non-blocking way. Other
// components can get notifications of socket connection updates by registering
// a callback.
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

    friend bool operator==(const Event&, const Event&);
  };

  // Callback for listening to conntrack table socket connection changes
  // of specified types set in `AddListener()`.
  using ConntrackEventHandler =
      base::RepeatingCallback<void(const Event& sock_event)>;

  // This class is a listener for conntrack events. Callbacks can be resgitered
  // for conntrack events by calling `ConntrackMonitor::AddListener()` and
  // event types (list of ConntrackMonitor::EventType) can be specified when
  // adding listener.
  // User will take over ownership of listener by obtaining and unique pointer
  // of this object when calling `ConntrackMonitor::AddListener()` and
  // a listener will be automatically unregistered from listener list when it
  // is deconstructed.
  class Listener : public base::CheckedObserver {
   public:
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    ~Listener() override;

   private:
    friend class ConntrackMonitor;
    Listener(uint8_t listen_flags,
             const ConntrackEventHandler& callback,
             ConntrackMonitor* monitor);

    void NotifyEvent(const Event& msg) const;

    uint8_t listen_flags_;
    const base::RepeatingCallback<void(const Event&)> callback_;
    ConntrackMonitor* monitor_;
  };

  // Gets a pointer for this singleton class.
  static ConntrackMonitor* GetInstance();

  ConntrackMonitor(const ConntrackMonitor&) = delete;
  ConntrackMonitor& operator=(const ConntrackMonitor&) = delete;

  // Starts the event-monitoring function of the conntrack monitor. This
  // function will create a base::FileDescriptorWatcher and add it to the
  // current message loop. The types of conntrack events this monitor handles is
  // set by |events|.
  void Start(base::span<const EventType> events);

  // Stops the event-monitoring function of the conntrack monitor, only for
  // testing purpose.
  void StopForTesting();

  // Sets the socket factory, only for testing purpose.
  void SetSocketFactoryForTesting(
      std::unique_ptr<net_base::MockSocketFactory> factory) {
    socket_factory_ = std::move(factory);
  }

  // Checks if |sock_| is null, only for testing purpose.
  bool IsSocketNullForTesting() const { return sock_ == nullptr; }

  // Adds an conntrack event listener to the list of entities that will
  // be notified of conntrack events.
  std::unique_ptr<Listener> AddListener(base::span<const EventType> events,
                                        const ConntrackEventHandler& callback);

 protected:
  explicit ConntrackMonitor(std::unique_ptr<net_base::Socket>);
  ConntrackMonitor();
  ~ConntrackMonitor();

 private:
  friend base::LazyInstanceTraitsBase<ConntrackMonitor>;

  static constexpr int kDefaultBufSize = 4096;

  // Dispatches a conntrack event to all listeners.
  void DispatchEvent(const Event& msg);

  // Receives and parses buffer from socket when socket is readable.
  void OnSocketReadable();

  // Parses buffer received from sockets and notify registered handlers of
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

  // List of listeners for conntrack table socket connection changes.
  base::ObserverList<Listener> listeners_;

  std::unique_ptr<net_base::SocketFactory> socket_factory_ =
      std::make_unique<net_base::SocketFactory>();

  // Bit mask for event types handled by this monitor, this value is by set
  // by caller when `Start()` is called. Listeners can only listen to events
  // this monior handles.
  uint8_t event_mask_;
};
}  // namespace patchpanel

#endif  // PATCHPANEL_CONNTRACK_MONITOR_H_
