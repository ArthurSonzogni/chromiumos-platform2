// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_RTNL_HANDLER_H_
#define NET_BASE_RTNL_HANDLER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/containers/span.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/callback.h>
#include <base/lazy_instance.h>
#include <base/memory/ref_counted.h>
#include <base/observer_list.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "net-base/export.h"
#include "net-base/ip_address.h"
#include "net-base/mac_address.h"
#include "net-base/rtnl_listener.h"
#include "net-base/rtnl_message.h"
#include "net-base/socket.h"

namespace net_base {

// This singleton class is responsible for interacting with the RTNL subsystem.
// RTNL provides (among other things) access to interface discovery (add/remove
// events), interface state monitoring and the ability to change interface
// flags. Similar functionality also exists for IP address configuration for
// interfaces and IP routing tables.
//
// RTNLHandler provides access to these events through a callback system and
// provides utility functions to make changes to interface, address and routing
// state.
class NET_BASE_EXPORT RTNLHandler {
 public:
  // TODO(crbug.com/1005487): use this for all user-triggered messages.
  // |error| is a positive errno or 0 for acknowledgements.
  using ResponseCallback = base::OnceCallback<void(int32_t error)>;

  // Request mask.
  static constexpr uint32_t kRequestLink = (1 << 0);
  static constexpr uint32_t kRequestAddr = (1 << 1);
  static constexpr uint32_t kRequestRoute = (1 << 2);
  static constexpr uint32_t kRequestRule = (1 << 3);
  static constexpr uint32_t kRequestNdUserOption = (1 << 4);
  static constexpr uint32_t kRequestNeighbor = (1 << 5);
  static constexpr uint32_t kRequestBridgeNeighbor = (1 << 6);

  virtual ~RTNLHandler();

  // Since this is a singleton, use RTNHandler::GetInstance()->Foo().
  static RTNLHandler* GetInstance();

  // This starts the event-monitoring function of the RTNL handler. This
  // function will create a base::FileDescriptorWatcher and add it to the
  // current message loop.
  virtual void Start(uint32_t netlink_groups_mask);

  // Add an RTNL event listener to the list of entities that will
  // be notified of RTNL events.
  virtual void AddListener(RTNLListener* to_add);

  // Remove a previously added RTNL event listener
  virtual void RemoveListener(RTNLListener* to_remove);

  // Set flags on a network interface that has a kernel index of
  // 'interface_index'.  Only the flags bits set in 'change' will
  // be set, and they will be set to the corresponding bit in 'flags'.
  virtual void SetInterfaceFlags(int interface_index,
                                 unsigned int flags,
                                 unsigned int change);

  // Set the maximum transmission unit (MTU) for the network interface that
  // has a kernel index of |interface_index|.
  virtual void SetInterfaceMTU(int interface_index, unsigned int mtu);

  // Set the MAC address for the network interface that has a kernel index of
  // |interface_index|.
  virtual void SetInterfaceMac(int interface_index,
                               const MacAddress& mac_address);

  // Set the MAC address for the network interface that has a kernel index of
  // |interface_index|. |response_callback| will be called when appropriate
  // |NLMSG_ERROR| message received.
  virtual void SetInterfaceMac(int interface_index,
                               const MacAddress& mac_address,
                               ResponseCallback response_callback);

  // Set address of a network interface that has a kernel index of
  // 'interface_index'.
  virtual bool AddInterfaceAddress(int interface_index,
                                   const IPCIDR& local,
                                   const std::optional<IPv4Address>& broadcast);

  // Remove address from a network interface that has a kernel index of
  // 'interface_index'.
  virtual bool RemoveInterfaceAddress(int interface_index, const IPCIDR& local);

  // Remove a network interface from the kernel.
  virtual bool RemoveInterface(int interface_index);

  // Request that various tables (link, address, routing) tables be
  // exhaustively dumped via RTNL.  As results arrive from the kernel
  // they will be broadcast to all listeners.  The possible values
  // (multiple can be ORred together) are below.
  virtual void RequestDump(uint32_t request_flags);

  // Returns the index of interface |interface_name|, or -1 if unable to
  // determine the index.
  virtual int GetInterfaceIndex(const std::string& interface_name);

  // Creates a new interface with name |interface_name| and type |link_kind| via
  // a RTM_NEWLINK request. |link_info_data| will be appended as the value of
  // IFLA_INFO_DATA if not empty. Returns false on sending failures.
  virtual bool AddInterface(const std::string& interface_name,
                            const std::string& link_kind,
                            base::span<const uint8_t> link_info_data,
                            ResponseCallback response_callback);

  // Sends an RTNL message. If the message is successfully sent, and |seq| is
  // not null, then it will be set to the message's assigned sequence number.
  virtual bool SendMessage(std::unique_ptr<RTNLMessage> message, uint32_t* seq);

