// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/dns_client.h"

#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "net-base/ares_interface.h"
#include "net-base/ip_address.h"

namespace net_base {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::Field;
using ::testing::Ge;
using ::testing::Pointee;
using ::testing::StrEq;
using ::testing::StrictMock;

using Error = DNSClient::Error;
using Result = DNSClient::Result;

struct AresAddrinfoContext {
  std::vector<struct sockaddr_in> sockaddr_in_list;
  std::vector<struct sockaddr_in6> sockaddr_in6_list;
  std::vector<struct ares_addrinfo_node> nodes;
  struct ares_addrinfo info = {};
};

// Generates the ares_addrinfo struct and holds the elements pointed to by it.
// The result is wrapped by unique_ptr to prevent copying, as ares_addrinfo
// stores pointers to internal fields. Copying the struct would lead to
// dangling pointers.
std::unique_ptr<AresAddrinfoContext> GenerateAresAddrinfoContext(
    const std::vector<IPAddress> addrs, std::optional<IPFamily> hint_family) {
  auto ret = std::make_unique<AresAddrinfoContext>();

  // Convert to the list of sockaddr_in and sockaddr_in6 instances, and store
  // them at `ret.sockaddr_in_list` and `ret.sockaddr_in6_list` fields.
  for (const IPAddress& ip : addrs) {
    if (hint_family && ip.GetFamily() != *hint_family) {
      continue;
    }

    if (const auto ipv4 = ip.ToIPv4Address(); ipv4.has_value()) {
      struct sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_addr = ipv4->ToInAddr();
      ret->sockaddr_in_list.push_back(addr);
    } else if (const auto ipv6 = ip.ToIPv6Address(); ipv6.has_value()) {
      struct sockaddr_in6 addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin6_addr = ipv6->ToIn6Addr();
      ret->sockaddr_in6_list.push_back(addr);
    }
  }

  // Generate the linked list of ares_addrinfo_node, and store the nodes at
  // the `ret.nodes` field.
  for (struct sockaddr_in& addr : ret->sockaddr_in_list) {
    struct ares_addrinfo_node node;
    memset(&node, 0, sizeof(node));
    node.ai_family = AF_INET;
    node.ai_addrlen = sizeof(addr);
    node.ai_addr = reinterpret_cast<sockaddr*>(&addr);
    ret->nodes.push_back(node);
  }
  for (struct sockaddr_in6& addr : ret->sockaddr_in6_list) {
    struct ares_addrinfo_node node;
    memset(&node, 0, sizeof(node));
    node.ai_family = AF_INET6;
    node.ai_addrlen = sizeof(addr);
    node.ai_addr = reinterpret_cast<sockaddr*>(&addr);
    ret->nodes.push_back(node);
  }
  for (size_t idx = 0; idx + 1 < ret->nodes.size(); idx++) {
    ret->nodes[idx].ai_next = &ret->nodes[idx + 1];
  }

  // Generate the ares_addrinfo that points to the linked list `ret.nodes`.
  memset(&ret->info, 0, sizeof(ret->info));
  if (!ret->nodes.empty()) {
    ret->info.nodes = &ret->nodes[0];
  }

  return ret;
}

class FakeAres : public AresInterface {
 public:
  ~FakeAres() override;

  int init_options(ares_channel* channelptr,
                   struct ares_options* options,
                   int optmask) override;

  void destroy(ares_channel channel) override;

  void set_local_dev(ares_channel channel, const char* local_dev_name) override;

  void getaddrinfo(ares_channel channel,
                   const char* name,
                   const char* service,
                   const struct ares_addrinfo_hints* hints,
                   ares_addrinfo_callback callback,
                   void* arg) override;
  void freeaddrinfo(struct ares_addrinfo* ai) override;

  struct timeval* timeout(ares_channel channel,
                          struct timeval* maxtv,
                          struct timeval* tv) override;

  int getsock(ares_channel channel,
              ares_socket_t* socks,
              int numsocks) override;

  void process_fd(ares_channel channel,
                  ares_socket_t read_fd,
                  ares_socket_t write_fd) override;

  int set_servers_csv(ares_channel channel, const char* servers) override;

  // The client of FakeAres will get the event that socket is readable or
  // writable.
  void TriggerReadReady();
  void TriggerWriteReady();

