// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_ETHERNET_EAP_LISTENER_H_
#define SHILL_ETHERNET_EAP_LISTENER_H_

#include <memory>
#include <string>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/callback.h>
#include <net-base/socket.h>

namespace shill {

// Listens for EAP packets on |interface_index| and invokes a
// callback when a request frame arrives.
class EapListener {
 public:
  using EapRequestReceivedCallback = base::RepeatingCallback<void()>;

  EapListener(int interface_index, const std::string& link_name);
  EapListener(const EapListener&) = delete;
  EapListener& operator=(const EapListener&) = delete;

  virtual ~EapListener();

  // Create a socket for tranmission and reception.  Returns true
  // if successful, false otherwise.
  virtual bool Start();

  // Destroy the client socket.
  virtual void Stop();

  // Setter for |request_received_callback_|.
  virtual void set_request_received_callback(
      const EapRequestReceivedCallback& callback) {
    request_received_callback_ = callback;
  }

 private:
  friend class EapListenerTest;

  // The largest EAP packet we expect to receive.
  static const size_t kMaxEapPacketLength;

  // Creates the socket file descriptor. Returns nullptr on failure.
  std::unique_ptr<net_base::Socket> CreateSocket();

  // Retrieves an EAP packet from |socket_|.  This is the callback method
  // configured on |socket_watcher_|.
  void ReceiveRequest();

  const std::string& LoggingTag();

  // The interface index for the device to monitor.
  const int interface_index_;

  // The link name of the parent device (for logging).
  const std::string link_name_;

  // Callback handle to invoke when an EAP request is received.
  EapRequestReceivedCallback request_received_callback_;

  // Used to create |socket_|.
  std::unique_ptr<net_base::SocketFactory> socket_factory_ =
      std::make_unique<net_base::SocketFactory>();

  // Receive socket configured to receive PAE (Port Access Entity) packets.
  std::unique_ptr<net_base::Socket> socket_;

  // Watcher to wait for |socket_| ready to read. It should be destructed
  // prior than |socket_|, so it's declared after |socket_|.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> socket_watcher_;
};

}  // namespace shill

#endif  // SHILL_ETHERNET_EAP_LISTENER_H_
