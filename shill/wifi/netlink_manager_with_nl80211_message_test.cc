// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides tests for individual messages.  It tests
// net_base::NetlinkMessageFactory's ability to create specific message types
// and it tests the various net_base::NetlinkMessage types' ability to parse
// those messages.

// This file tests the public interface to net_base::NetlinkManager.
#include <chromeos/net-base/netlink_manager.h>

#include <errno.h>

#include <map>
#include <string>
#include <vector>

#include <base/containers/span.h>
#include <base/logging.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <chromeos/net-base/byte_utils.h>
#include <chromeos/net-base/generic_netlink_message.h>
#include <chromeos/net-base/mock_netlink_socket.h>
#include <chromeos/net-base/netlink_message.h>
#include <chromeos/net-base/netlink_packet.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/wifi/nl80211_message.h"

using testing::_;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::Test;

namespace shill {

namespace {

// These data blocks have been collected by shill using net_base::NetlinkManager
// while, simultaneously (and manually) comparing shill output with that of the
// 'iw' code from which it was derived.  The test strings represent the raw
// packet data coming from the kernel.  The comments above each of these strings
// is the markup that "iw" outputs for ech of these packets.

// These constants are consistent throughout the packets, below.

const uint16_t kNl80211FamilyId = 0x13;

// Family and group Ids.
const char kFamilyStoogesString[] = "stooges";  // Not saved as a legal family.
const char kGroupMoeString[] = "moe";           // Not saved as a legal group.
const char kFamilyMarxString[] = "marx";
const uint16_t kFamilyMarxNumber = 20;
const char kGroupGrouchoString[] = "groucho";
const uint32_t kGroupGrouchoNumber = 21;
const char kGroupHarpoString[] = "harpo";
const uint32_t kGroupHarpoNumber = 22;
const char kGroupChicoString[] = "chico";
const uint32_t kGroupChicoNumber = 23;
const char kGroupZeppoString[] = "zeppo";
const uint32_t kGroupZeppoNumber = 24;
const char kGroupGummoString[] = "gummo";
const uint32_t kGroupGummoNumber = 25;

// wlan0 (phy #0): disconnected (by AP) reason: 2: Previous authentication no
// longer valid

const unsigned char kNL80211_CMD_DISCONNECT[] = {
    0x30, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x30, 0x01, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x36, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x47, 0x00};

const unsigned char kNLMSG_ACK[] = {0x14, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01,
                                    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Error code 1.
const unsigned char kNLMSG_Error[] = {0x14, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01,
                                      0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

const char kGetFamilyCommandString[] = "CTRL_CMD_GETFAMILY";

}  // namespace

class NetlinkManagerTest : public Test {
 public:
  NetlinkManagerTest()
      : netlink_manager_(net_base::NetlinkManager::GetInstance()),
        netlink_socket_(new net_base::MockNetlinkSocket()) {
    EXPECT_NE(nullptr, netlink_manager_);
    netlink_manager_->message_types_[Nl80211Message::kMessageTypeString]
        .family_id = kNl80211FamilyId;
    netlink_manager_->message_types_[kFamilyMarxString].family_id =
        kFamilyMarxNumber;
    netlink_manager_->message_types_[kFamilyMarxString].groups =
        std::map<std::string, uint32_t>{
            {kGroupGrouchoString, kGroupGrouchoNumber},
            {kGroupHarpoString, kGroupHarpoNumber},
            {kGroupChicoString, kGroupChicoNumber},
            {kGroupZeppoString, kGroupZeppoNumber},
            {kGroupGummoString, kGroupGummoNumber}};
    netlink_manager_->message_factory_.AddFactoryMethod(
        kNl80211FamilyId, base::BindRepeating(&Nl80211Message::CreateMessage));
    Nl80211Message::SetMessageType(kNl80211FamilyId);
    netlink_manager_->sock_.reset(netlink_socket_);  // Passes ownership.
    EXPECT_TRUE(netlink_manager_->Init());
  }

  ~NetlinkManagerTest() override {
    // net_base::NetlinkManager is a singleton, so reset its state for the next
    // test.
    netlink_manager_->Reset(true);
  }

  // |SaveReply|, |SendMessage|, and |ReplyToSentMessage| work together to
  // enable a test to get a response to a sent message.  They must be called
  // in the order, above, so that a) a reply message is available to b) have
  // its sequence number replaced, and then c) sent back to the code.
  void SaveReply(base::span<const uint8_t> message) {
    saved_message_ = {std::begin(message), std::end(message)};
  }

  // Replaces the |saved_message_|'s sequence number with the sent value.
  bool SendMessage(base::span<const uint8_t> outgoing_message) {
    if (outgoing_message.size() < sizeof(nlmsghdr)) {
      LOG(ERROR) << "Outgoing message is too short";
      return false;
    }
    const nlmsghdr* outgoing_header =
        reinterpret_cast<const nlmsghdr*>(outgoing_message.data());

    if (saved_message_.size() < sizeof(nlmsghdr)) {
      LOG(ERROR) << "Saved message is too short; have you called |SaveReply|?";
      return false;
    }
    nlmsghdr* reply_header = reinterpret_cast<nlmsghdr*>(saved_message_.data());

    reply_header->nlmsg_seq = outgoing_header->nlmsg_seq;
    saved_sequence_number_ = reply_header->nlmsg_seq;
    return true;
  }

  bool ReplyToSentMessage(std::vector<uint8_t>* message) {
    if (!message) {
      return false;
    }
    *message = saved_message_;
    return true;
  }

  bool ReplyWithRandomMessage(std::vector<uint8_t>* message) {
    net_base::GetFamilyMessage get_family_message;
    // Any number that's not 0 or 1 is acceptable, here.  Zero is bad because
    // we want to make sure that this message is different than the main
    // send/receive pair.  One is bad because the default for
    // |saved_sequence_number_| is zero and the likely default value for the
    // first sequence number generated from the code is 1.
    const uint32_t kRandomOffset = 1003;
    if (!message) {
      return false;
    }
    *message =
        get_family_message.Encode(saved_sequence_number_ + kRandomOffset);
    return true;
  }

 protected:
  class MockHandlerNetlink {
   public:
    MockHandlerNetlink()
        : on_netlink_message_(base::BindRepeating(
              &MockHandlerNetlink::OnNetlinkMessage, base::Unretained(this))) {}
    MockHandlerNetlink(const MockHandlerNetlink&) = delete;
    MockHandlerNetlink& operator=(const MockHandlerNetlink&) = delete;

    MOCK_METHOD(void, OnNetlinkMessage, (const net_base::NetlinkMessage& msg));
    const net_base::NetlinkManager::NetlinkMessageHandler& on_netlink_message()
        const {
      return on_netlink_message_;
    }

   private:
    net_base::NetlinkManager::NetlinkMessageHandler on_netlink_message_;
  };

  class MockHandlerNetlinkAuxiliary {
   public:
    MockHandlerNetlinkAuxiliary()
        : on_netlink_message_(
              base::BindRepeating(&MockHandlerNetlinkAuxiliary::OnErrorHandler,
                                  base::Unretained(this))) {}
    MockHandlerNetlinkAuxiliary(const MockHandlerNetlinkAuxiliary&) = delete;
    MockHandlerNetlinkAuxiliary& operator=(const MockHandlerNetlinkAuxiliary&) =
        delete;

    MOCK_METHOD(void,
                OnErrorHandler,
                (net_base::NetlinkManager::AuxiliaryMessageType,
                 const net_base::NetlinkMessage*));
    const net_base::NetlinkManager::NetlinkAuxiliaryMessageHandler&
    on_netlink_message() const {
      return on_netlink_message_;
    }

   private:
    net_base::NetlinkManager::NetlinkAuxiliaryMessageHandler
        on_netlink_message_;
  };

  class MockHandler80211 {
   public:
    MockHandler80211()
        : on_netlink_message_(base::BindRepeating(
              &MockHandler80211::OnNetlinkMessage, base::Unretained(this))) {}
    MockHandler80211(const MockHandler80211&) = delete;
    MockHandler80211& operator=(const MockHandler80211&) = delete;

    MOCK_METHOD(void, OnNetlinkMessage, (const Nl80211Message&));
    const Nl80211Message::Handler& on_netlink_message() const {
      return on_netlink_message_;
    }

   private:
    Nl80211Message::Handler on_netlink_message_;
  };

  class MockHandlerNetlinkAck {
   public:
    MockHandlerNetlinkAck()
        : on_netlink_message_(base::BindRepeating(
              &MockHandlerNetlinkAck::OnAckHandler, base::Unretained(this))) {}
    MockHandlerNetlinkAck(const MockHandlerNetlinkAck&) = delete;
    MockHandlerNetlinkAck& operator=(const MockHandlerNetlinkAck&) = delete;

    MOCK_METHOD(void, OnAckHandler, (bool*));
    const net_base::NetlinkManager::NetlinkAckHandler& on_netlink_message()
        const {
      return on_netlink_message_;
    }

   private:
    net_base::NetlinkManager::NetlinkAckHandler on_netlink_message_;
  };

  // Expose the private attributes of net_base::NetlinkManager.
  static constexpr base::TimeDelta kMaximumNewFamilyTimeout =
      net_base::NetlinkManager::kMaximumNewFamilyTimeout;
  static constexpr base::TimeDelta kResponseTimeout =
      net_base::NetlinkManager::kResponseTimeout;

  void ResendPendingDumpMessage() {
    netlink_manager_->ResendPendingDumpMessage();
  }
  void OnRawNlMessageReceived(base::span<const uint8_t> data) {
    netlink_manager_->OnRawNlMessageReceived(data);
  }
  void OnNlMessageReceived(net_base::NetlinkPacket* packet) {
    netlink_manager_->OnNlMessageReceived(packet);
  }
  void OnPendingDumpTimeout() { netlink_manager_->OnPendingDumpTimeout(); }
  bool IsDumpPending() { return netlink_manager_->IsDumpPending(); }
  uint32_t PendingDumpSequenceNumber() {
    return netlink_manager_->PendingDumpSequenceNumber();
  }

  std::queue<net_base::NetlinkManager::NetlinkPendingMessage>&
  pending_messages() {
    return netlink_manager_->pending_messages_;
  }
  base::CancelableOnceClosure& pending_dump_timeout_callback() {
    return netlink_manager_->pending_dump_timeout_callback_;
  }
  base::CancelableOnceClosure& resend_dump_message_callback() {
    return netlink_manager_->resend_dump_message_callback_;
  }
  void Reset() { netlink_manager_->Reset(false); }

  base::test::TaskEnvironment task_environment_{
      // required by base::FileDescriptorWatcher.
      base::test::TaskEnvironment::MainThreadType::IO,
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY,
      // required by mocking base::TimeTicks::Now().
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
  };

  net_base::NetlinkManager* netlink_manager_;
  net_base::MockNetlinkSocket* netlink_socket_;  // Owned by |netlink_manager_|.
  std::vector<uint8_t> saved_message_;
  uint32_t saved_sequence_number_ = 0;
};

TEST_F(NetlinkManagerTest, SubscribeToEvents) {
  // Family not registered.
  EXPECT_CALL(*netlink_socket_, SubscribeToEvents(_)).Times(0);
  EXPECT_FALSE(netlink_manager_->SubscribeToEvents(kFamilyStoogesString,
                                                   kGroupMoeString));

  // Group not part of family
  EXPECT_CALL(*netlink_socket_, SubscribeToEvents(_)).Times(0);
  EXPECT_FALSE(
      netlink_manager_->SubscribeToEvents(kFamilyMarxString, kGroupMoeString));

  // Family registered and group part of family.
  EXPECT_CALL(*netlink_socket_, SubscribeToEvents(kGroupHarpoNumber))
      .WillOnce(Return(true));
  EXPECT_TRUE(netlink_manager_->SubscribeToEvents(kFamilyMarxString,
                                                  kGroupHarpoString));
}

TEST_F(NetlinkManagerTest, GetFamily) {
  const uint16_t kSampleMessageType = 42;
  const std::string kSampleMessageName = "SampleMessageName";
  const uint32_t kRandomSequenceNumber = 3;

  net_base::NewFamilyMessage new_family_message;
  new_family_message.attributes()->CreateControlAttribute(CTRL_ATTR_FAMILY_ID);
  new_family_message.attributes()->SetU16AttributeValue(CTRL_ATTR_FAMILY_ID,
                                                        kSampleMessageType);
  new_family_message.attributes()->CreateControlAttribute(
      CTRL_ATTR_FAMILY_NAME);
  new_family_message.attributes()->SetStringAttributeValue(
      CTRL_ATTR_FAMILY_NAME, kSampleMessageName);

  // The sequence number is immaterial since it'll be overwritten.
  SaveReply(new_family_message.Encode(kRandomSequenceNumber));
  EXPECT_CALL(*netlink_socket_, SendMessage(_))
      .WillOnce(Invoke(this, &NetlinkManagerTest::SendMessage));
  EXPECT_CALL(*netlink_socket_, file_descriptor()).WillRepeatedly(Return(0));
  EXPECT_CALL(*netlink_socket_, WaitForRead).WillOnce(Return(1));
  EXPECT_CALL(*netlink_socket_, RecvMessage(_))
      .WillOnce(Invoke(this, &NetlinkManagerTest::ReplyToSentMessage));
  net_base::NetlinkMessageFactory::FactoryMethod null_factory;
  EXPECT_EQ(kSampleMessageType,
            netlink_manager_->GetFamily(kSampleMessageName, null_factory));
}

TEST_F(NetlinkManagerTest, GetFamilyOneInterstitialMessage) {
  Reset();

  const uint16_t kSampleMessageType = 42;
  const std::string kSampleMessageName = "SampleMessageName";
  const uint32_t kRandomSequenceNumber = 3;

  net_base::NewFamilyMessage new_family_message;
  new_family_message.attributes()->CreateControlAttribute(CTRL_ATTR_FAMILY_ID);
  new_family_message.attributes()->SetU16AttributeValue(CTRL_ATTR_FAMILY_ID,
                                                        kSampleMessageType);
  new_family_message.attributes()->CreateControlAttribute(
      CTRL_ATTR_FAMILY_NAME);
  new_family_message.attributes()->SetStringAttributeValue(
      CTRL_ATTR_FAMILY_NAME, kSampleMessageName);

  // The sequence number is immaterial since it'll be overwritten.
  SaveReply(new_family_message.Encode(kRandomSequenceNumber));
  EXPECT_CALL(*netlink_socket_, SendMessage(_))
      .WillOnce(Invoke(this, &NetlinkManagerTest::SendMessage));
  EXPECT_CALL(*netlink_socket_, file_descriptor()).WillRepeatedly(Return(0));
  EXPECT_CALL(*netlink_socket_, WaitForRead).WillRepeatedly(Return(1));
  EXPECT_CALL(*netlink_socket_, RecvMessage(_))
      .WillOnce(Invoke(this, &NetlinkManagerTest::ReplyWithRandomMessage))
      .WillOnce(Invoke(this, &NetlinkManagerTest::ReplyToSentMessage));
  net_base::NetlinkMessageFactory::FactoryMethod null_factory;
  EXPECT_EQ(kSampleMessageType,
            netlink_manager_->GetFamily(kSampleMessageName, null_factory));
}

TEST_F(NetlinkManagerTest, GetFamilyTimeout) {
  const base::TimeDelta kLargePeriod =
      kMaximumNewFamilyTimeout + base::Seconds(1);

  Reset();

  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(true));
  EXPECT_CALL(*netlink_socket_, file_descriptor()).WillRepeatedly(Return(0));
  EXPECT_CALL(*netlink_socket_, WaitForRead)
      .WillRepeatedly(InvokeWithoutArgs([&]() {
        task_environment_.FastForwardBy(kLargePeriod);
        return 1;
      }));
  EXPECT_CALL(*netlink_socket_, RecvMessage(_))
      .WillRepeatedly(
          Invoke(this, &NetlinkManagerTest::ReplyWithRandomMessage));
  net_base::NetlinkMessageFactory::FactoryMethod null_factory;

  const std::string kSampleMessageName = "SampleMessageName";
  EXPECT_EQ(net_base::NetlinkMessage::kIllegalMessageType,
            netlink_manager_->GetFamily(kSampleMessageName, null_factory));
}

TEST_F(NetlinkManagerTest, BroadcastHandler) {
  Reset();
  net_base::MutableNetlinkPacket packet(kNL80211_CMD_DISCONNECT);

  MockHandlerNetlink handler1;
  MockHandlerNetlink handler2;

  // Simple, 1 handler, case.
  EXPECT_CALL(handler1, OnNetlinkMessage(_)).Times(1);
  EXPECT_FALSE(
      netlink_manager_->FindBroadcastHandler(handler1.on_netlink_message()));
  netlink_manager_->AddBroadcastHandler(handler1.on_netlink_message());
  EXPECT_TRUE(
      netlink_manager_->FindBroadcastHandler(handler1.on_netlink_message()));
  OnNlMessageReceived(&packet);
  packet.ResetConsumedBytes();

  // Add a second handler.
  EXPECT_CALL(handler1, OnNetlinkMessage(_)).Times(1);
  EXPECT_CALL(handler2, OnNetlinkMessage(_)).Times(1);
  netlink_manager_->AddBroadcastHandler(handler2.on_netlink_message());
  OnNlMessageReceived(&packet);
  packet.ResetConsumedBytes();

  // Verify that a handler can't be added twice.
  EXPECT_CALL(handler1, OnNetlinkMessage(_)).Times(1);
  EXPECT_CALL(handler2, OnNetlinkMessage(_)).Times(1);
  netlink_manager_->AddBroadcastHandler(handler1.on_netlink_message());
  OnNlMessageReceived(&packet);
  packet.ResetConsumedBytes();

  // Check that we can remove a handler.
  EXPECT_CALL(handler1, OnNetlinkMessage(_)).Times(0);
  EXPECT_CALL(handler2, OnNetlinkMessage(_)).Times(1);
  EXPECT_TRUE(
      netlink_manager_->RemoveBroadcastHandler(handler1.on_netlink_message()));
  OnNlMessageReceived(&packet);
  packet.ResetConsumedBytes();

  // Check that re-adding the handler goes smoothly.
  EXPECT_CALL(handler1, OnNetlinkMessage(_)).Times(1);
  EXPECT_CALL(handler2, OnNetlinkMessage(_)).Times(1);
  netlink_manager_->AddBroadcastHandler(handler1.on_netlink_message());
  OnNlMessageReceived(&packet);
  packet.ResetConsumedBytes();

  // Check that ClearBroadcastHandlers works.
  netlink_manager_->ClearBroadcastHandlers();
  EXPECT_CALL(handler1, OnNetlinkMessage(_)).Times(0);
  EXPECT_CALL(handler2, OnNetlinkMessage(_)).Times(0);
  OnNlMessageReceived(&packet);
}

TEST_F(NetlinkManagerTest, MessageHandler) {
  Reset();
  MockHandlerNetlink handler_broadcast;
  EXPECT_TRUE(netlink_manager_->AddBroadcastHandler(
      handler_broadcast.on_netlink_message()));

  Nl80211Message sent_message_1(CTRL_CMD_GETFAMILY, kGetFamilyCommandString);
  MockHandler80211 handler_sent_1;

  Nl80211Message sent_message_2(CTRL_CMD_GETFAMILY, kGetFamilyCommandString);
  MockHandler80211 handler_sent_2;

  // Set up the received message as a response to sent_message_1.
  net_base::MutableNetlinkPacket received_message(kNL80211_CMD_DISCONNECT);

  // Verify that generic handler gets called for a message when no
  // message-specific handler has been installed.
  EXPECT_CALL(handler_broadcast, OnNetlinkMessage(_)).Times(1);
  OnNlMessageReceived(&received_message);
  received_message.ResetConsumedBytes();

  // Send the message and give our handler.  Verify that we get called back.
  net_base::NetlinkManager::NetlinkAuxiliaryMessageHandler null_error_handler;
  net_base::NetlinkManager::NetlinkAckHandler null_ack_handler;
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillRepeatedly(Return(true));
  EXPECT_TRUE(sent_message_1.Send(netlink_manager_,
                                  handler_sent_1.on_netlink_message(),
                                  null_ack_handler, null_error_handler));
  // Make it appear that this message is in response to our sent message.
  received_message.SetMessageSequence(netlink_socket_->GetLastSequenceNumber());
  EXPECT_CALL(handler_sent_1, OnNetlinkMessage(_)).Times(1);
  OnNlMessageReceived(&received_message);
  received_message.ResetConsumedBytes();

  // Verify that broadcast handler is called for the message after the
  // message-specific handler is called once.
  EXPECT_CALL(handler_broadcast, OnNetlinkMessage(_)).Times(1);
  OnNlMessageReceived(&received_message);
  received_message.ResetConsumedBytes();

  // Install and then uninstall message-specific handler; verify broadcast
  // handler is called on message receipt.
  EXPECT_TRUE(sent_message_1.Send(netlink_manager_,
                                  handler_sent_1.on_netlink_message(),
                                  null_ack_handler, null_error_handler));
  received_message.SetMessageSequence(netlink_socket_->GetLastSequenceNumber());
  EXPECT_TRUE(netlink_manager_->RemoveMessageHandler(sent_message_1));
  EXPECT_CALL(handler_broadcast, OnNetlinkMessage(_)).Times(1);
  OnNlMessageReceived(&received_message);
  received_message.ResetConsumedBytes();

  // Install handler for different message; verify that broadcast handler is
  // called for _this_ message.
  EXPECT_TRUE(sent_message_2.Send(netlink_manager_,
                                  handler_sent_2.on_netlink_message(),
                                  null_ack_handler, null_error_handler));
  EXPECT_CALL(handler_broadcast, OnNetlinkMessage(_)).Times(1);
  OnNlMessageReceived(&received_message);
  received_message.ResetConsumedBytes();

  // Change the ID for the message to that of the second handler; verify that
  // the appropriate handler is called for _that_ message.
  received_message.SetMessageSequence(netlink_socket_->GetLastSequenceNumber());
  EXPECT_CALL(handler_sent_2, OnNetlinkMessage(_)).Times(1);
  OnNlMessageReceived(&received_message);
}

TEST_F(NetlinkManagerTest, AckHandler) {
  Reset();

  Nl80211Message sent_message(CTRL_CMD_GETFAMILY, kGetFamilyCommandString);
  MockHandler80211 handler_sent_1;
  MockHandlerNetlinkAck handler_sent_2;

  // Send the message and give a Nl80211 response handlerand an Ack
  // handler that does not remove other callbacks after execution.
  // Receive an Ack message and verify that the Ack handler is invoked.
  net_base::NetlinkManager::NetlinkAuxiliaryMessageHandler null_error_handler;
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillRepeatedly(Return(true));
  EXPECT_TRUE(sent_message.Send(
      netlink_manager_, handler_sent_1.on_netlink_message(),
      handler_sent_2.on_netlink_message(), null_error_handler));
  // Set up message as an ack in response to sent_message.
  net_base::MutableNetlinkPacket received_ack_message(kNLMSG_ACK);

  // Make it appear that this message is in response to our sent message.
  received_ack_message.SetMessageSequence(
      netlink_socket_->GetLastSequenceNumber());
  EXPECT_CALL(handler_sent_2, OnAckHandler(_))
      .Times(1)
      .WillOnce(SetArgPointee<0>(false));  // Do not remove callbacks
  OnNlMessageReceived(&received_ack_message);

  // Receive an Nl80211 response message after handling the Ack and verify
  // that the Nl80211 response handler is invoked to ensure that it was not
  // deleted after the Ack handler was executed.
  net_base::MutableNetlinkPacket received_response_message(
      kNL80211_CMD_DISCONNECT);

  // Make it appear that this message is in response to our sent message.
  received_response_message.SetMessageSequence(
      netlink_socket_->GetLastSequenceNumber());
  EXPECT_CALL(handler_sent_1, OnNetlinkMessage(_)).Times(1);
  OnNlMessageReceived(&received_response_message);
  received_response_message.ResetConsumedBytes();

  // Send the message and give a Nl80211 response handler and Ack handler again,
  // but remove other callbacks after executing the Ack handler.
  // Receive an Ack message and verify the Ack handler is invoked.
  EXPECT_TRUE(sent_message.Send(
      netlink_manager_, handler_sent_1.on_netlink_message(),
      handler_sent_2.on_netlink_message(), null_error_handler));
  received_ack_message.ResetConsumedBytes();
  received_ack_message.SetMessageSequence(
      netlink_socket_->GetLastSequenceNumber());
  EXPECT_CALL(handler_sent_2, OnAckHandler(_))
      .Times(1)
      .WillOnce(SetArgPointee<0>(true));  // Remove callbacks
  OnNlMessageReceived(&received_ack_message);

  // Receive an Nl80211 response message after handling the Ack and verify
  // that the Nl80211 response handler is not invoked this time, since it should
  // have been deleted after calling the Ack handler.
  received_response_message.SetMessageSequence(
      received_ack_message.GetNlMsgHeader().nlmsg_seq);
  EXPECT_CALL(handler_sent_1, OnNetlinkMessage(_)).Times(0);
  OnNlMessageReceived(&received_response_message);
}

TEST_F(NetlinkManagerTest, ErrorHandler) {
  Nl80211Message sent_message(CTRL_CMD_GETFAMILY, kGetFamilyCommandString);
  MockHandler80211 handler_sent_1;
  MockHandlerNetlinkAck handler_sent_2;
  MockHandlerNetlinkAuxiliary handler_sent_3;

  // Send the message and receive a netlink reply.
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillRepeatedly(Return(true));
  EXPECT_TRUE(sent_message.Send(netlink_manager_,
                                handler_sent_1.on_netlink_message(),
                                handler_sent_2.on_netlink_message(),
                                handler_sent_3.on_netlink_message()));
  net_base::MutableNetlinkPacket received_response_message(
      kNL80211_CMD_DISCONNECT);
  received_response_message.SetMessageSequence(
      netlink_socket_->GetLastSequenceNumber());
  EXPECT_CALL(handler_sent_1, OnNetlinkMessage(_)).Times(1);
  OnNlMessageReceived(&received_response_message);

  // Send the message again, but receive an error response.
  EXPECT_TRUE(sent_message.Send(netlink_manager_,
                                handler_sent_1.on_netlink_message(),
                                handler_sent_2.on_netlink_message(),
                                handler_sent_3.on_netlink_message()));
  net_base::MutableNetlinkPacket received_error_message(kNLMSG_Error);
  received_error_message.SetMessageSequence(
      netlink_socket_->GetLastSequenceNumber());
  EXPECT_CALL(handler_sent_3,
              OnErrorHandler(net_base::NetlinkManager::kErrorFromKernel, _))
      .Times(1);
  OnNlMessageReceived(&received_error_message);

  // Put the state of the singleton back where it was.
  Reset();
}

TEST_F(NetlinkManagerTest, MultipartMessageHandler) {
  Reset();

  // Install a broadcast handler.
  MockHandlerNetlink broadcast_handler;
  EXPECT_TRUE(netlink_manager_->AddBroadcastHandler(
      broadcast_handler.on_netlink_message()));

  // Build a message and send it in order to install a response handler.
  TriggerScanMessage trigger_scan_message;
  MockHandler80211 response_handler;
  MockHandlerNetlinkAuxiliary auxiliary_handler;
  MockHandlerNetlinkAck ack_handler;
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(true));
  net_base::NetlinkManager::NetlinkAuxiliaryMessageHandler null_error_handler;
  EXPECT_TRUE(trigger_scan_message.Send(
      netlink_manager_, response_handler.on_netlink_message(),
      ack_handler.on_netlink_message(),
      auxiliary_handler.on_netlink_message()));

