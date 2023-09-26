// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dns_client.h"

#include <netdb.h>
#include <sys/time.h>

#include <memory>
#include <string>
#include <vector>

#include <base/functional/bind.h>
#include <base/strings/stringprintf.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <net-base/mock_socket.h>

#include "shill/error.h"
#include "shill/mock_ares.h"
#include "shill/mock_event_dispatcher.h"

using testing::_;
using testing::DoAll;
using testing::Not;
using testing::Return;
using testing::ReturnArg;
using testing::SetArgPointee;
using testing::StrEq;
using testing::StrictMock;
using testing::Test;

namespace shill {

namespace {
const char kGoodName[] = "all-systems.mcast.net";
const char kResult[] = "224.0.0.1";
const char kGoodServer[] = "8.8.8.8";
const char kBadServer[] = "10.9xx8.7";
const char kNetworkInterface[] = "eth0";
char kReturnAddressList0[] = {static_cast<char>(224), 0, 0, 1};
char* kReturnAddressList[] = {kReturnAddressList0, nullptr};
char kFakeAresChannelData = 0;
const ares_channel kAresChannel =
    reinterpret_cast<ares_channel>(&kFakeAresChannelData);
const base::TimeDelta kAresTimeout =
    base::Seconds(2);  // ARES transaction timeout
const base::TimeDelta kAresWait =
    base::Seconds(1);  // Time period ARES asks caller to wait

// Matches the base::expected<net_base::IPAddress, Error> argument that has
// value.
MATCHER(HasValue, "") {
  return arg.has_value();
}

// Matches the base::expected<net_base::IPAddress, Error> argument that has
// error.
MATCHER_P2(IsError, error_type, error_message, "") {
  return !arg.has_value() && error_type == arg.error().type() &&
         error_message == arg.error().message();
}

}  // namespace

class DnsClientTest : public Test {
 public:
  DnsClientTest() : ares_result_(ARES_SUCCESS), address_result_(std::nullopt) {
    hostent_.h_addrtype = net_base::ToSAFamily(net_base::IPFamily::kIPv4);
    hostent_.h_length = sizeof(kReturnAddressList0);
    hostent_.h_addr_list = kReturnAddressList;
  }

  void SetUp() override { SetInActive(); }

  void TearDown() override {
    // We need to make sure the dns_client instance releases ares_
    // before the destructor for DnsClientTest deletes ares_.
    if (dns_client_) {
      dns_client_->Stop();
    }
  }

  void CallReplyCB() {
    dns_client_->ReceiveDnsReplyCB(dns_client_.get(), ares_result_, 0,
                                   &hostent_);
  }

  void CallDnsRead() { dns_client_->HandleDnsRead(fake_ares_socket_.Get()); }

  void CallDnsWrite() { dns_client_->HandleDnsWrite(fake_ares_socket_.Get()); }

  void CallTimeout() { dns_client_->HandleTimeout(); }

  void CallCompletion() { dns_client_->HandleCompletion(); }

  void CreateClient(base::TimeDelta timeout) {
    dns_client_ = std::make_unique<DnsClient>(
        net_base::IPFamily::kIPv4, kNetworkInterface, timeout, &dispatcher_,
        callback_target_.callback());
    dns_client_->ares_ = &ares_;
  }

  void SetActive() {
    const struct timeval ares_timeout = {
        .tv_sec = static_cast<time_t>(kAresWait.InSeconds()),
        .tv_usec = static_cast<suseconds_t>(kAresWait.InMicroseconds() %
                                            base::Time::kMicrosecondsPerSecond),
    };

    // Returns that ares socket is readable.
    EXPECT_CALL(ares_, GetSock(_, _, _))
        .WillRepeatedly(
            DoAll(SetArgPointee<1>(fake_ares_socket_.Get()), Return(1)));
    EXPECT_CALL(ares_, Timeout(_, _, _))
        .WillRepeatedly(DoAll(SetArgPointee<2>(ares_timeout), ReturnArg<2>()));
  }

