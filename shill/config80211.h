// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This library provides an abstracted interface to the cfg80211 kernel module
// and mac80211 drivers.  These are accessed via a netlink socket using the
// following software stack:
//
//    [shill]
//       |
// [nl80211 library]
//       |
// [libnl_genl/libnl libraries]
//       |
//   (netlink socket)
//       |
// [cfg80211 kernel module]
//       |
// [mac80211 drivers]
//
// Messages go from user-space to kernel-space (i.e., Kernel-Bound) or in the
// other direction (i.e., User-Bound).
//
// For the love of Pete, there are a lot of different types of callbacks,
// here.  I'll try to differentiate:
//
// Config80211 Callback -
//    This is a base::Callback object installed by the user and called by
//    Config80211 for each message it receives.  More specifically, when the
//    user calls Config80211::SubscribeToEvents, Config80211 installs
//    OnRawNlMessageReceived as a netlink callback function (described below).
//    OnRawNlMessageReceived, in turn, parses the message from cfg80211 and
//    calls Config80211::Callback with the resultant UserBoundNlMessage.
//
// Netlink Callback -
//    Netlink callbacks are mechanisms installed by the user (well, by
//    Config80211 -- none of these are intended for use by users of
//    Config80211) for the libnl layer to communicate back to the user.  Some
//    callbacks are installed for global use (i.e., the default callback used
//    for all messages) or as an override for a specific message.  Netlink
//    callbacks come in three levels.
//
//    The lowest level (nl_recvmsg_msg_cb_t) is a function installed by
//    Config80211.  These are called by libnl when messages are received from
//    the kernel.
//
//    The medium level (nl_cb) is also used by Config80211.  This, the 'netlink
//    callback structure', encapsualtes a number of netlink callback functions
//    (nl_recvmsg_msg_cb_t, one each for different types of messages).
//
//    The highest level is the NetlinkSocket::Callback object.
//
// Dispatcher Callback -
//    This base::Callback is a private method of Config80211 created and
//    installed behind the scenes.  This is not the callback you're looking
//    for; move along.  This is called by shill's EventDispatcher when there's
//    data waiting for user space code on the netlink socket.  This callback
//    then calls NetlinkSocket::GetMessages which calls nl_recvmsgs_default
//    which, in turn, calls the installed netlink callback function.

#ifndef SHILL_CONFIG80211_H_
#define SHILL_CONFIG80211_H_

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include <iomanip>
#include <list>
#include <map>
#include <set>
#include <string>

#include <base/basictypes.h>
#include <base/bind.h>
#include <base/lazy_instance.h>

#include "shill/event_dispatcher.h"
#include "shill/io_handler.h"
#include "shill/nl80211_socket.h"

namespace shill {

class KernelBoundNlMessage;
class UserBoundNlMessage;

// Provides a transport-independent ability to receive status from the wifi
// configuration.  In its current implementation, it uses the netlink socket
// interface to interface with the wifi system.
//
// Config80211 is a singleton and, as such, coordinates access to libnl.
class Config80211 {
 public:
  typedef base::Callback<void(const UserBoundNlMessage &)> Callback;

  // The different kinds of events to which we can subscribe (and receive)
  // from cfg80211.
  enum EventType {
    kEventTypeConfig,
    kEventTypeScan,
    kEventTypeRegulatory,
    kEventTypeMlme,
    kEventTypeCount
  };

  // This represents whether the cfg80211/mac80211 are installed in the kernel.
  enum WifiState {
    kWifiUp,
    kWifiDown
  };

  virtual ~Config80211();

  // This is a singleton -- use Config80211::GetInstance()->Foo()
  static Config80211 *GetInstance();

  // Performs non-trivial object initialization of the Config80211 singleton.
  bool Init(EventDispatcher *dispatcher);

  // Returns the file descriptor of socket used to read wifi data.
  int GetFd() const { return (sock_ ? sock_->GetFd() : -1); }