  // Build a multi-part response (well, it's just one message but it'll be
  // received multiple times).
  const uint32_t kSequenceNumber = 32;  // Arbitrary (replaced, later).
  NewScanResultsMessage new_scan_results;
  new_scan_results.AddFlag(NLM_F_MULTI);
  const auto new_scan_results_bytes = new_scan_results.Encode(kSequenceNumber);
  net_base::MutableNetlinkPacket received_message(new_scan_results_bytes);
  received_message.SetMessageSequence(netlink_socket_->GetLastSequenceNumber());

  // Verify that the message-specific handler is called.
  EXPECT_CALL(response_handler, OnNetlinkMessage(_));
  OnNlMessageReceived(&received_message);

  // Verify that the message-specific handler is still called.
  EXPECT_CALL(response_handler, OnNetlinkMessage(_));
  received_message.ResetConsumedBytes();
  OnNlMessageReceived(&received_message);

  // Build a Done message with the sent-message sequence number.
  net_base::DoneMessage done_message;
  done_message.AddFlag(NLM_F_MULTI);
  net_base::NetlinkPacket done_packet(
      done_message.Encode(netlink_socket_->GetLastSequenceNumber()));

  // Verify that the message-specific auxiliary handler is called for the done
  // message, with the correct message type.
  EXPECT_CALL(auxiliary_handler,
              OnErrorHandler(net_base::NetlinkManager::kDone, _));