  // The next process_fd() call will invoke the callback with the given
  // parameters.
  void InvokeCallbackOnNextProcessFD(int status, std::vector<IPAddress> addrs);

 private:
  struct GetaddrinfoParams {
    std::optional<IPFamily> hint_family = std::nullopt;
    void* arg = nullptr;
    ares_addrinfo_callback callback = nullptr;
  };

  struct CallbackResult {
    int status = 0;
    std::vector<IPAddress> addrs;
  };

  ares_channel CreateChannel() {
    CHECK_EQ(channel_, nullptr) << "Channel has been created";
    // Note that the value doesn't important here. It will only be used as an
    // identifier so it only needs to be unique.
    channel_ = this;
    return reinterpret_cast<ares_channel>(channel_);
  }

  void CheckChannel(ares_channel channel) {
    CHECK_EQ(channel, channel_) << "Input channel does not match";
  }

  void VerifyReadFD(int fd);
  void BlockWriteFD();

  void* channel_ = nullptr;

  std::unique_ptr<GetaddrinfoParams> getaddrinfo_params_;

  base::ScopedFD read_fd_local_;
  base::ScopedFD read_fd_remote_;
  base::ScopedFD write_fd_local_;
  base::ScopedFD write_fd_remote_;

  std::optional<CallbackResult> callback_result_;
};

FakeAres::~FakeAres() {
  CHECK_EQ(channel_, nullptr)
      << "Channel is not nullptr, perhaps no call to ares_destroy()?";
}

int FakeAres::init_options(ares_channel* channelptr,
                           struct ares_options* options,
                           int optmask) {
  LOG(INFO) << __func__ << ": " << channelptr;
  *channelptr = CreateChannel();
  return ARES_SUCCESS;
}

void FakeAres::destroy(ares_channel channel) {
  CheckChannel(channel);
  if (getaddrinfo_params_) {
    getaddrinfo_params_->callback(getaddrinfo_params_->arg, ARES_EDESTRUCTION,
                                  /*timeouts=*/0, /*result=*/nullptr);
    getaddrinfo_params_ = nullptr;
  }

  channel_ = nullptr;
  read_fd_local_.reset();
  read_fd_remote_.reset();
  write_fd_local_.reset();
  write_fd_remote_.reset();
}

void FakeAres::set_local_dev(ares_channel channel, const char* local_dev_name) {
  CheckChannel(channel);
}

void FakeAres::getaddrinfo(ares_channel channel,
                           const char* name,
                           const char* service,
                           const struct ares_addrinfo_hints* hints,
                           ares_addrinfo_callback callback,
                           void* arg) {
  CheckChannel(channel);
  CHECK_EQ(getaddrinfo_params_, nullptr) << "Callback has been set";
  getaddrinfo_params_ = std::make_unique<GetaddrinfoParams>();
  getaddrinfo_params_->hint_family =
      FromSAFamily((sa_family_t)hints->ai_family);
  getaddrinfo_params_->callback = callback;
  getaddrinfo_params_->arg = arg;

  int fds[2];
  CHECK_EQ(pipe2(fds, 0), 0);
  read_fd_local_.reset(fds[0]);
  read_fd_remote_.reset(fds[1]);
  CHECK_EQ(pipe2(fds, 0), 0);
  write_fd_local_.reset(fds[1]);
  write_fd_remote_.reset(fds[0]);

  // Block the write fd at first so that the client won't get a ready event at
  // the beginning.
  BlockWriteFD();
}

void FakeAres::freeaddrinfo(struct ares_addrinfo* ai) {
  // Do nothing, because the ares_addrinfo instance is freed by
  // AresAddrinfoContext after callback.
}

struct timeval* FakeAres::timeout(ares_channel channel,
                                  struct timeval* maxtv,
                                  struct timeval* tv) {
  return maxtv;
}

int FakeAres::getsock(ares_channel channel,
                      ares_socket_t* socks,
                      int numsocks) {
  CheckChannel(channel);
  CHECK_GE(numsocks, 2);
  socks[0] = read_fd_local_.get();
  socks[1] = write_fd_local_.get();
  // (1 << 0): socket 0 is readable;
  // (1 << (ARES_GETSOCK_MAXNUM + 1)): socket 1 is writable;
  return (1 << 0) | (1 << (ARES_GETSOCK_MAXNUM + 1));
}

void FakeAres::process_fd(ares_channel channel,
                          ares_socket_t read_fd,
                          ares_socket_t write_fd) {
  CheckChannel(channel);
  if (read_fd != ARES_SOCKET_BAD) {
    CHECK_EQ(read_fd, read_fd_local_.get());
    VerifyReadFD(read_fd);
  }
  if (write_fd != ARES_SOCKET_BAD) {
    CHECK_EQ(write_fd, write_fd_local_.get());
    BlockWriteFD();
  }
  if (!callback_result_.has_value()) {
    return;
  }

  CHECK(getaddrinfo_params_);

  std::unique_ptr<AresAddrinfoContext> context = GenerateAresAddrinfoContext(
      callback_result_->addrs, getaddrinfo_params_->hint_family);
  getaddrinfo_params_->callback(getaddrinfo_params_->arg,
                                callback_result_->status,
                                /*timeouts=*/0, &context->info);
}

int FakeAres::set_servers_csv(ares_channel channel, const char* servers) {
  CheckChannel(channel);
  return ARES_SUCCESS;
}

// The string used to trigger and verify the read fd behavior. The content can
// be any.
constexpr std::string_view kFDContent = "0";

// Triggers the read ready event by sending some content on the pipe.
void FakeAres::TriggerReadReady() {
  CHECK(read_fd_remote_.is_valid()) << "Read fd is not ready";
  CHECK(base::WriteFileDescriptor(read_fd_remote_.get(), kFDContent));
}

void FakeAres::VerifyReadFD(int fd) {
  char buf[kFDContent.size() + 1];
  CHECK(base::ReadFromFD(fd, base::span(buf, kFDContent.size())))
      << "Failed to read from fd";
  CHECK_EQ(std::string_view(buf, kFDContent.size()), kFDContent);
}

// Note that 4096 is minimum buffer size of a pipe.
constexpr int kPipeBufferSize = 4096;

// Triggers the write ready event by consuming the content in the pipe so that
// it's no longer blocking.
void FakeAres::TriggerWriteReady() {
  CHECK(write_fd_remote_.is_valid()) << "Write fd is not ready";
  static char buf[kPipeBufferSize];
  CHECK(base::ReadFromFD(write_fd_remote_.get(), buf));
}

void FakeAres::BlockWriteFD() {
  CHECK_EQ(kPipeBufferSize,
           fcntl(write_fd_remote_.get(), F_SETPIPE_SZ, kPipeBufferSize));
  CHECK(base::WriteFileDescriptor(write_fd_local_.get(),
                                  std::string(kPipeBufferSize, 'a')));
}

void FakeAres::InvokeCallbackOnNextProcessFD(int status,
                                             std::vector<IPAddress> addrs) {
  callback_result_ = CallbackResult{status, std::move(addrs)};
}

class DNSClientTest : public testing::Test {
 protected:
  DNSClientTest() = default;
  ~DNSClientTest() = default;

