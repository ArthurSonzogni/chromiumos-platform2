// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_TEST_MOCK_MESSAGE_SENDER_H_
#define SECAGENTD_TEST_MOCK_MESSAGE_SENDER_H_

#include <memory>

#include "gmock/gmock-function-mocker.h"
#include "google/protobuf/message_lite.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "secagentd/message_sender.h"

namespace secagentd::testing {

class MockMessageSender : public MessageSenderInterface {
 public:
  MOCK_METHOD(absl::Status, InitializeQueues, (), (override));
  MOCK_METHOD(absl::Status,
              SendMessage,
              (reporting::Destination,
               cros_xdr::reporting::CommonEventDataFields*,
               std::unique_ptr<google::protobuf::MessageLite>),
              (override));
};

}  // namespace secagentd::testing
#endif  // SECAGENTD_TEST_MOCK_MESSAGE_SENDER_H_