  OnNlMessageReceived(&done_packet);

  // Verify that broadcast handler is called now that the done message has
  // been seen.
  EXPECT_CALL(response_handler, OnNetlinkMessage(_)).Times(0);
  EXPECT_CALL(auxiliary_handler, OnErrorHandler(_, _)).Times(0);
  EXPECT_CALL(ack_handler, OnAckHandler(_)).Times(0);
  EXPECT_CALL(broadcast_handler, OnNetlinkMessage(_)).Times(1);
  received_message.ResetConsumedBytes();
  OnNlMessageReceived(&received_message);
}

TEST_F(NetlinkManagerTest, TimeoutResponseHandlers) {
  const base::TimeDelta kSmallPeriod = base::Microseconds(100);
  const base::TimeDelta kLargePeriod = kResponseTimeout + base::Seconds(1);

  Reset();
  MockHandlerNetlink broadcast_handler;
  EXPECT_TRUE(netlink_manager_->AddBroadcastHandler(
      broadcast_handler.on_netlink_message()));

  // Set up the received message as a response to the get_wiphy_message
  // we're going to send.
  NewWiphyMessage new_wiphy_message;
  const uint32_t kRandomSequenceNumber = 3;
  const auto new_wiphy_message_bytes =
      new_wiphy_message.Encode(kRandomSequenceNumber);
  net_base::MutableNetlinkPacket received_message(new_wiphy_message_bytes);

  // Setup a random received message to trigger wiping out old messages.
  NewScanResultsMessage new_scan_results;
  const auto new_scan_results_bytes =
      new_scan_results.Encode(kRandomSequenceNumber);

  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillRepeatedly(Return(true));

  GetWiphyMessage get_wiphy_message;
  MockHandler80211 response_handler;
  MockHandlerNetlinkAuxiliary auxiliary_handler;
  MockHandlerNetlinkAck ack_handler;

  GetRegMessage get_reg_message;  // Just a message to trigger timeout.
  Nl80211Message::Handler null_message_handler;
  net_base::NetlinkManager::NetlinkAuxiliaryMessageHandler null_error_handler;
  net_base::NetlinkManager::NetlinkAckHandler null_ack_handler;

  // Send two messages within the message handler timeout; verify that we
  // get called back (i.e., that the first handler isn't discarded).
  EXPECT_TRUE(get_wiphy_message.Send(netlink_manager_,
                                     response_handler.on_netlink_message(),
                                     ack_handler.on_netlink_message(),
                                     auxiliary_handler.on_netlink_message()));
  task_environment_.FastForwardBy(kSmallPeriod);
  received_message.SetMessageSequence(netlink_socket_->GetLastSequenceNumber());
  EXPECT_TRUE(get_reg_message.Send(netlink_manager_, null_message_handler,
                                   null_ack_handler, null_error_handler));
  EXPECT_CALL(response_handler, OnNetlinkMessage(_));
  OnNlMessageReceived(&received_message);

  // Send two messages at an interval greater than the message handler timeout
  // before the response to the first arrives.  Verify that the error handler
  // for the first message is called (with a timeout flag) and that the
  // broadcast handler gets called, instead of the message's handler.
  EXPECT_TRUE(get_wiphy_message.Send(netlink_manager_,
                                     response_handler.on_netlink_message(),
                                     ack_handler.on_netlink_message(),
                                     auxiliary_handler.on_netlink_message()));
  received_message.ResetConsumedBytes();
  received_message.SetMessageSequence(netlink_socket_->GetLastSequenceNumber());
  EXPECT_CALL(
      auxiliary_handler,
      OnErrorHandler(net_base::NetlinkManager::kTimeoutWaitingForResponse,
                     nullptr));
  task_environment_.FastForwardBy(kLargePeriod);
  EXPECT_TRUE(get_reg_message.Send(netlink_manager_, null_message_handler,
                                   null_ack_handler, null_error_handler));
  EXPECT_CALL(response_handler, OnNetlinkMessage(_)).Times(0);
  EXPECT_CALL(broadcast_handler, OnNetlinkMessage(_));
  OnNlMessageReceived(&received_message);
}

TEST_F(NetlinkManagerTest, PendingDump) {
  // Set up the responses to the two get station messages  we're going to send.
  // The response to then first message is a 2-message multi-part response,
  // while the response to the second is a single response.
  NewStationMessage new_station_message_1_pt1;
  NewStationMessage new_station_message_1_pt2;
  NewStationMessage new_station_message_2;
  const uint32_t kRandomSequenceNumber = 3;
  new_station_message_1_pt1.AddFlag(NLM_F_MULTI);
  new_station_message_1_pt2.AddFlag(NLM_F_MULTI);
  const auto new_station_message_1_pt1_bytes =
      new_station_message_1_pt1.Encode(kRandomSequenceNumber);
  const auto new_station_message_1_pt2_bytes =
      new_station_message_1_pt2.Encode(kRandomSequenceNumber);
  const auto new_station_message_2_bytes =
      new_station_message_2.Encode(kRandomSequenceNumber);
  net_base::MutableNetlinkPacket received_message_1_pt1(
      new_station_message_1_pt1_bytes);
  net_base::MutableNetlinkPacket received_message_1_pt2(
      new_station_message_1_pt2_bytes);
  received_message_1_pt2.SetMessageType(NLMSG_DONE);
  net_base::MutableNetlinkPacket received_message_2(
      new_station_message_2_bytes);

  // The two get station messages (with the dump flag set) will be sent one
  // after another. The second message can only be sent once all replies to the
  // first have been received. The get wiphy message will be sent while waiting
  // for replies from the first get station message.
  GetStationMessage get_station_message_1;
  get_station_message_1.AddFlag(NLM_F_DUMP);
  GetStationMessage get_station_message_2;
  get_station_message_2.AddFlag(NLM_F_DUMP);
  GetWiphyMessage get_wiphy_message;
  MockHandler80211 response_handler;
  MockHandlerNetlinkAuxiliary auxiliary_handler;
  MockHandlerNetlinkAck ack_handler;

  // Send the first get station message, which should be sent immediately and
  // trigger a pending dump.
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(true));
  EXPECT_TRUE(get_station_message_1.Send(
      netlink_manager_, response_handler.on_netlink_message(),
      ack_handler.on_netlink_message(),
      auxiliary_handler.on_netlink_message()));
  uint16_t get_station_message_1_seq_num =
      netlink_socket_->GetLastSequenceNumber();
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(1, pending_messages().size());
  EXPECT_EQ(get_station_message_1_seq_num, PendingDumpSequenceNumber());