  void SetInActive() {
    EXPECT_CALL(ares_, GetSock(_, _, _)).WillRepeatedly(Return(0));
    EXPECT_CALL(ares_, Timeout(_, _, _)).WillRepeatedly(ReturnArg<1>());
  }

  void StartValidRequest() {
    CreateClient(kAresTimeout);

    SetActive();
    EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, kAresWait));
    EXPECT_CALL(ares_, InitOptions(_, _, _))
        .WillOnce(DoAll(SetArgPointee<0>(kAresChannel), Return(ARES_SUCCESS)));
    EXPECT_CALL(ares_, SetLocalDev(kAresChannel, StrEq(kNetworkInterface)))
        .Times(1);
    EXPECT_CALL(ares_, SetServersCsv(_, StrEq(kGoodServer)))
        .WillOnce(Return(ARES_SUCCESS));
    EXPECT_CALL(ares_, GetHostByName(kAresChannel, StrEq(kGoodName), _, _, _));
    EXPECT_CALL(ares_, Destroy(kAresChannel));

    Error error;
    ASSERT_TRUE(dns_client_->Start({kGoodServer}, kGoodName, &error));
    EXPECT_TRUE(error.IsSuccess());
  }

  void TestValidCompletion() {
    EXPECT_CALL(ares_, ProcessFd(kAresChannel, fake_ares_socket_.Get(),
                                 ARES_SOCKET_BAD))
        .WillOnce(InvokeWithoutArgs(this, &DnsClientTest::CallReplyCB));
    ExpectPostCompletionTask();
    CallDnsRead();

    // Make sure that the address value is correct as held in the DnsClient.
    const auto ipaddr = *net_base::IPAddress::CreateFromString(kResult);
    EXPECT_EQ(ipaddr, dns_client_->address_);

    // Make sure the callback gets called with a success result, and save
    // the callback address argument in |address_result_|.
    EXPECT_CALL(callback_target_, CallTarget(HasValue()))
        .WillOnce(Invoke(this, &DnsClientTest::SaveCallbackArgs));
    CallCompletion();

    // Make sure the address was successfully passed to the callback.
    EXPECT_EQ(ipaddr, address_result_);
    EXPECT_TRUE(dns_client_->address_.IsZero());
  }

  void SaveCallbackArgs(
      const base::expected<net_base::IPAddress, Error>& address) {
    if (address.has_value()) {
      address_result_ = *address;
    } else {
      error_result_ = address.error();
    }
  }

  void ExpectPostCompletionTask() {
    EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, base::TimeDelta()));
  }

  void ExpectReset() {
    EXPECT_TRUE(dns_client_->address_.GetFamily() == net_base::IPFamily::kIPv4);
    EXPECT_TRUE(dns_client_->address_.IsZero());
    EXPECT_EQ(nullptr, dns_client_->resolver_state_);
  }

 protected:
  class DnsCallbackTarget {
   public:
    DnsCallbackTarget()
        : callback_(base::BindRepeating(&DnsCallbackTarget::CallTarget,
                                        base::Unretained(this))) {}

    MOCK_METHOD(void,
                CallTarget,
                ((const base::expected<net_base::IPAddress, Error>&)));

    const DnsClient::ClientCallback& callback() const { return callback_; }

   private:
    DnsClient::ClientCallback callback_;
  };

  base::test::TaskEnvironment task_environment_{
      // required by base::FileDescriptorWatcher.
      base::test::TaskEnvironment::MainThreadType::IO,

      // required by base::TimeTicks::Now().
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  net_base::MockSocket fake_ares_socket_;

  std::unique_ptr<DnsClient> dns_client_;
  StrictMock<MockEventDispatcher> dispatcher_;
  std::string queued_request_;
  StrictMock<DnsCallbackTarget> callback_target_;
  StrictMock<MockAres> ares_;
  struct hostent hostent_;
  int ares_result_;
  Error error_result_;
  std::optional<net_base::IPAddress> address_result_;
};

TEST_F(DnsClientTest, Constructor) {
  CreateClient(kAresTimeout);
  ExpectReset();
}

