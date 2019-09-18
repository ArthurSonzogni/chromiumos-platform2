// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/card_qrtr.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/files/scoped_file.h>
#include <base/strings/string_number_conversions.h>
#include <fcntl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "hermes/apdu.h"
#include "hermes/sgp_22.h"
#include "hermes/socket_qrtr.h"
#include "hermes/type_traits.h"
#include "hermes/utils.h"

//
// General testing structure
// -------------------------
// The CardQrtr implementation sends and receives data from a qrtr socket, whose
// other end is a modem. In order to fake communication with the modem, the
// qrtr socket is replaced with a regular file descriptor, with the modem itself
// being faked by the CardQrtrTest testing framework.
//
// For each TEST_F(CardQrtrTest, ...) test, sending data from modem -> CardQrtr
// can be faked with CardQrtrTest::CardReceiveData(...). The CardQrtr -> modem
// messages are obviously not faked, as it is what we are testing, but
// CardQrtr::SendApdus is now wrapped by CardQrtrTest::SendApdus. The
// EXPECT_SEND macro is used to verify that the sent data is as we expected.  In
// both cases, the transaction IDs of provided data is ignored, and the proper
// transaction ID values from the calls made to CardQrtr::AllocateIds are used
// instead. This means that tests will not break if the implementation of
// AllocateIds is changed.
//

using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::Invoke;
using ::testing::WithArgs;

namespace {

constexpr uint32_t kTestNode = 0;
constexpr uint32_t kTestPort = 59;
const char* kQrtrFilename = "/tmp/hermes_qrtr_test";

constexpr auto kQrtrNewServerResp = hermes::make_array<uint8_t>(
  0x04, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x3B, 0x00, 0x00, 0x00
);

constexpr auto kQrtrResetReq = hermes::make_array<uint8_t>(
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
);

constexpr auto kQrtrResetResp = hermes::make_array<uint8_t>(
  0x02, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00,
  0x00
);

constexpr auto kQrtrOpenLogicalChannelReq = hermes::make_array<uint8_t>(
  0x00, 0x00, 0x00, 0x42, 0x00, 0x18, 0x00, 0x01, 0x01, 0x00, 0x01, 0x10, 0x11,
  0x00, 0x10, 0xA0, 0x00, 0x00, 0x05, 0x59, 0x10, 0x10, 0xFF, 0xFF, 0xFF, 0xFF,
  0x89, 0x00, 0x00, 0x01, 0x00
);

constexpr auto kQrtrOpenLogicalChannelResp = hermes::make_array<uint8_t>(
  0x02, 0x00, 0x00, 0x42, 0x00, 0x35, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x12, 0x22, 0x00, 0x21, 0x6F, 0x1F, 0x84, 0x10, 0xA0, 0x00, 0x00, 0x05,
  0x59, 0x10, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0x89, 0x00, 0x00, 0x01, 0x00, 0xA5,
  0x04, 0x9F, 0x65, 0x01, 0xFF, 0xE0, 0x05, 0x82, 0x03, 0x02, 0x00, 0x00, 0x11,
  0x02, 0x00, 0x90, 0x00, 0x10, 0x01, 0x00, 0x01
);

constexpr auto kApduPrefix = hermes::make_array<uint8_t>(
  0x00, 0x00, 0x00, 0x3B, 0x00, 0x13, 0x00, 0x01, 0x01, 0x00, 0x01, 0x02, 0x08,
  0x00, 0x06, 0x00, 0x80, 0xE2, 0x91, 0x00, 0x00
);

constexpr auto kApduSuffix = hermes::make_array<uint8_t>(
  0x10, 0x01, 0x00, 0x01
);

constexpr auto kGetChallengeApdu = hermes::make_array<uint8_t>(
  0xBF, 0x2E, 0x00
);

constexpr auto kGetChallengeResp = hermes::make_array<uint8_t>(
  0x02, 0x00, 0x00, 0x3B, 0x00, 0x23, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x10, 0x19, 0x00, 0x17, 0x00, 0xBF, 0x2E, 0x12, 0x80, 0x10, 0x5A, 0x6C,
  0x23, 0x71, 0x94, 0xBE, 0xAB, 0x24, 0xF4, 0xEF, 0xAB, 0x54, 0xB7, 0x3A, 0x59,
  0xCF, 0x90, 0x00
);

void NullResponseCallback(std::vector<std::vector<uint8_t>>& responses, int err) {}

// Create a full QRTR packet given the data of an APDU message. The current
// implementation only works for non-fragmented APDUs.
template <typename Iterator>
hermes::EnableIfIterator_t<Iterator, std::vector<uint8_t>> CreateQrtrFromApdu(
    Iterator first, Iterator last) {
  std::vector<uint8_t> result;
  result.insert(result.end(), kApduPrefix.begin(), kApduPrefix.end());
  result.insert(result.end(), first, last);
  result.insert(result.end(), kApduSuffix.begin(), kApduSuffix.end());
  result[5] = result.size() - 7;
  result[12] = result.size() - 18;
  result[14] = std::distance(first, last) + 5;
  result[20] = std::distance(first, last);
  return result;
}

}  // namespace