  // Send the second get station message before the replies to the first
  // get station message have been received. This should cause the message
  // to be enqueued for later sending.
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).Times(0);
  EXPECT_TRUE(get_station_message_2.Send(
      netlink_manager_, response_handler.on_netlink_message(),
      ack_handler.on_netlink_message(),
      auxiliary_handler.on_netlink_message()));
  uint16_t get_station_message_2_seq_num =
      netlink_socket_->GetLastSequenceNumber();
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(2, pending_messages().size());
  EXPECT_EQ(get_station_message_1_seq_num, PendingDumpSequenceNumber());

  // Send the get wiphy message before the replies to the first
  // get station message have been received. Since this message does not have
  // the NLM_F_DUMP flag set, it will not be enqueued and sent immediately.
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(true));
  EXPECT_TRUE(get_wiphy_message.Send(netlink_manager_,
                                     response_handler.on_netlink_message(),
                                     ack_handler.on_netlink_message(),
                                     auxiliary_handler.on_netlink_message()));
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(2, pending_messages().size());
  EXPECT_EQ(get_station_message_1_seq_num, PendingDumpSequenceNumber());

  // Now we receive the two-part response to the first message.
  // On receiving the first part, keep waiting for second part.
  received_message_1_pt1.SetMessageSequence(get_station_message_1_seq_num);
  EXPECT_CALL(response_handler, OnNetlinkMessage(_));
  OnNlMessageReceived(&received_message_1_pt1);
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(2, pending_messages().size());
  EXPECT_EQ(get_station_message_1_seq_num, PendingDumpSequenceNumber());

  // On receiving second part of the message, report done to the error handler,
  // and dispatch the next message in the queue.
  received_message_1_pt2.SetMessageSequence(get_station_message_1_seq_num);
  EXPECT_CALL(auxiliary_handler,
              OnErrorHandler(net_base::NetlinkManager::kDone, _));
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(true));
  OnNlMessageReceived(&received_message_1_pt2);
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(1, pending_messages().size());
  EXPECT_EQ(get_station_message_2_seq_num, PendingDumpSequenceNumber());

  // Receive response to second dump message, and stop waiting for dump replies.
  received_message_2.SetMessageSequence(get_station_message_2_seq_num);
  EXPECT_CALL(response_handler, OnNetlinkMessage(_));
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).Times(0);
  OnNlMessageReceived(&received_message_2);
  EXPECT_FALSE(IsDumpPending());
  EXPECT_TRUE(pending_messages().empty());
  EXPECT_EQ(0, PendingDumpSequenceNumber());

  // Put the state of the singleton back where it was.
  Reset();
}

