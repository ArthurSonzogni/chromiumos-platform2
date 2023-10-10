// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/dns_client.h"

#include <fcntl.h>
#include <netdb.h>

#include <string>
#include <string_view>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "net-base/ares_interface.h"

namespace net_base {
namespace {

using Error = DNSClient::Error;
using Result = DNSClient::Result;

class FakeAres : public AresInterface {
 public:
  ~FakeAres();

  int init_options(ares_channel* channelptr,
                   struct ares_options* options,
                   int optmask) override;

  void destroy(ares_channel channel) override;

  void gethostbyname(ares_channel channel,
                     const char* name,
                     int family,
                     ares_host_callback callback,
                     void* arg) override;

  struct timeval* timeout(ares_channel channel,
                          struct timeval* maxtv,
                          struct timeval* tv) override;

  int getsock(ares_channel channel,
              ares_socket_t* socks,
              int numsocks) override;

  void process_fd(ares_channel channel,
                  ares_socket_t read_fd,
                  ares_socket_t write_fd) override;

  // The client of FakeAres will get the event that socket is readable or
  // writable.
  void TriggerReadReady();
  void TriggerWriteReady();

  // The next process_fd() call will invoke the callback with the given
  // parameters.
  void InvokeCallbackOnNextProcessFD(int status, std::vector<IPAddress> addrs);

 private:
  struct GethostbynameParams {
    int family = 0;
    void* arg = nullptr;
    ares_host_callback callback = nullptr;
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

  std::unique_ptr<GethostbynameParams> gethostbyname_params_;

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
  *channelptr = CreateChannel();
  return ARES_SUCCESS;
}

void FakeAres::destroy(ares_channel channel) {
  CheckChannel(channel);
  if (gethostbyname_params_) {
    gethostbyname_params_->callback(gethostbyname_params_->arg,
                                    ARES_EDESTRUCTION,
                                    /*timeouts=*/0, /*hostent=*/nullptr);
  }

  channel_ = nullptr;
  gethostbyname_params_ = nullptr;
  read_fd_local_.reset();
  read_fd_remote_.reset();
  write_fd_local_.reset();
  write_fd_remote_.reset();
}

void FakeAres::gethostbyname(ares_channel channel,
                             const char* name,
                             int family,
                             ares_host_callback callback,
                             void* arg) {
  CheckChannel(channel);
  CHECK_EQ(gethostbyname_params_, nullptr) << "Callback has been set";
  gethostbyname_params_ = std::make_unique<GethostbynameParams>();
  gethostbyname_params_->family = family;
  gethostbyname_params_->callback = callback;
  gethostbyname_params_->arg = arg;

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

  CHECK(gethostbyname_params_);
  std::vector<std::vector<uint8_t>> addrs_in_bytes;
  std::vector<char*> ptrs_to_addrs_in_bytes;
  for (const auto& ip : callback_result_->addrs) {
    addrs_in_bytes.push_back(ip.ToBytes());
    ptrs_to_addrs_in_bytes.push_back(
        reinterpret_cast<char*>(addrs_in_bytes.back().data()));
  }
  ptrs_to_addrs_in_bytes.push_back(nullptr);

  struct hostent ent;

  // Not using these fields in the implementation now, just ignore them.
  ent.h_name = nullptr;
  ent.h_aliases = nullptr;

  ent.h_addrtype = gethostbyname_params_->family;
  ent.h_length = gethostbyname_params_->family == AF_INET ? 4 : 16;
  ent.h_addr_list = ptrs_to_addrs_in_bytes.data();

  gethostbyname_params_->callback(gethostbyname_params_->arg,
                                  callback_result_->status,
                                  /*timeouts=*/0, &ent);
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
  CHECK(base::ReadFromFD(fd, buf, kFDContent.size()))
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
  CHECK(base::ReadFromFD(write_fd_remote_.get(), buf, kPipeBufferSize));
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
  DNSClient::Callback GetCallback() {
    return base::BindOnce(&DNSClientTest::Callback, base::Unretained(this));
  }

  base::test::TaskEnvironment task_env_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO};
};

TEST_F(DNSClientTest, IPv4WriteReadAndReturnSuccess) {
  FakeAres fake_ares;
  const auto addrs = {IPAddress::CreateFromString("192.168.1.1").value(),
                      IPAddress::CreateFromString("192.168.1.2").value()};

  auto client = DNSClient::Resolve(IPFamily::kIPv4, "test-url", GetCallback(),
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

  auto client = DNSClient::Resolve(IPFamily::kIPv6, "test-url", GetCallback(),
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

  auto client = DNSClient::Resolve(IPFamily::kIPv4, "test-url", GetCallback(),
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

  auto client = DNSClient::Resolve(IPFamily::kIPv4, "test-url", GetCallback(),
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
  auto client = DNSClient::Resolve(IPFamily::kIPv4, "test-url", GetCallback(),
                                   opts, &fake_ares);

  fake_ares.TriggerWriteReady();
  task_env_.RunUntilIdle();

  EXPECT_CALL(*this, Callback(Result(base::unexpected(Error::kTimedOut))));
  task_env_.FastForwardBy(base::Seconds(2));
}

TEST_F(DNSClientTest, WriteAndDestroyObject) {
  FakeAres fake_ares;

  auto client = DNSClient::Resolve(IPFamily::kIPv4, "test-url", GetCallback(),
                                   /*options=*/{}, &fake_ares);

  fake_ares.TriggerWriteReady();
  task_env_.RunUntilIdle();

  // No callback should be invoked in this case.
  EXPECT_CALL(*this, Callback).Times(0);
  client.reset();
  task_env_.FastForwardUntilNoTasksRemain();
}

}  // namespace
}  // namespace net_base