  MOCK_METHOD(void, Callback, (const Result&), ());
  MOCK_METHOD(void, CallbackWithDuration, (base::TimeDelta, const Result&), ());
  DNSClient::Callback GetCallback() {
    return base::BindOnce(&DNSClientTest::Callback, base::Unretained(this));
  }
  DNSClient::CallbackWithDuration GetCallbackWithDuration() {
    return base::BindOnce(&DNSClientTest::CallbackWithDuration,
                          base::Unretained(this));
  }

  base::test::TaskEnvironment task_env_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO};
};

TEST_F(DNSClientTest, IPv4WriteReadAndReturnSuccess) {
  FakeAres fake_ares;
  const auto addrs = {IPAddress::CreateFromString("192.168.1.1").value(),
                      IPAddress::CreateFromString("192.168.1.2").value()};

  auto client =
      DNSClientFactory().Resolve(IPFamily::kIPv4, "test-url", GetCallback(),
                                 /*options=*/{}, &fake_ares);

  fake_ares.TriggerWriteReady();
  task_env_.RunUntilIdle();

  fake_ares.TriggerReadReady();
  fake_ares.InvokeCallbackOnNextProcessFD(ARES_SUCCESS, addrs);
  EXPECT_CALL(*this, Callback(Result(addrs)));
  task_env_.RunUntilIdle();
}