TEST_F(NetlinkManagerTest, PendingDump_Timeout) {
  // These two messages will be sent one after another.
  GetStationMessage get_station_message_1;
  get_station_message_1.AddFlag(NLM_F_DUMP);
  GetStationMessage get_station_message_2;
  get_station_message_2.AddFlag(NLM_F_DUMP);
  MockHandler80211 response_handler;
  MockHandlerNetlinkAuxiliary auxiliary_handler;
  MockHandlerNetlinkAck ack_handler;

  // Send the first get station message, which should be sent immediately and
  // trigger a pending dump.
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(true));
  EXPECT_TRUE(get_station_message_1.Send(
      netlink_manager_, response_handler.on_netlink_message(),
      ack_handler.on_netlink_message(),
      auxiliary_handler.on_netlink_message()));
  uint16_t get_station_message_1_seq_num =
      netlink_socket_->GetLastSequenceNumber();
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(1, pending_messages().size());
  EXPECT_EQ(get_station_message_1_seq_num, PendingDumpSequenceNumber());

  // Send the second get station message before the replies to the first
  // get station message have been received. This should cause the message
  // to be enqueued for later sending.
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).Times(0);
  EXPECT_TRUE(get_station_message_2.Send(
      netlink_manager_, response_handler.on_netlink_message(),
      ack_handler.on_netlink_message(),
      auxiliary_handler.on_netlink_message()));
  uint16_t get_station_message_2_seq_num =
      netlink_socket_->GetLastSequenceNumber();
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(2, pending_messages().size());
  EXPECT_EQ(get_station_message_1_seq_num, PendingDumpSequenceNumber());

  // Timeout waiting for responses to the first get station message. This
  // should cause the first get station message to be resent.
  pending_messages().front().retries_left = 1;
  EXPECT_CALL(auxiliary_handler, OnErrorHandler(_, _)).Times(0);
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(true));
  OnPendingDumpTimeout();
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(2, pending_messages().size());
  EXPECT_EQ(get_station_message_1_seq_num, PendingDumpSequenceNumber());

  // Another timeout waiting for responses to the first get station message.
  // This should cause the second get station message to be sent.
  EXPECT_CALL(
      auxiliary_handler,
      OnErrorHandler(net_base::NetlinkManager::kTimeoutWaitingForResponse, _));
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(true));
  OnPendingDumpTimeout();
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(1, pending_messages().size());
  EXPECT_EQ(get_station_message_2_seq_num, PendingDumpSequenceNumber());

  // Put the state of the singleton back where it was.
  Reset();
}