 protected:
  RTNLHandler();
  RTNLHandler(const RTNLHandler&) = delete;
  RTNLHandler& operator=(const RTNLHandler&) = delete;

 private:
  using ErrorMask = std::set<int>;

  friend base::LazyInstanceTraitsBase<RTNLHandler>;
  friend class RTNLHandlerTest;
  friend class RTNLHandlerFuzz;
  friend class RTNLListenerTest;

  FRIEND_TEST(RTNLHandlerTest, SendMessageInferredErrorMasks);
  FRIEND_TEST(RTNLListenerTest, NoRun);
  FRIEND_TEST(RTNLListenerTest, Run);

  // Size of the window for receiving error sequences out-of-order.
  static const uint32_t kErrorWindowSize;
  // Size of the window for maintaining RTNLMessages in |stored_requests_| that
  // haven't yet gotten a response.
  static const uint32_t kStoredRequestWindowSize;

  // This stops the event-monitoring function of the RTNL handler -- it is
  // private since it will never happen in normal running, but is useful for
  // tests.
  void Stop();

  // Called by OnReadError to clear |response_callbacks_|, |stored_requests_|
  // reset netlink socket, sequence number and create new socket.
  void ResetSocket();

  // Called by the |socket_watcher_| when the |rtnl_socket_| is ready to read.
  void OnReadable();

  // Dispatches an rtnl message to all listeners
  void DispatchEvent(uint32_t type, const RTNLMessage& msg);
  // Send the next table-dump request to the kernel
  void NextRequest(uint32_t seq);
  // Parse an incoming rtnl message from the kernel
  void ParseRTNL(base::span<const uint8_t> data);

  bool AddressRequest(int interface_index,
                      RTNLMessage::Mode mode,
                      int flags,
                      const IPCIDR& local,
                      const std::optional<IPv4Address>& broadcast);

  // Send a formatted RTNL message.  Associates an error mask -- a list
  // of errors that are expected and should not trigger log messages by
  // default -- with the outgoing message.  If the message is sent
  // successfully, the sequence number in |message| is set, and the
  // function returns true.  Otherwise this function returns false.
  bool SendMessageWithErrorMask(std::unique_ptr<RTNLMessage> message,
                                const ErrorMask& error_mask,
                                uint32_t* msg_seq);

  // Returns whether |sequence| lies within the current error mask window.
  bool IsSequenceInErrorMaskWindow(uint32_t sequence);

  // Saves an error mask to be associated with this sequence number.
  void SetErrorMask(uint32_t sequence, const ErrorMask& error_mask);

  // Destructively retrieves the error mask associated with this sequence
  // number.  If this sequence number now lies outside the receive window
  // or no error mask was assigned, an empty ErrorMask is returned.
  ErrorMask GetAndClearErrorMask(uint32_t sequence);

  // This method assumes that |request| is a more recent request than all
  // previous requests passed here (i.e. that this method is called in order).
  //
  // Storing a request when there is already a request stored with the same
  // sequence number will result in the stored request being updated by the new
  // request.
  void StoreRequest(std::unique_ptr<RTNLMessage> request);
  // Removes a stored request from |stored_requests_| and returns it. Returns
  // nullptr if there is no request stored with that sequence.
  std::unique_ptr<RTNLMessage> PopStoredRequest(uint32_t seq);
  uint32_t CalculateStoredRequestWindowSize();

  std::unique_ptr<SocketFactory> socket_factory_ =
      std::make_unique<SocketFactory>();
  bool in_request_;

  std::unique_ptr<Socket> rtnl_socket_;
  // Watcher to wait for |rtnl_socket_| ready to read. It should be destructed
  // prior than |rtnl_socket_|, so it's declared after |rtnl_socket_|.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> socket_watcher_;

  uint32_t netlink_groups_mask_;
  uint32_t request_flags_;
  uint32_t request_sequence_;
  uint32_t last_dump_sequence_;
  // Sequence of the oldest request stored in |stored_requests_|.
  uint32_t oldest_request_sequence_;
  // Mapping of sequence number to corresponding RTNLMessage.
  std::map<uint32_t, std::unique_ptr<RTNLMessage>> stored_requests_;

  base::ObserverList<RTNLListener> listeners_;
  std::vector<ErrorMask> error_mask_window_;

  // Once |NLMSG_ERROR| message was received, appropriate response_callback
  // matched by message sequence id must be called with encoded error in
  // |NLMSG_ERROR| message.
  std::unordered_map<uint32_t, ResponseCallback> response_callbacks_;
};

}  // namespace net_base

#endif  // NET_BASE_RTNL_HANDLER_H_
