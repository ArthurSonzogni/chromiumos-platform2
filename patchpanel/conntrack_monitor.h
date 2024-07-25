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

#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <base/observer_list.h>
#include <base/observer_list_types.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/mock_socket.h>
#include <chromeos/net-base/socket.h>
#include <libnetfilter_conntrack/libnetfilter_conntrack.h>

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
  // TODO(chuweih): Change |proto| and |state| into enum class.
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

  // Starts the conntrack monitor. Creates a base::FileDescriptorWatcher and add
  // it to the current message loop. The types of conntrack events this monitor
  // handles is set by |events|.
  explicit ConntrackMonitor(
      base::span<const EventType> events,
      std::unique_ptr<net_base::SocketFactory> socket_factory =
          std::make_unique<net_base::SocketFactory>());
  virtual ~ConntrackMonitor();

  ConntrackMonitor(const ConntrackMonitor&) = delete;
  ConntrackMonitor& operator=(const ConntrackMonitor&) = delete;

  // Checks if |sock_| is null, only for testing purpose.
  bool IsSocketNullForTesting() const { return sock_ == nullptr; }

  // Adds an conntrack event listener to the list of entities that will
  // be notified of conntrack events.
  virtual std::unique_ptr<Listener> AddListener(
      base::span<const EventType> events,
      const ConntrackEventHandler& callback);

 protected:
  static constexpr uint8_t kDefaultEventBitMask = 0;
  static constexpr uint8_t kNewEventBitMask = (1 << 0);
  static constexpr uint8_t kUpdateEventBitMask = (1 << 1);
  static constexpr uint8_t kDestroyEventBitMask = (1 << 2);

  // Convert EventType enum into bit mask.
  static uint8_t EventTypeToMask(ConntrackMonitor::EventType event);

  // Dispatches a conntrack event to all listeners.
  void DispatchEvent(const Event& msg);

 private:
  // Receives and parses buffer from socket when socket is readable, and
  // notifies registered handlers of conntrack table updates.
  void OnSocketReadable();

  // The netlink socket used to get conntrack events.
  std::unique_ptr<net_base::Socket> sock_;
  // List of callbacks that listen to conntrack table socket connection
  // changes.
  std::vector<ConntrackEventHandler> event_handlers_;

  // List of listeners for conntrack table socket connection changes.
  base::ObserverList<Listener> listeners_;

  // Bit mask for event types handled by this monitor, this value is by set
  // by caller when `Start()` is called. Listeners can only listen to events
  // this monior handles.
  uint8_t event_mask_;

  base::WeakPtrFactory<ConntrackMonitor> weak_factory_{this};
};
}  // namespace patchpanel

#endif  // PATCHPANEL_CONNTRACK_MONITOR_H_