TEST_F(NetlinkManagerTest, PendingDump_Retry) {
  const int kNumRetries = 1;
  // Create EBUSY netlink error response. Do this manually because
  // ErrorAckMessage does not implement net_base::NetlinkMessage::Encode.
  net_base::MutableNetlinkPacket received_ebusy_message(kNLMSG_ACK);
  *received_ebusy_message.GetMutablePayload() =
      net_base::byte_utils::ToBytes<uint32_t>(EBUSY);

  // The two get station messages (with the dump flag set) will be sent one
  // after another. The second message can only be sent once all replies to the
  // first have been received.
  GetStationMessage get_station_message_1;
  get_station_message_1.AddFlag(NLM_F_DUMP);
  GetStationMessage get_station_message_2;
  get_station_message_2.AddFlag(NLM_F_DUMP);
  MockHandler80211 response_handler;
  MockHandlerNetlinkAuxiliary auxiliary_handler;
  MockHandlerNetlinkAck ack_handler;

  // Send the first get station message, which should be sent immediately and
  // trigger a pending dump.
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(true));
  EXPECT_TRUE(get_station_message_1.Send(
      netlink_manager_, response_handler.on_netlink_message(),
      ack_handler.on_netlink_message(),
      auxiliary_handler.on_netlink_message()));
  uint16_t get_station_message_1_seq_num =
      netlink_socket_->GetLastSequenceNumber();
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(1, pending_messages().size());
  EXPECT_EQ(get_station_message_1_seq_num, PendingDumpSequenceNumber());

  // Send the second get station message before the replies to the first
  // get station message have been received. This should cause the message
  // to be enqueued for later sending.
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).Times(0);
  EXPECT_TRUE(get_station_message_2.Send(
      netlink_manager_, response_handler.on_netlink_message(),
      ack_handler.on_netlink_message(),
      auxiliary_handler.on_netlink_message()));
  uint16_t get_station_message_2_seq_num =
      netlink_socket_->GetLastSequenceNumber();
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(2, pending_messages().size());
  EXPECT_EQ(get_station_message_1_seq_num, PendingDumpSequenceNumber());

  // Now we receive an EBUSY error response, which should trigger a retry and
  // not invoke the error handler.
  pending_messages().front().retries_left = kNumRetries;
  received_ebusy_message.SetMessageSequence(get_station_message_1_seq_num);
  EXPECT_EQ(kNumRetries, pending_messages().front().retries_left);
  EXPECT_CALL(auxiliary_handler, OnErrorHandler(_, _)).Times(0);
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(true));
  OnNlMessageReceived(&received_ebusy_message);
  // Cancel timeout callback before attempting resend.
  EXPECT_TRUE(pending_dump_timeout_callback().IsCancelled());
  EXPECT_FALSE(resend_dump_message_callback().IsCancelled());
  // Trigger this manually instead of via message loop since it is posted as a
  // delayed task, which base::RunLoop().RunUntilIdle() will not dispatch.
  ResendPendingDumpMessage();
  EXPECT_EQ(kNumRetries - 1, pending_messages().front().retries_left);
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(2, pending_messages().size());
  EXPECT_EQ(get_station_message_1_seq_num, PendingDumpSequenceNumber());

  // We receive an EBUSY error response again. Since we have no retries left for
  // this message, the error handler should be invoked, and the next pending
  // message sent.
  received_ebusy_message.ResetConsumedBytes();
  received_ebusy_message.SetMessageSequence(get_station_message_1_seq_num);
  EXPECT_EQ(0, pending_messages().front().retries_left);
  EXPECT_CALL(auxiliary_handler,
              OnErrorHandler(net_base::NetlinkManager::kErrorFromKernel, _));
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(true));
  OnNlMessageReceived(&received_ebusy_message);
  EXPECT_TRUE(IsDumpPending());
  EXPECT_EQ(1, pending_messages().size());
  EXPECT_EQ(get_station_message_2_seq_num, PendingDumpSequenceNumber());

  // Now we receive an EBUSY error response to the second get station message,
  // which should trigger a retry. However, we fail on sending this second retry
  // out on the netlink socket. Since we expended our one retry on this attempt,
  // we should invoke the error handler and declare the dump complete.
  received_ebusy_message.ResetConsumedBytes();
  received_ebusy_message.SetMessageSequence(get_station_message_2_seq_num);
  EXPECT_EQ(1, pending_messages().front().retries_left);
  EXPECT_CALL(auxiliary_handler,
              OnErrorHandler(net_base::NetlinkManager::kErrorFromKernel, _));
  EXPECT_CALL(*netlink_socket_, SendMessage(_)).WillOnce(Return(false));
  OnNlMessageReceived(&received_ebusy_message);
  // Cancel timeout callback before attempting resend.
  EXPECT_TRUE(pending_dump_timeout_callback().IsCancelled());
  EXPECT_FALSE(resend_dump_message_callback().IsCancelled());
  // Trigger this manually instead of via message loop since it is posted as a
  // delayed task, which base::RunLoop().RunUntilIdle() will not dispatch.
  ResendPendingDumpMessage();
  EXPECT_FALSE(IsDumpPending());
  EXPECT_TRUE(pending_dump_timeout_callback().IsCancelled());
  EXPECT_TRUE(resend_dump_message_callback().IsCancelled());
  EXPECT_TRUE(pending_messages().empty());

  // Put the state of the singleton back where it was.
  Reset();
}