// Expect CardQrtr instance to send the provided vector of data to the modem.
// This macro should only be called within a CardQrtrTest.
//
// Note that the transaction ID in the provided vector will be ignored and the
// earliest unused ID from CardQrtr::AllocateId will be used instead. This
// allows for changes in message ordering to not invalidate the data passed
// to this macro.
#define EXPECT_SEND(socket_obj, data)                                         \
  EXPECT_CALL(socket_obj, Send(_, data.size(), _))                            \
    .Times(1)                                                                 \
    .WillOnce(WithArgs<0, 1>(Invoke(                                          \
      [this, d = data](const void* arr, size_t l) {                           \
        auto expected = d;                                                    \
        expected[1] = ((uint8_t*)arr)[1];                                     \
        this->receive_ids_.push_back(expected[1]);                            \
        EXPECT_THAT(expected, ElementsAreArray((uint8_t*)arr, l));            \
        return 0;                                                             \
      })))

namespace hermes {

// Socket class which mocks the outgoing (host -> modem) socket calls and
// provides implementations for incoming (modem -> host) socket calls that reads
// data from kQrtrFilename rather than from an actual QRTR socket.
class MockSocketQrtr : public SocketInterface {
 public:
  void SetDataAvailableCallback(DataAvailableCallback cb) override {
    cb_ = cb;
  }

  bool Open() override {
    socket_ = base::ScopedFD(open(kQrtrFilename, O_RDWR));
    if (!socket_.is_valid()) {
      return false;
    }
    int off = lseek(socket_.get(), 0, SEEK_SET);
    EXPECT_EQ(off, 0);
    // Return without setting up a MessageLoop::WatchFileDescriptor. The epoll
    // syscall does not always support regular file descriptors. Libevent could
    // be configured not to use epoll, but this would require modifying or
    // substituting base::MessagePumpLibevent. Instead, CardQrtrTest will
    // manually call the DataAvailableCallback as needed.
    return true;
  }

  bool IsValid() const override { return socket_.is_valid(); }
  Type GetType() const override { return Type::kQrtr; }

  int Recv(void* buf, size_t size, void* metadata) override {
    int bytes_read = read(socket_.get(), buf, size);
    EXPECT_EQ(bytes_read, size);
    LOG(INFO) << "Mock CardQrtr receiving data (" << size
              << " bytes): " << base::HexEncode(buf, size);

    if (metadata) {
      auto data = reinterpret_cast<SocketQrtr::PacketMetadata*>(metadata);
      data->node = kTestNode;
      if (size && *static_cast<uint8_t*>(buf) == QRTR_TYPE_NEW_SERVER) {
        data->port = QRTR_PORT_CTRL;
      } else {
        data->port = kTestPort;
      }
    }
    return bytes_read;
  }

  MOCK_METHOD(void, Close, (), (override));
  MOCK_METHOD(bool, StartService, (uint32_t, uint16_t, uint16_t), (override));
  MOCK_METHOD(bool, StopService, (uint32_t, uint16_t, uint16_t), (override));
  MOCK_METHOD(int, Send, (const void*, size_t, const void*), (override));

 private:
  friend class CardQrtrTest;

  base::ScopedFD socket_;
  DataAvailableCallback cb_;
};

// Test framework for CardQrtr tests. Allows for the faking of modem -> cpu
// responses with the use of CardReceiveData.
class CardQrtrTest : public testing::Test {
 public:
  CardQrtrTest() {
    auto socket = std::make_unique<MockSocketQrtr>();
    socket_ = socket.get();
    card_ = CardQrtr::Create(std::move(socket), nullptr, nullptr);
  }

 protected:
  // Fake modem initialization such that tests may jump right to sending QMI
  // commands
  void SetUp() override {
    fd_.reset(open(kQrtrFilename, O_RDWR | O_CREAT | O_TRUNC, 0777));
    ASSERT_TRUE(fd_.is_valid());

    receive_ids_.clear();
  }

