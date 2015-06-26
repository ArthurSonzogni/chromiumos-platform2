// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/icmp_session.h"

#include <base/test/simple_test_tick_clock.h>
#include <gtest/gtest.h>

#include "shill/mock_event_dispatcher.h"
#include "shill/mock_icmp.h"
#include "shill/net/ip_address.h"

using base::Bind;
using base::Unretained;
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;
using testing::Test;

namespace shill {

namespace {

// ICMP echo replies with 0 bytes of data and and echo ID 0. Sequence numbers
// are 0x8, 0x9, and 0xa respectively to simulate replies to a sequence of sent
// echo requests.
const uint8_t kIcmpEchoReply1[] = {0x00, 0x00, 0xf7, 0xff,
                                   0x00, 0x00, 0x08, 0x00};
const uint16_t kIcmpEchoReply1_SeqNum = 0x08;
const uint8_t kIcmpEchoReply2[] = {0x00, 0x00, 0xf6, 0xff,
                                   0x00, 0x00, 0x09, 0x00};
const uint16_t kIcmpEchoReply2_SeqNum = 0x09;
const uint8_t kIcmpEchoReply3[] = {0x00, 0x00, 0xf5, 0xff,
                                   0x00, 0x00, 0x0a, 0x00};
const uint16_t kIcmpEchoReply3_SeqNum = 0x0a;

// This ICMP echo reply has an echo ID of 0xe, which is different from the
// echo ID used in the unit tests (0).
const uint8_t kIcmpEchoReplyDifferentEchoID[] = {0x00, 0x00, 0xea, 0xff,
                                                 0x0e, 0x00, 0x0b, 0x00};

}  // namespace

MATCHER_P(IsIPAddress, address, "") {
  // IPAddress objects don't support the "==" operator as per style, so we need
  // a custom matcher.
  return address.Equals(arg);
}

class IcmpSessionTest : public Test {
 public:
  IcmpSessionTest() : icmp_session_(&dispatcher_) {}
  virtual ~IcmpSessionTest() {}

  virtual void SetUp() {
    icmp_session_.tick_clock_ = &testing_clock_;
    icmp_ = new NiceMock<MockIcmp>();
    // Passes ownership.
    icmp_session_.icmp_.reset(icmp_);
    ON_CALL(*icmp_, IsStarted()).WillByDefault(Return(false));
  }

  virtual void TearDown() {
    EXPECT_CALL(*icmp_, IsStarted());
    IcmpSession::kNextUniqueEchoId = 0;
  }

  MOCK_METHOD1(ResultCallback, void(const IcmpSession::IcmpSessionResult&));

 protected:
  static const char kIPAddress[];

  void StartAndVerify(const IPAddress& destination) {
    EXPECT_CALL(*icmp_, IsStarted());
    EXPECT_CALL(*icmp_, Start()).WillOnce(Return(true));
    EXPECT_CALL(dispatcher_, CreateInputHandler(icmp_->socket(), _, _));
    EXPECT_CALL(dispatcher_, PostDelayedTask(_, GetTimeoutSeconds() * 1000));
    EXPECT_CALL(dispatcher_, PostTask(_));
    EXPECT_TRUE(Start(destination));
    EXPECT_TRUE(GetSeqNumToSentRecvTime()->empty());
    EXPECT_TRUE(GetReceivedEchoReplySeqNumbers()->empty());
    EXPECT_CALL(*icmp_, IsStarted()).WillRepeatedly(Return(true));
  }

  bool Start(const IPAddress& destination) {
    return icmp_session_.Start(
        destination, Bind(&IcmpSessionTest::ResultCallback, Unretained(this)));
  }

  void Stop() {
    icmp_session_.Stop();
  }

  bool SeqNumToSentRecvTimeContains(uint16_t seq_num) {
    return icmp_session_.seq_num_to_sent_recv_time_.find(seq_num) !=
           icmp_session_.seq_num_to_sent_recv_time_.end();
  }

  bool ReceivedEchoReplySeqNumbersContains(uint16_t seq_num) {
    return icmp_session_.received_echo_reply_seq_numbers_.find(seq_num) !=
           icmp_session_.received_echo_reply_seq_numbers_.end();
  }

