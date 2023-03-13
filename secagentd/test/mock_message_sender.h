// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_TEST_MOCK_MESSAGE_SENDER_H_
#define SECAGENTD_TEST_MOCK_MESSAGE_SENDER_H_

#include <memory>

#include "gmock/gmock.h"  // IWYU pragma: keep
#include "google/protobuf/message_lite.h"
#include "missive/proto/record_constants.pb.h"
#include "secagentd/message_sender.h"
#include "secagentd/proto/security_xdr_events.pb.h"

namespace secagentd::testing {

class MockMessageSender : public MessageSenderInterface {
 public:
  MOCK_METHOD(absl::Status, Initialize, (), (override));
  MOCK_METHOD(void,
              SendMessage,
              (reporting::Destination,
               cros_xdr::reporting::CommonEventDataFields*,
               std::unique_ptr<google::protobuf::MessageLite>,
               std::optional<reporting::ReportQueue::EnqueueCallback> cb),
              (override));
};

template <typename KeyType, typename XdrMessage, typename AtomicVariantMessage>
class MockBatchSender
    : public BatchSenderInterface<KeyType, XdrMessage, AtomicVariantMessage> {
 public:
  using VisitCallback =
      typename BatchSenderInterface<KeyType, XdrMessage, AtomicVariantMessage>::
          VisitCallback;
  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(void,
              Enqueue,
              (std::unique_ptr<AtomicVariantMessage>),
              (override));
  MOCK_METHOD(bool,
              Visit,
              (typename AtomicVariantMessage::VariantTypeCase,
               const KeyType&,
               VisitCallback),
              (override));
};

}  // namespace secagentd::testing
#endif  // SECAGENTD_TEST_MOCK_MESSAGE_SENDER_H_