  void TearDown() override {
    EXPECT_CALL(*socket_, Close());
    card_.reset(nullptr);
    fd_.reset();
  }

  // Wrapper for CardQrtr::SendApdus. Tests should use this rather than
  // CardQrtr::SendApdus.
  void SendApdus(std::vector<lpa::card::Apdu> commands,
                 CardQrtr::ResponseCallback cb) {
    EXPECT_CALL(*socket_, StartService(_, _, _))
      // Add a receive transaction id when new_lookup is called.
      .WillOnce(WithoutArgs(Invoke([this](){
        this->receive_ids_.push_back(0);
        return true; })));

    {
      ::testing::InSequence dummy;

      // Expect RESET and OPEN_LOGICAL_CHANNEL request after receiving
      // NEW_SERVER.
      EXPECT_SEND(*socket_, kQrtrResetReq);
      EXPECT_SEND(*socket_, kQrtrOpenLogicalChannelReq);
    }

    card_->SendApdus(std::move(commands), std::move(cb));
    SimulateInitialization();

    EXPECT_CALL(*socket_, StopService(_, _, _));
  }

  // Cause |card_| to receive the provided data.
  template <typename Iterator>
  EnableIfIterator_t<Iterator, void> CardReceiveData(
      Iterator first, Iterator last) {
    std::vector<uint8_t> receive_data(first, last);
    receive_data[1] = receive_ids_[0];
    receive_ids_.pop_front();

    int ret = write(fd_.get(), receive_data.data(), receive_data.size());
    EXPECT_EQ(ret, receive_data.size());
    // Set card buffer size so that the proper amount of data is read from fd.
    card_->buffer_.resize(receive_data.size());
    socket_->cb_.Run(card_->socket_.get());
  }

  void SimulateInitialization() {
    // Receive NEW_SERVER response from sock_new_lookup
    CardReceiveData(kQrtrNewServerResp.begin(), kQrtrNewServerResp.end());
    // Receive RESET response from RESET request
    CardReceiveData(kQrtrResetResp.begin(), kQrtrResetResp.end());
    // Receive repsonse to OPEN_LOGICAL_CHANNEL request
    CardReceiveData(kQrtrOpenLogicalChannelResp.begin(),
                    kQrtrOpenLogicalChannelResp.end());
  }

  base::ScopedFD fd_;
  // Queue of transaction ids created by the CardQrtr instance in question. This
  // is used such that AllocateId implementations may change without breaking
  // the unit tests (which should not be affected by changes in id allocation
  // strategy).  Send ids are ids to use when sending commands. Likewise for
  // receive ids.
  std::deque<uint16_t> receive_ids_;
  MockSocketQrtr* socket_;
  std::unique_ptr<CardQrtr> card_;
};

///////////
// TESTS //
///////////

TEST_F(CardQrtrTest, EmptyApdu) {
  auto v = std::vector<uint8_t>();
  EXPECT_SEND(*socket_, CreateQrtrFromApdu(v.begin(), v.end()));
  std::vector<lpa::card::Apdu> commands = {lpa::card::Apdu::NewStoreData({})};
  SendApdus(std::move(commands), NullResponseCallback);
}

TEST_F(CardQrtrTest, RequestGetEid) {
  EXPECT_SEND(*socket_, CreateQrtrFromApdu(kGetChallengeApdu.begin(),
                                           kGetChallengeApdu.end()));
  std::vector<lpa::card::Apdu> commands = {lpa::card::Apdu::NewStoreData(
      std::vector<uint8_t>(kGetChallengeApdu.begin(),
                           kGetChallengeApdu.end()))};
  SendApdus(std::move(commands), NullResponseCallback);
}

TEST_F(CardQrtrTest, SendTwoApdus) {
  auto v = std::vector<uint8_t>();
  {
    ::testing::InSequence dummy;

    EXPECT_SEND(*socket_, CreateQrtrFromApdu(kGetChallengeApdu.begin(),
                                             kGetChallengeApdu.end()));
    // Do not expect to reinitialize the modem in between APDUs.
    EXPECT_SEND(*socket_, CreateQrtrFromApdu(v.begin(), v.end()));
  }

  std::vector<lpa::card::Apdu> commands = {
    lpa::card::Apdu::NewStoreData(
      std::vector<uint8_t>(kGetChallengeApdu.begin(),
                           kGetChallengeApdu.end())),
      lpa::card::Apdu::NewStoreData({})};
  SendApdus(std::move(commands), NullResponseCallback);
  CardReceiveData(kGetChallengeResp.begin(),
                  kGetChallengeResp.end());
}

}  // namespace hermes
