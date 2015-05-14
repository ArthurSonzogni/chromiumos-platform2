// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_NOTIFICATION_XMPP_CHANNEL_H_
#define BUFFET_NOTIFICATION_XMPP_CHANNEL_H_

#include <memory>
#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/task_runner.h>
#include <chromeos/backoff_entry.h>
#include <chromeos/streams/stream.h>

#include "buffet/notification/notification_channel.h"

namespace buffet {

class XmppChannel : public NotificationChannel {
 public:
  // |account| is the robot account for buffet and |access_token|
  // it the OAuth token. Note that the OAuth token expires fairly frequently
  // so you will need to reset the XmppClient every time this happens.
  XmppChannel(const std::string& account,
              const std::string& access_token,
              const scoped_refptr<base::TaskRunner>& task_runner);
  virtual ~XmppChannel() = default;

  // Overrides from NotificationChannel.
  std::string GetName() const override;
  void AddChannelParameters(base::DictionaryValue* channel_json) override;
  void Start(NotificationDelegate* delegate) override;
  void Stop() override;

  // Internal states for the XMPP stream.
  enum class XmppState {
    kNotStarted,
    kStarted,
    kAuthenticationStarted,
    kAuthenticationFailed,
    kStreamRestartedPostAuthentication,
    kBindSent,
    kSessionStarted,
    kSubscribeStarted,
    kSubscribed,
  };

 protected:
  virtual void Connect(const std::string& host, uint16_t port,
                       const base::Closure& callback);

  XmppState state_{XmppState::kNotStarted};

  // The connection socket stream to the XMPP server.
  chromeos::Stream* stream_{nullptr};

 private:
  void SendMessage(const std::string& message);
  void WaitForMessage();

  void OnConnected();
  void OnMessageRead(size_t size);
  void OnMessageSent();
  void OnError(const chromeos::Error* error);

  // Robot account name for the device.
  std::string account_;

  // OAuth access token for the account. Expires fairly frequently.
  std::string access_token_;

  chromeos::StreamPtr raw_socket_;

  // Read buffer for incoming message packets.
  std::vector<char> read_socket_data_;
  // Write buffer for outgoing message packets.
  std::string write_socket_data_;

  chromeos::BackoffEntry backoff_entry_;
  NotificationDelegate* delegate_{nullptr};
  scoped_refptr<base::TaskRunner> task_runner_;

  base::WeakPtrFactory<XmppChannel> weak_ptr_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(XmppChannel);
};

}  // namespace buffet

#endif  // BUFFET_NOTIFICATION_XMPP_CHANNEL_H_