// Correctly handles empty server addresses.
TEST_F(DnsClientTest, ServerJoin) {
  CreateClient(kAresTimeout);
  EXPECT_CALL(ares_, InitOptions(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(kAresChannel), Return(ARES_SUCCESS)));
  EXPECT_CALL(ares_, SetServersCsv(_, StrEq(kGoodServer)))
      .WillOnce(Return(ARES_SUCCESS));
  EXPECT_CALL(ares_, SetLocalDev(kAresChannel, StrEq(kNetworkInterface)))
      .Times(1);
  EXPECT_CALL(ares_, GetHostByName(kAresChannel, StrEq(kGoodName), _, _, _));

  SetActive();
  EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, kAresWait));
  Error error;
  ASSERT_TRUE(dns_client_->Start({"", kGoodServer, "", ""}, kGoodName, &error));
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_CALL(ares_, Destroy(kAresChannel));
}

// Receive error because no DNS servers were specified.
TEST_F(DnsClientTest, NoServers) {
  CreateClient(kAresTimeout);
  Error error;
  EXPECT_FALSE(dns_client_->Start({}, kGoodName, &error));
  EXPECT_EQ(Error::kInvalidArguments, error.type());
}

// Setup error because SetServersCsv failed due to invalid DNS servers.
TEST_F(DnsClientTest, SetServersCsvInvalidServer) {
  CreateClient(kAresTimeout);
  EXPECT_CALL(ares_, InitOptions(_, _, _)).WillOnce(Return(ARES_SUCCESS));
  EXPECT_CALL(ares_, SetServersCsv(_, StrEq(kBadServer)))
      .WillOnce(Return(ARES_EBADSTR));
  Error error;
  EXPECT_FALSE(dns_client_->Start({kBadServer}, kGoodName, &error));
  EXPECT_EQ(Error::kOperationFailed, error.type());
}

// Setup error because InitOptions failed.
TEST_F(DnsClientTest, InitOptionsFailure) {
  CreateClient(kAresTimeout);
  EXPECT_CALL(ares_, InitOptions(_, _, _)).WillOnce(Return(ARES_EBADFLAGS));
  Error error;
  EXPECT_FALSE(dns_client_->Start({kGoodServer}, kGoodName, &error));
  EXPECT_EQ(Error::kOperationFailed, error.type());
}

// Fail a second request because one is already in progress.
TEST_F(DnsClientTest, MultipleRequest) {
  StartValidRequest();
  EXPECT_TRUE(dns_client_->IsActive());
  Error error;
  ASSERT_FALSE(dns_client_->Start({kGoodServer}, kGoodName, &error));
  EXPECT_EQ(Error::kInProgress, error.type());
}

TEST_F(DnsClientTest, GoodRequest) {
  StartValidRequest();
  TestValidCompletion();
}

TEST_F(DnsClientTest, GoodRequestWithTimeout) {
  StartValidRequest();
  // Insert an intermediate HandleTimeout callback.
  task_environment_.FastForwardBy(kAresWait);
  EXPECT_CALL(ares_, ProcessFd(kAresChannel, ARES_SOCKET_BAD, ARES_SOCKET_BAD));
  EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, kAresWait));
  CallTimeout();
  task_environment_.FastForwardBy(kAresWait);
  TestValidCompletion();
}

TEST_F(DnsClientTest, GoodRequestWithDnsRead) {
  StartValidRequest();
  // Insert an intermediate HandleDnsRead callback.
  task_environment_.FastForwardBy(kAresWait);
  EXPECT_CALL(
      ares_, ProcessFd(kAresChannel, fake_ares_socket_.Get(), ARES_SOCKET_BAD));
  EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, kAresWait));
  CallDnsRead();
  task_environment_.FastForwardBy(kAresWait);
  TestValidCompletion();
}