// Not strictly part of the "public" interface, but part of the
// external interface.
TEST_F(NetlinkManagerTest, OnInvalidRawNlMessageReceived) {
  MockHandlerNetlink message_handler;
  netlink_manager_->AddBroadcastHandler(message_handler.on_netlink_message());

  std::vector<uint8_t> bad_len_message{0x01};  // len should be 32-bits
  std::vector<uint8_t> bad_hdr_message{0x04, 0x00, 0x00, 0x00};  // only len
  std::vector<uint8_t> bad_body_message{
      0x30, 0x00, 0x00, 0x00,  // length
      0x00, 0x00,              // type
      0x00, 0x00,              // flags
      0x00, 0x00, 0x00, 0x00,  // sequence number
      0x00, 0x00, 0x00, 0x00,  // sender port
                               // Body is empty, but should be 32 bytes.
  };

  for (auto message : {bad_len_message, bad_hdr_message, bad_body_message}) {
    EXPECT_CALL(message_handler, OnNetlinkMessage(_)).Times(0);
    OnRawNlMessageReceived(message);
    Mock::VerifyAndClearExpectations(&message_handler);
  }

  std::vector<uint8_t> good_message{
      0x14, 0x00, 0x00, 0x00,  // length
      0x00, 0x00,              // type
      0x00, 0x00,              // flags
      0x00, 0x00, 0x00, 0x00,  // sequence number
      0x00, 0x00, 0x00, 0x00,  // sender port
      0x00, 0x00, 0x00, 0x00,  // body
  };

  for (auto bad_msg : {bad_len_message, bad_hdr_message, bad_body_message}) {
    // A good message followed by a bad message. This should yield one call
    // to |message_handler|, and one error message.
    std::vector<uint8_t> two_messages(good_message.begin(), good_message.end());
    two_messages.insert(two_messages.end(), bad_msg.begin(), bad_msg.end());
    EXPECT_CALL(message_handler, OnNetlinkMessage(_)).Times(1);
    OnRawNlMessageReceived(two_messages);
    Mock::VerifyAndClearExpectations(&message_handler);
  }

  EXPECT_CALL(message_handler, OnNetlinkMessage(_)).Times(0);
  OnRawNlMessageReceived({});
}

}  // namespace shill