  void TransmitEchoRequestTask(const IPAddress& destination,
                               bool transmit_request_success) {
    EXPECT_CALL(*icmp_, TransmitEchoRequest(IsIPAddress(destination),
                                            icmp_session_.echo_id_,
                                            GetCurrentSequenceNumber()))
        .WillOnce(Return(transmit_request_success));
    icmp_session_.TransmitEchoRequestTask(destination);
  }

  void VerifyIcmpSessionStopped() {
    EXPECT_TRUE(icmp_session_.timeout_callback_.IsCancelled());
    EXPECT_TRUE(icmp_session_.result_callback_.is_null());
    EXPECT_FALSE(icmp_session_.echo_reply_handler_);
  }

  void OnEchoReplyReceived(InputData* data) {
    icmp_session_.OnEchoReplyReceived(data);
  }

  IcmpSession::IcmpSessionResult GenerateIcmpResult() {
    return icmp_session_.GenerateIcmpResult();
  }

  std::map<uint16_t, IcmpSession::SentRecvTimePair>* GetSeqNumToSentRecvTime() {
    return &icmp_session_.seq_num_to_sent_recv_time_;
  }
  std::set<uint16_t>* GetReceivedEchoReplySeqNumbers() {
    return &icmp_session_.received_echo_reply_seq_numbers_;
  }
  uint16_t GetNextUniqueEchoId() const {
    return IcmpSession::kNextUniqueEchoId;
  }
  int GetTotalNumEchoRequests() const {
    return IcmpSession::kTotalNumEchoRequests;
  }
  int GetCurrentSequenceNumber() const {
    return icmp_session_.current_sequence_number_;
  }
  void SetCurrentSequenceNumber(uint16_t val) {
    icmp_session_.current_sequence_number_ = val;
  }
  size_t GetTimeoutSeconds() const { return IcmpSession::kTimeoutSeconds; }
  int GetEchoRequestIntervalSeconds() const {
    return IcmpSession::kEchoRequestIntervalSeconds;
  }