TEST_F(DnsClientTest, GoodRequestWithDnsWrite) {
  StartValidRequest();
  // Insert an intermediate HandleDnsWrite callback.
  task_environment_.FastForwardBy(kAresWait);
  EXPECT_CALL(
      ares_, ProcessFd(kAresChannel, ARES_SOCKET_BAD, fake_ares_socket_.Get()));
  EXPECT_CALL(dispatcher_, PostDelayedTask(_, _, kAresWait));
  CallDnsWrite();
  task_environment_.FastForwardBy(kAresWait);
  TestValidCompletion();
}

// Failure due to the timeout occurring during first call to RefreshHandles.
TEST_F(DnsClientTest, TimeoutFirstRefresh) {
  CreateClient(kAresTimeout);
  EXPECT_CALL(ares_, InitOptions(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(kAresChannel), Return(ARES_SUCCESS)));
  EXPECT_CALL(ares_, SetLocalDev(kAresChannel, StrEq(kNetworkInterface)))
      .Times(1);
  EXPECT_CALL(ares_, SetServersCsv(_, StrEq(kGoodServer)))
      .WillOnce(Return(ARES_SUCCESS));
  EXPECT_CALL(ares_, GetHostByName(kAresChannel, StrEq(kGoodName), _, _, _))
      .WillOnce([&]() {
        // Simulate the function call takes a long time.
        task_environment_.FastForwardBy(kAresTimeout);
      });
  EXPECT_CALL(callback_target_, CallTarget(Not(HasValue()))).Times(0);
  EXPECT_CALL(ares_, Destroy(kAresChannel));
  Error error;
  // Expect the DnsClient to post a completion task.  However this task will
  // never run since the Stop() gets called before returning.  We confirm
  // that the task indeed gets canceled below in ExpectReset().
  ExpectPostCompletionTask();
  ASSERT_FALSE(dns_client_->Start({kGoodServer}, kGoodName, &error));

  EXPECT_EQ(Error::kOperationTimeout, error.type());
  EXPECT_EQ(std::string(DnsClient::kErrorTimedOut), error.message());
  ExpectReset();
}

// Failed request due to timeout within the dns_client.
TEST_F(DnsClientTest, TimeoutDispatcherEvent) {
  StartValidRequest();
  EXPECT_CALL(ares_, ProcessFd(kAresChannel, ARES_SOCKET_BAD, ARES_SOCKET_BAD));
  task_environment_.FastForwardBy(kAresTimeout);
  ExpectPostCompletionTask();
  CallTimeout();
  EXPECT_CALL(callback_target_, CallTarget(IsError(Error::kOperationTimeout,
                                                   DnsClient::kErrorTimedOut)));
  CallCompletion();
}

// Failed request due to timeout reported by ARES.
TEST_F(DnsClientTest, TimeoutFromARES) {
  StartValidRequest();
  task_environment_.FastForwardBy(kAresWait);
  ares_result_ = ARES_ETIMEOUT;
  EXPECT_CALL(ares_, ProcessFd(kAresChannel, ARES_SOCKET_BAD, ARES_SOCKET_BAD))
      .WillOnce(InvokeWithoutArgs(this, &DnsClientTest::CallReplyCB));
  ExpectPostCompletionTask();
  CallTimeout();
  EXPECT_CALL(callback_target_, CallTarget(IsError(Error::kOperationTimeout,
                                                   DnsClient::kErrorTimedOut)));
  CallCompletion();
}

// Failed request due to "host not found" reported by ARES.
TEST_F(DnsClientTest, HostNotFound) {
  StartValidRequest();
  task_environment_.FastForwardBy(kAresWait);
  ares_result_ = ARES_ENOTFOUND;
  EXPECT_CALL(ares_,
              ProcessFd(kAresChannel, fake_ares_socket_.Get(), ARES_SOCKET_BAD))
      .WillOnce(InvokeWithoutArgs(this, &DnsClientTest::CallReplyCB));
  ExpectPostCompletionTask();
  CallDnsRead();
  EXPECT_CALL(callback_target_, CallTarget(IsError(Error::kOperationFailed,
                                                   DnsClient::kErrorNotFound)));
  CallCompletion();
}

}  // namespace shill
