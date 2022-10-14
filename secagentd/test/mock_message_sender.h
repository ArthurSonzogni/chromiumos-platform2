// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_TEST_MOCK_MESSAGE_SENDER_H_
#define SECAGENTD_TEST_MOCK_MESSAGE_SENDER_H_

#include <memory>
#include <utility>

#include "secagentd/message_sender.h"
#include "testing/gmock/include/gmock/gmock.h"
namespace secagentd {

class MockMessageSender : public MessageSenderInterface {
  MOCK_METHOD(absl::Status, InitializeQueues, (), (override));
  MOCK_METHOD(absl::Status,
              SendMessage,
              (const bpf::cros_event& event),
              (override));
};

}  // namespace secagentd
#endif  // SECAGENTD_TEST_MOCK_MESSAGE_SENDER_H_