TEST_F(DNSClientTest, IPv6WriteReadAndReturnSuccess) {
  FakeAres fake_ares;
  const auto addrs = {IPAddress::CreateFromString("fd00::1").value(),
                      IPAddress::CreateFromString("fd00::2").value()};

  auto client =
      DNSClientFactory().Resolve(IPFamily::kIPv6, "test-url", GetCallback(),
                                 /*options=*/{}, &fake_ares);

  fake_ares.TriggerWriteReady();
  task_env_.RunUntilIdle();

  fake_ares.TriggerReadReady();
  fake_ares.InvokeCallbackOnNextProcessFD(ARES_SUCCESS, addrs);
  EXPECT_CALL(*this, Callback(Result(addrs)));
  task_env_.RunUntilIdle();
}

TEST_F(DNSClientTest, MultipleWriteReadAndReturnSuccess) {
  FakeAres fake_ares;
  const auto addrs = {IPAddress::CreateFromString("192.168.1.1").value(),
                      IPAddress::CreateFromString("192.168.1.2").value()};

  auto client =
      DNSClientFactory().Resolve(IPFamily::kIPv4, "test-url", GetCallback(),
                                 /*options=*/{}, &fake_ares);

  fake_ares.TriggerWriteReady();
  fake_ares.TriggerReadReady();
  task_env_.RunUntilIdle();

  fake_ares.TriggerWriteReady();
  fake_ares.TriggerReadReady();
  task_env_.RunUntilIdle();

  fake_ares.TriggerReadReady();
  fake_ares.InvokeCallbackOnNextProcessFD(ARES_SUCCESS, addrs);
  EXPECT_CALL(*this, Callback(Result(addrs)));
  task_env_.RunUntilIdle();
}

TEST_F(DNSClientTest, WriteReadAndReturnError) {
  FakeAres fake_ares;

  auto client =
      DNSClientFactory().Resolve(IPFamily::kIPv4, "test-url", GetCallback(),
                                 /*options=*/{}, &fake_ares);

  fake_ares.TriggerWriteReady();
  fake_ares.TriggerReadReady();
  fake_ares.InvokeCallbackOnNextProcessFD(ARES_ENODATA, {});
  EXPECT_CALL(*this, Callback(Result(base::unexpected(Error::kNoData))));
  task_env_.RunUntilIdle();
}

TEST_F(DNSClientTest, WriteAndTimeout) {
  FakeAres fake_ares;

  DNSClient::Options opts = {
      .timeout = base::Seconds(1),
  };
  auto client = DNSClientFactory().Resolve(IPFamily::kIPv4, "test-url",
                                           GetCallback(), opts, &fake_ares);

  fake_ares.TriggerWriteReady();
  task_env_.RunUntilIdle();

  EXPECT_CALL(*this, Callback(Result(base::unexpected(Error::kTimedOut))));
  task_env_.FastForwardBy(base::Seconds(2));
}

TEST_F(DNSClientTest, WriteAndDestroyObject) {
  FakeAres fake_ares;

  auto client =
      DNSClientFactory().Resolve(IPFamily::kIPv4, "test-url", GetCallback(),
                                 /*options=*/{}, &fake_ares);

  fake_ares.TriggerWriteReady();
  task_env_.RunUntilIdle();

  // No callback should be invoked in this case.
  EXPECT_CALL(*this, Callback).Times(0);
  client.reset();
  task_env_.FastForwardUntilNoTasksRemain();
}

// Only need to mock several functions.
class MockAres : public FakeAres {
 public:
  MockAres() {
    ON_CALL(*this, init_options)
        .WillByDefault([this](ares_channel* channelptr,
                              struct ares_options* options, int optmask) {
          return this->FakeAres::init_options(channelptr, options, optmask);
        });
    ON_CALL(*this, set_local_dev)
        .WillByDefault(
            [this](ares_channel channel, const char* local_dev_name) {
              return this->FakeAres::set_local_dev(channel, local_dev_name);
            });
    ON_CALL(*this, set_servers_csv)
        .WillByDefault([this](ares_channel channel, const char* servers) {
          return this->FakeAres::set_servers_csv(channel, servers);
        });
  }