  // Install a Config80211 Callback.  The callback is a user-supplied object
  // to be called by the system for user-bound messages that do not have a
  // corresponding messaage-specific callback.  |AddBroadcastCallback| should
  // be called before |SubscribeToEvents| since the result of this call are
  // used for that call.
  bool AddBroadcastCallback(const Callback &callback);

  // Uninstall a Config80211 Callback.
  bool RemoveBroadcastCallback(const Callback &callback);

  // Determines whether a callback is in the list of broadcast callbacks.
  bool FindBroadcastCallback(const Callback &callback) const;

  // Uninstall all Config80211 broadcast Callbacks.
  void ClearBroadcastCallbacks();

  // Install a Config80211 Callback to handle the response to a specific
  // message.
  // TODO(wdg): Eventually, this should also include a timeout and a callback
  // to call in case of timeout.
  bool SetMessageCallback(const KernelBoundNlMessage &message,
                          const Callback &callback);

  // Uninstall a Config80211 Callback for a specific message using the
  // message's sequence number.
  bool UnsetMessageCallbackById(uint32_t sequence_number);

  // Return a string corresponding to the passed-in EventType.
  static bool GetEventTypeString(EventType type, std::string *value);

  // Sign-up to receive and log multicast events of a specific type (once wifi
  // is up).
  bool SubscribeToEvents(EventType type);

  // Indicate that the mac80211 driver is up and, ostensibly, accepting event
  // subscription requests or down.
  void SetWifiState(WifiState new_state);

 protected:
  friend struct base::DefaultLazyInstanceTraits<Config80211>;

  explicit Config80211();

 private:
  friend class Config80211Test;
  FRIEND_TEST(Config80211Test, BroadcastCallbackTest);
  FRIEND_TEST(Config80211Test, MessageCallbackTest);
  typedef std::map<EventType, std::string> EventTypeStrings;
  typedef std::set<EventType> SubscribedEvents;
  typedef std::map<uint32_t, Callback> MessageCallbacks;

  // Sign-up to receive and log multicast events of a specific type (assumes
  // wifi is up).
  bool ActuallySubscribeToEvents(EventType type);

  // EventDispatcher calls this when data is available on our socket.  This
  // callback reads data from the driver, parses that data, and logs it.
  void HandleIncomingEvents(int fd);

  // This is a Netlink Callback.  libnl-80211 calls this method when it
  // receives data from cfg80211.  This method converts the 'struct nl_msg'
  // which is passed to it (and is a complete fiction -- it's actually struct
  // sk_buff, a data type to which we don't have access) to an nlmsghdr, a
  // type that we _can_ examine, and passes it to |OnNlMessageReceived|.
  static int OnRawNlMessageReceived(struct nl_msg *msg, void *arg);

  // This method processes a message from |OnRawNlMessageReceived| by passing
  // the message to either the Config80211 callback that matches the sequence
  // number of the message or, if there isn't one, to all of the default
  // Config80211 callbacks in |broadcast_callbacks_|.
  int OnNlMessageReceived(nlmsghdr *msg);

  // Just for tests, this method turns off WiFi and clears the subscribed
  // events list.
  void Reset();

  // Config80211 Callbacks, OnRawNlMessageReceived invokes each of these
  // User-supplied callback object when _it_ gets called to read libnl data.
  std::list<Callback> broadcast_callbacks_;

  // Message-specific callbacks, mapped by message ID.
  MessageCallbacks  message_callbacks_;

  static EventTypeStrings *event_types_;

  WifiState wifi_state_;

  SubscribedEvents subscribed_events_;

  // Hooks needed to be called by shill's EventDispatcher.
  EventDispatcher *dispatcher_;
  base::WeakPtrFactory<Config80211> weak_ptr_factory_;
  base::Callback<void(int)> dispatcher_callback_;
  scoped_ptr<IOHandler> dispatcher_handler_;

  Nl80211Socket *sock_;

  DISALLOW_COPY_AND_ASSIGN(Config80211);
};

}  // namespace shill

#endif  // SHILL_CONFIG80211_H_