  MockIcmp* icmp_;
  StrictMock<MockEventDispatcher> dispatcher_;
  IcmpSession icmp_session_;
  base::SimpleTestTickClock testing_clock_;
};

const char IcmpSessionTest::kIPAddress[] = "10.0.1.1";

TEST_F(IcmpSessionTest, Constructor) {
  // |icmp_session_| should have been assigned the value of |kNextUniqueEchoId|
  // on construction, and caused the value of this static variable to be
  // incremented.
  uint16_t saved_echo_id = GetNextUniqueEchoId();
  EXPECT_EQ(saved_echo_id - 1, icmp_session_.echo_id_);

  // The next IcmpSession object constructed, |session| should get the next
  // unique value of |kNextUniqueEchoId|, and further increment this variable.
  IcmpSession session(&dispatcher_);
  EXPECT_EQ(saved_echo_id, session.echo_id_);
  EXPECT_EQ(saved_echo_id + 1, GetNextUniqueEchoId());
}

TEST_F(IcmpSessionTest, StartWhileAlreadyStarted) {
  IPAddress ipv4_destination(IPAddress::kFamilyIPv4);
  EXPECT_TRUE(ipv4_destination.SetAddressFromString(kIPAddress));
  StartAndVerify(ipv4_destination);

  // Since an ICMP session is already started, we should fail to start it again.
  EXPECT_CALL(*icmp_, Start()).Times(0);
  EXPECT_CALL(dispatcher_, CreateInputHandler(_, _, _)).Times(0);
  EXPECT_CALL(dispatcher_, PostDelayedTask(_, _)).Times(0);
  EXPECT_CALL(dispatcher_, PostTask(_)).Times(0);
  EXPECT_FALSE(Start(ipv4_destination));
}

TEST_F(IcmpSessionTest, StopWhileNotStarted) {
  // Attempting to stop the ICMP session while it is not started should do
  // nothing.
  EXPECT_CALL(*icmp_, IsStarted()).WillOnce(Return(false));
  EXPECT_CALL(*this, ResultCallback(_)).Times(0);
  EXPECT_CALL(*icmp_, Stop()).Times(0);
  Stop();
}

TEST_F(IcmpSessionTest, SessionSuccess) {
  // Test a successful ICMP session where the sending of requests and receiving
  // of replies are interleaved. Moreover, test the case where transmitting an
  // echo request fails.

  base::TimeTicks now = testing_clock_.NowTicks();
  base::TimeTicks kSentTime1 = base::TimeTicks::FromInternalValue(10);
  base::TimeTicks kRecvTime1 = base::TimeTicks::FromInternalValue(20);
  base::TimeTicks kSentTime2 = base::TimeTicks::FromInternalValue(30);
  base::TimeTicks kSentTime3 = base::TimeTicks::FromInternalValue(40);
  base::TimeTicks kRecvTime2 = base::TimeTicks::FromInternalValue(50);
  base::TimeTicks kWrongEchoIDRecvTime = base::TimeTicks::FromInternalValue(60);
  base::TimeTicks kRecvTime3 = base::TimeTicks::FromInternalValue(70);

  IcmpSession::IcmpSessionResult expected_result;
  expected_result.push_back(kRecvTime1 - kSentTime1);
  expected_result.push_back(kRecvTime2 - kSentTime2);
  expected_result.push_back(kRecvTime3 - kSentTime3);

  // Initiate session.
  IPAddress ipv4_destination(IPAddress::kFamilyIPv4);
  EXPECT_TRUE(ipv4_destination.SetAddressFromString(kIPAddress));
  StartAndVerify(ipv4_destination);

  // Send the first echo request.
  testing_clock_.Advance(kSentTime1 - now);
  now = testing_clock_.NowTicks();
  SetCurrentSequenceNumber(kIcmpEchoReply1_SeqNum);
  EXPECT_CALL(dispatcher_,
              PostDelayedTask(_, GetEchoRequestIntervalSeconds() * 1000));
  TransmitEchoRequestTask(ipv4_destination, true);
  EXPECT_TRUE(GetReceivedEchoReplySeqNumbers()->empty());
  EXPECT_EQ(1, GetSeqNumToSentRecvTime()->size());
  EXPECT_TRUE(SeqNumToSentRecvTimeContains(kIcmpEchoReply1_SeqNum));
  EXPECT_EQ(now, GetSeqNumToSentRecvTime()->at(kIcmpEchoReply1_SeqNum).first);
  EXPECT_EQ(kIcmpEchoReply2_SeqNum, GetCurrentSequenceNumber());

  // Receive first reply.
  testing_clock_.Advance(kRecvTime1 - now);
  now = testing_clock_.NowTicks();
  uint8_t buffer_1[sizeof(kIcmpEchoReply1)];
  memcpy(buffer_1, kIcmpEchoReply1, sizeof(kIcmpEchoReply1));
  InputData data_1(reinterpret_cast<unsigned char*>(buffer_1),
                   sizeof(buffer_1));
  EXPECT_CALL(*this, ResultCallback(_)).Times(0);
  OnEchoReplyReceived(&data_1);
  EXPECT_EQ(1, GetReceivedEchoReplySeqNumbers()->size());
  EXPECT_TRUE(ReceivedEchoReplySeqNumbersContains(kIcmpEchoReply1_SeqNum));

  // Send the second echo request.
  testing_clock_.Advance(kSentTime2 - now);
  now = testing_clock_.NowTicks();
  EXPECT_CALL(dispatcher_,
              PostDelayedTask(_, GetEchoRequestIntervalSeconds() * 1000));
  TransmitEchoRequestTask(ipv4_destination, true);
  EXPECT_EQ(1, GetReceivedEchoReplySeqNumbers()->size());
  EXPECT_EQ(2, GetSeqNumToSentRecvTime()->size());
  EXPECT_TRUE(SeqNumToSentRecvTimeContains(kIcmpEchoReply2_SeqNum));
  EXPECT_EQ(now, GetSeqNumToSentRecvTime()->at(kIcmpEchoReply2_SeqNum).first);
  EXPECT_EQ(kIcmpEchoReply3_SeqNum, GetCurrentSequenceNumber());

  // Sending final request.
  testing_clock_.Advance(kSentTime3 - now);
  now = testing_clock_.NowTicks();
  EXPECT_CALL(dispatcher_, PostDelayedTask(_, _)).Times(0);
  EXPECT_CALL(*icmp_, Stop()).Times(0);
  TransmitEchoRequestTask(ipv4_destination, true);
  EXPECT_EQ(1, GetReceivedEchoReplySeqNumbers()->size());
  EXPECT_EQ(3, GetSeqNumToSentRecvTime()->size());
  EXPECT_TRUE(SeqNumToSentRecvTimeContains(kIcmpEchoReply3_SeqNum));
  EXPECT_EQ(now, GetSeqNumToSentRecvTime()->at(kIcmpEchoReply3_SeqNum).first);
  EXPECT_EQ(kIcmpEchoReply3_SeqNum + 1, GetCurrentSequenceNumber());

  // Receive second reply.
  testing_clock_.Advance(kRecvTime2 - now);
  now = testing_clock_.NowTicks();
  uint8_t buffer_2[sizeof(kIcmpEchoReply2)];
  memcpy(buffer_2, kIcmpEchoReply2, sizeof(kIcmpEchoReply2));
  InputData data_2(reinterpret_cast<unsigned char*>(buffer_2),
                   sizeof(buffer_2));
  EXPECT_CALL(*this, ResultCallback(_)).Times(0);
  EXPECT_CALL(*icmp_, Stop()).Times(0);
  OnEchoReplyReceived(&data_2);
  EXPECT_EQ(3, GetSeqNumToSentRecvTime()->size());
  EXPECT_EQ(2, GetReceivedEchoReplySeqNumbers()->size());
  EXPECT_TRUE(ReceivedEchoReplySeqNumbersContains(kIcmpEchoReply2_SeqNum));

  // Receive a reply that has an echo ID that does not match that of this
  // ICMP session. This reply will not be processed.
  testing_clock_.Advance(kWrongEchoIDRecvTime - now);
  now = testing_clock_.NowTicks();
  uint8_t buffer_3[sizeof(kIcmpEchoReplyDifferentEchoID)];
  memcpy(buffer_3, kIcmpEchoReplyDifferentEchoID,
         sizeof(kIcmpEchoReplyDifferentEchoID));
  InputData data_3(reinterpret_cast<unsigned char*>(buffer_3),
                   sizeof(buffer_3));
  EXPECT_CALL(*this, ResultCallback(_)).Times(0);
  EXPECT_CALL(*icmp_, Stop()).Times(0);
  OnEchoReplyReceived(&data_3);
  EXPECT_EQ(3, GetSeqNumToSentRecvTime()->size());
  EXPECT_EQ(2, GetReceivedEchoReplySeqNumbers()->size());

  // Receive third reply, which concludes the ICMP session.
  testing_clock_.Advance(kRecvTime3 - now);
  now = testing_clock_.NowTicks();
  uint8_t buffer_4[sizeof(kIcmpEchoReply3)];
  memcpy(buffer_4, kIcmpEchoReply3, sizeof(kIcmpEchoReply3));
  InputData data_4(reinterpret_cast<unsigned char*>(buffer_4),
                   sizeof(buffer_4));
  EXPECT_CALL(*this, ResultCallback(expected_result));
  EXPECT_CALL(*icmp_, Stop());
  OnEchoReplyReceived(&data_4);
  EXPECT_EQ(3, GetSeqNumToSentRecvTime()->size());
  EXPECT_EQ(3, GetReceivedEchoReplySeqNumbers()->size());
  EXPECT_TRUE(ReceivedEchoReplySeqNumbersContains(kIcmpEchoReply3_SeqNum));

  VerifyIcmpSessionStopped();
}

TEST_F(IcmpSessionTest, SessionTimeoutOrInterrupted) {
  // Test a failed ICMP session where we neither send out all echo requests nor
  // receive all echo replies before stopping the ICMP session (because of a
  // timeout or a manually-triggered stop). Moreover, test that echo requests
  // that are sent unsuccessfully are sent again.

  base::TimeTicks now = testing_clock_.NowTicks();
  base::TimeTicks kSentTime1 = base::TimeTicks::FromInternalValue(10);
  base::TimeTicks kSentTime2 = base::TimeTicks::FromInternalValue(20);
  base::TimeTicks kRecvTime1 = base::TimeTicks::FromInternalValue(30);
  base::TimeTicks kResendTime1 = base::TimeTicks::FromInternalValue(40);

  IcmpSession::IcmpSessionResult expected_partial_result;
  expected_partial_result.push_back(kRecvTime1 - kSentTime1);
  expected_partial_result.push_back(base::TimeDelta());

  // Initiate session.
  IPAddress ipv4_destination(IPAddress::kFamilyIPv4);
  EXPECT_TRUE(ipv4_destination.SetAddressFromString(kIPAddress));
  StartAndVerify(ipv4_destination);

  // Send the first echo request successfully.
  testing_clock_.Advance(kSentTime1 - now);
  now = testing_clock_.NowTicks();
  SetCurrentSequenceNumber(kIcmpEchoReply1_SeqNum);
  EXPECT_CALL(dispatcher_,
              PostDelayedTask(_, GetEchoRequestIntervalSeconds() * 1000));
  TransmitEchoRequestTask(ipv4_destination, true);
  EXPECT_TRUE(GetReceivedEchoReplySeqNumbers()->empty());
  EXPECT_EQ(1, GetSeqNumToSentRecvTime()->size());
  EXPECT_TRUE(SeqNumToSentRecvTimeContains(kIcmpEchoReply1_SeqNum));
  EXPECT_EQ(now, GetSeqNumToSentRecvTime()->at(kIcmpEchoReply1_SeqNum).first);
  EXPECT_EQ(kIcmpEchoReply2_SeqNum, GetCurrentSequenceNumber());

  // Send the second echo request unsuccessfully.
  testing_clock_.Advance(kSentTime2 - now);
  now = testing_clock_.NowTicks();
  EXPECT_CALL(dispatcher_,
              PostDelayedTask(_, GetEchoRequestIntervalSeconds() * 1000));
  TransmitEchoRequestTask(ipv4_destination, false);
  EXPECT_TRUE(GetReceivedEchoReplySeqNumbers()->empty());
  EXPECT_EQ(1, GetSeqNumToSentRecvTime()->size());
  EXPECT_FALSE(SeqNumToSentRecvTimeContains(kIcmpEchoReply2_SeqNum));
  // The sequence number should still be incremented when we fail to transmit an
  // echo request.
  EXPECT_EQ(kIcmpEchoReply3_SeqNum, GetCurrentSequenceNumber());

  // Receive first reply.
  testing_clock_.Advance(kRecvTime1 - now);
  now = testing_clock_.NowTicks();
  uint8_t buffer_1[sizeof(kIcmpEchoReply1)];
  memcpy(buffer_1, kIcmpEchoReply1, sizeof(kIcmpEchoReply1));
  InputData data_1(reinterpret_cast<unsigned char*>(buffer_1),
                   sizeof(buffer_1));
  EXPECT_CALL(*this, ResultCallback(_)).Times(0);
  OnEchoReplyReceived(&data_1);
  EXPECT_EQ(1, GetReceivedEchoReplySeqNumbers()->size());
  EXPECT_TRUE(ReceivedEchoReplySeqNumbersContains(kIcmpEchoReply1_SeqNum));

  // Resend second echo request successfully.
  testing_clock_.Advance(kResendTime1 - now);
  now = testing_clock_.NowTicks();
  EXPECT_CALL(dispatcher_,
              PostDelayedTask(_, GetEchoRequestIntervalSeconds() * 1000));
  TransmitEchoRequestTask(ipv4_destination, true);
  EXPECT_EQ(1, GetReceivedEchoReplySeqNumbers()->size());
  EXPECT_EQ(2, GetSeqNumToSentRecvTime()->size());
  EXPECT_TRUE(SeqNumToSentRecvTimeContains(kIcmpEchoReply3_SeqNum));
  EXPECT_EQ(now, GetSeqNumToSentRecvTime()->at(kIcmpEchoReply3_SeqNum).first);
  EXPECT_EQ(kIcmpEchoReply3_SeqNum + 1, GetCurrentSequenceNumber());

  // Timeout triggered or session manually stopped, so we report partial
  // results.
  EXPECT_CALL(*this, ResultCallback(expected_partial_result));
  EXPECT_CALL(*icmp_, Stop());
  Stop();
  EXPECT_EQ(2, GetSeqNumToSentRecvTime()->size());
  EXPECT_EQ(1, GetReceivedEchoReplySeqNumbers()->size());

  VerifyIcmpSessionStopped();
}

}  // namespace shill