  MOCK_METHOD(int,
              init_options,
              (ares_channel * channelptr,
               struct ares_options* options,
               int optmask),
              (override));
  MOCK_METHOD(void,
              set_local_dev,
              (ares_channel channel, const char* local_dev_name),
              (override));
  MOCK_METHOD(int,
              set_servers_csv,
              (ares_channel channel, const char* servers),
              (override));
};

TEST_F(DNSClientTest, ResolveWithOptions) {
  StrictMock<MockAres> mock_ares;

  DNSClient::Options test_opts = {
      .number_of_tries = 5,
      .per_query_initial_timeout = base::Seconds(10),
      .interface = "wlan0",
      .name_server = IPAddress::CreateFromString("1.2.3.4").value(),
  };

  EXPECT_CALL(mock_ares,
              init_options(_,
                           Pointee(AllOf(Field(&ares_options::tries, 5),
                                         Field(&ares_options::timeout, 10000))),
                           ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES));
  EXPECT_CALL(mock_ares, set_local_dev(_, StrEq("wlan0")));
  EXPECT_CALL(mock_ares, set_servers_csv(_, StrEq("1.2.3.4")));

  auto client = DNSClientFactory().Resolve(
      IPFamily::kIPv4, "test-url", GetCallback(), test_opts, &mock_ares);
}

TEST_F(DNSClientTest, ResolveWithoutOptions) {
  StrictMock<MockAres> mock_ares;

  EXPECT_CALL(mock_ares,
              init_options(_,
                           Pointee(AllOf(Field(&ares_options::tries, 0),
                                         Field(&ares_options::timeout, 0))),
                           /*opt_masks=*/0));

  auto client = DNSClientFactory().Resolve(
      IPFamily::kIPv4, "test-url", GetCallback(), /*options=*/{}, &mock_ares);
}

TEST_F(DNSClientTest, ReturnSuccessWithDuration) {
  FakeAres fake_ares;
  const auto addrs = {IPAddress::CreateFromString("192.168.1.1").value()};

  auto client = DNSClientFactory().Resolve(IPFamily::kIPv4, "test-url",
                                           GetCallbackWithDuration(),
                                           /*options=*/{}, &fake_ares);

  task_env_.FastForwardBy(base::Milliseconds(150));
  fake_ares.TriggerWriteReady();
  task_env_.RunUntilIdle();

  fake_ares.TriggerReadReady();
  fake_ares.InvokeCallbackOnNextProcessFD(ARES_SUCCESS, addrs);
  EXPECT_CALL(*this,
              CallbackWithDuration(Ge(base::Milliseconds(150)), Result(addrs)));
  task_env_.RunUntilIdle();
}

TEST_F(DNSClientTest, ReturnErrorWithDuration) {
  FakeAres fake_ares;

  auto client = DNSClientFactory().Resolve(IPFamily::kIPv4, "test-url",
                                           GetCallbackWithDuration(),
                                           /*options=*/{}, &fake_ares);

  task_env_.FastForwardBy(base::Milliseconds(200));
  fake_ares.TriggerWriteReady();
  fake_ares.TriggerReadReady();
  fake_ares.InvokeCallbackOnNextProcessFD(ARES_ENODATA, {});
  EXPECT_CALL(*this,
              CallbackWithDuration(Ge(base::Milliseconds(200)),
                                   Result(base::unexpected(Error::kNoData))));
  task_env_.RunUntilIdle();
}

TEST_F(DNSClientTest, TimeoutWithDuration) {
  FakeAres fake_ares;

  DNSClient::Options opts = {
      .timeout = base::Seconds(1),
  };
  auto client = DNSClientFactory().Resolve(
      IPFamily::kIPv4, "test-url", GetCallbackWithDuration(), opts, &fake_ares);

  fake_ares.TriggerWriteReady();
  task_env_.RunUntilIdle();

  EXPECT_CALL(*this,
              CallbackWithDuration(Ge(base::Seconds(1)),
                                   Result(base::unexpected(Error::kTimedOut))));
  task_env_.FastForwardBy(base::Seconds(2));
}

}  // namespace
}  // namespace net_base
