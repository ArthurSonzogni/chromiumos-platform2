// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include <brillo/http/http_transport_curl.h>

#include <base/at_exit.h>
#include <base/functional/bind.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/single_thread_task_runner.h>
#include <brillo/http/http_connection_curl.h>
#include <brillo/http/http_request.h>
#include <brillo/http/mock_curl_api.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using testing::WithoutArgs;

namespace brillo {
namespace http {
namespace curl {

class HttpCurlTransportTest : public testing::Test {
 public:
  void SetUp() override {
    curl_api_ = std::make_shared<MockCurlInterface>();
    transport_ = std::make_shared<Transport>(curl_api_);
    handle_ = reinterpret_cast<CURL*>(100);  // Mock handle value.
    EXPECT_CALL(*curl_api_, EasyInit()).WillOnce(Return(handle_));
    EXPECT_CALL(*curl_api_, EasySetOptPtr(handle_, CURLOPT_CAINFO, _))
        .WillOnce(Return(CURLE_OK));
    EXPECT_CALL(*curl_api_, EasySetOptStr(handle_, CURLOPT_CAPATH, _))
        .WillOnce(Return(CURLE_OK));
    EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_SSL_VERIFYPEER, 1))
        .WillOnce(Return(CURLE_OK));
    EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_SSL_VERIFYHOST, 2))
        .WillOnce(Return(CURLE_OK));
    EXPECT_CALL(*curl_api_, EasySetOptPtr(handle_, CURLOPT_PRIVATE, _))
        .WillRepeatedly(Return(CURLE_OK));
  }

  void TearDown() override {
    transport_.reset();
    curl_api_.reset();
  }

 protected:
  std::shared_ptr<MockCurlInterface> curl_api_;
  std::shared_ptr<Transport> transport_;
  CURL* handle_{nullptr};
};

TEST_F(HttpCurlTransportTest, RequestGet) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_USERAGENT, "User Agent"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_REFERER, "http://foo.bar/baz"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  auto connection =
      transport_->CreateConnection("http://foo.bar/get", request_type::kGet, {},
                                   "User Agent", "http://foo.bar/baz", nullptr);
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, RequestGetWithProxy) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_USERAGENT, "User Agent"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_REFERER, "http://foo.bar/baz"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_PROXY, "http://proxy.server"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  std::shared_ptr<Transport> proxy_transport =
      std::make_shared<Transport>(curl_api_, "http://proxy.server");

  auto connection = proxy_transport->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "User Agent",
      "http://foo.bar/baz", nullptr);
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, RequestHead) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/head"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_NOBODY, 1))
      .WillOnce(Return(CURLE_OK));
  auto connection = transport_->CreateConnection(
      "http://foo.bar/head", request_type::kHead, {}, "", "", nullptr);
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, RequestPut) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/put"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_UPLOAD, 1))
      .WillOnce(Return(CURLE_OK));
  auto connection = transport_->CreateConnection(
      "http://foo.bar/put", request_type::kPut, {}, "", "", nullptr);
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, RequestPost) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://www.foo.bar/post"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_POST, 1))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptPtr(handle_, CURLOPT_POSTFIELDS, nullptr))
      .WillOnce(Return(CURLE_OK));
  auto connection = transport_->CreateConnection(
      "http://www.foo.bar/post", request_type::kPost, {}, "", "", nullptr);
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, RequestPatch) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://www.foo.bar/patch"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_POST, 1))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptPtr(handle_, CURLOPT_POSTFIELDS, nullptr))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptStr(handle_, CURLOPT_CUSTOMREQUEST,
                                        request_type::kPatch))
      .WillOnce(Return(CURLE_OK));
  auto connection = transport_->CreateConnection(
      "http://www.foo.bar/patch", request_type::kPatch, {}, "", "", nullptr);
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, CurlFailure) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OUT_OF_MEMORY));
  EXPECT_CALL(*curl_api_, EasyStrError(CURLE_OUT_OF_MEMORY))
      .WillOnce(Return("Out of Memory"));
  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  ErrorPtr error;
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", &error);

  EXPECT_EQ(nullptr, connection.get());
  EXPECT_EQ("curl_easy_error", error->GetDomain());
  EXPECT_EQ(std::to_string(CURLE_OUT_OF_MEMORY), error->GetCode());
  EXPECT_EQ("Out of Memory", error->GetMessage());
}

class HttpCurlTransportAsyncTest : public testing::Test {
 public:
  void SetUp() override {
    curl_api_ = std::make_shared<MockCurlInterface>();
    transport_ = std::make_shared<Transport>(curl_api_);
    EXPECT_CALL(*curl_api_, EasyInit()).WillOnce(Return(handle_));
    EXPECT_CALL(*curl_api_, EasySetOptPtr(handle_, CURLOPT_CAINFO, _))
        .WillOnce(Return(CURLE_OK));
    EXPECT_CALL(*curl_api_, EasySetOptStr(handle_, CURLOPT_CAPATH, _))
        .WillOnce(Return(CURLE_OK));
    EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_SSL_VERIFYPEER, 1))
        .WillOnce(Return(CURLE_OK));
    EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_SSL_VERIFYHOST, 2))
        .WillOnce(Return(CURLE_OK));
    EXPECT_CALL(*curl_api_, EasySetOptPtr(handle_, CURLOPT_PRIVATE, _))
        .WillOnce(Return(CURLE_OK));
  }

 protected:
  std::shared_ptr<MockCurlInterface> curl_api_;
  std::shared_ptr<Transport> transport_;
  CURL* handle_{reinterpret_cast<CURL*>(123)};          // Mock handle value.
  CURLM* multi_handle_{reinterpret_cast<CURLM*>(456)};  // Mock handle value.
  curl_socket_t dummy_socket_{789};
};

TEST_F(HttpCurlTransportAsyncTest, StartAsyncTransfer) {
  // This test is a bit tricky because it deals with asynchronous I/O which
  // relies on a message loop to run all the async tasks.
  // For this, create a temporary I/O message loop and run it ourselves for the
  // duration of the test.
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::RunLoop run_loop;

  // Initial expectations for creating a CURL connection.
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);
  ASSERT_NE(nullptr, connection.get());

  // Success/error callback needed to report the result of an async operation.
  int success_call_count = 0;
  auto success_callback = [](int* success_call_count,
                             base::OnceClosure quit_closure,
                             RequestID /* request_id */,
                             std::unique_ptr<http::Response> /* resp */) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, std::move(quit_closure));
    (*success_call_count)++;
  };

  auto error_callback = [](RequestID /* request_id */,
                           const Error* /* error */) {
    FAIL() << "This callback shouldn't have been called";
  };

  EXPECT_CALL(*curl_api_, MultiInit()).WillOnce(Return(multi_handle_));
  EXPECT_CALL(*curl_api_, EasyGetInfoInt(handle_, CURLINFO_RESPONSE_CODE, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(200), Return(CURLE_OK)));

  curl_socket_callback socket_callback = nullptr;
  EXPECT_CALL(*curl_api_,
              MultiSetSocketCallback(multi_handle_, _, transport_.get()))
      .WillOnce(DoAll(SaveArg<1>(&socket_callback), Return(CURLM_OK)));

  curl_multi_timer_callback timer_callback = nullptr;
  EXPECT_CALL(*curl_api_,
              MultiSetTimerCallback(multi_handle_, _, transport_.get()))
      .WillOnce(DoAll(SaveArg<1>(&timer_callback), Return(CURLM_OK)));

  EXPECT_CALL(*curl_api_, MultiAddHandle(multi_handle_, handle_))
      .WillOnce(Return(CURLM_OK));

  EXPECT_EQ(1, transport_->StartAsyncTransfer(
                   connection.get(),
                   base::BindOnce(success_callback, &success_call_count,
                                  run_loop.QuitClosure()),
                   base::BindOnce(error_callback)));
  EXPECT_EQ(0, success_call_count);

  timer_callback(multi_handle_, 1, transport_.get());

  auto do_socket_action = [&socket_callback, this] {
    EXPECT_CALL(*curl_api_, MultiAssign(multi_handle_, dummy_socket_, _))
        .Times(2)
        .WillRepeatedly(Return(CURLM_OK));
    EXPECT_EQ(0, socket_callback(handle_, dummy_socket_, CURL_POLL_REMOVE,
                                 transport_.get(), nullptr));
  };

  EXPECT_CALL(*curl_api_,
              MultiSocketAction(multi_handle_, CURL_SOCKET_TIMEOUT, 0, _))
      .WillOnce(DoAll(SetArgPointee<3>(1),
                      WithoutArgs(Invoke(do_socket_action)), Return(CURLM_OK)))
      .WillRepeatedly(DoAll(SetArgPointee<3>(0), Return(CURLM_OK)));

  CURLMsg msg = {};
  msg.msg = CURLMSG_DONE;
  msg.easy_handle = handle_;
  msg.data.result = CURLE_OK;

  EXPECT_CALL(*curl_api_, MultiInfoRead(multi_handle_, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), Return(&msg)))
      .WillRepeatedly(DoAll(SetArgPointee<1>(0), Return(nullptr)));
  EXPECT_CALL(*curl_api_, EasyGetInfoPtr(handle_, CURLINFO_PRIVATE, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(connection.get()), Return(CURLE_OK)));

  EXPECT_CALL(*curl_api_, MultiRemoveHandle(multi_handle_, handle_))
      .WillOnce(Return(CURLM_OK));

  // Just in case something goes wrong and |success_callback| isn't called,
  // post a time-out quit closure to abort the message loop after 1 second.
  task_executor.task_runner()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), base::Seconds(1));
  run_loop.Run();
  EXPECT_EQ(1, success_call_count);

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();

  EXPECT_CALL(*curl_api_, MultiCleanup(multi_handle_))
      .WillOnce(Return(CURLM_OK));
  transport_.reset();
}

TEST_F(HttpCurlTransportTest, RequestGetTimeout) {
  transport_->SetDefaultTimeout(base::Milliseconds(2000));
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_TIMEOUT_MS, 2000))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, RequestGetResolveHost) {
  transport_->ResolveHostToIp("foo.bar", 80, "127.0.0.1");
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptPtr(handle_, CURLOPT_RESOLVE, _))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, RequestGetBufferSize) {
  transport_->SetBufferSize(512 * 1024);
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_,
              EasySetOptInt(handle_, CURLOPT_BUFFERSIZE, 512 * 1024))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, RequestGetBufferSizeDefault) {
  transport_->SetBufferSize(std::nullopt);
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_BUFFERSIZE, _))
      .Times(0);
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());
  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, RequestGetUploadBufferSize) {
  transport_->SetUploadBufferSize(2 * 1024 * 1024);
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_UPLOAD_BUFFERSIZE,
                                        2 * 1024 * 1024))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, RequestGetUploadBufferSizeDefault) {
  transport_->SetUploadBufferSize(std::nullopt);
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_UPLOAD_BUFFERSIZE, _))
      .Times(0);
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, SetDnsServers) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_DNS_SERVERS, "1.2.3.4,3.4.5.6"))
      .WillOnce(Return(CURLE_OK));

  transport_->SetDnsServers({"1.2.3.4", "3.4.5.6"});
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, SetDnsInterface) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptStr(handle_, CURLOPT_DNS_INTERFACE, "eth0"))
      .WillOnce(Return(CURLE_OK));

  transport_->SetDnsInterface("eth0");
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, SetDnsLocalIPv4Address) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_DNS_LOCAL_IP4, "192.168.0.14"))
      .WillOnce(Return(CURLE_OK));

  transport_->SetDnsLocalIPv4Address("192.168.0.14");
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, SetDnsLocalIPv6Address) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptStr(handle_, CURLOPT_DNS_LOCAL_IP6,
                                        "fe80::a9ff:fe46:b619"))
      .WillOnce(Return(CURLE_OK));

  transport_->SetDnsLocalIPv6Address("fe80::a9ff:fe46:b619");
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, SetInterface) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptStr(handle_, CURLOPT_INTERFACE, "if!eth0"))
      .WillOnce(Return(CURLE_OK));

  transport_->SetInterface("eth0");
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, SetLocalIpAddress) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_INTERFACE, "host!192.168.1.13"))
      .WillOnce(Return(CURLE_OK));

  transport_->SetLocalIpAddress("192.168.1.13");
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, SetSockOptCallback) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptPtr(handle_, CURLOPT_SOCKOPTDATA, _))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_,
              EasySetOptCallback(handle_, CURLOPT_SOCKOPTFUNCTION, _))
      .WillOnce(Return(CURLE_OK));

  transport_->SetSockOptCallback(base::BindRepeating([](int) { return true; }));
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

TEST_F(HttpCurlTransportTest, SetInterfaceAndLocalIpAddress) {
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_URL, "http://foo.bar/get"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptInt(handle_, CURLOPT_HTTPGET, 1))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_, EasySetOptStr(handle_, CURLOPT_INTERFACE, "if!wlan0"))
      .WillOnce(Return(CURLE_OK));
  EXPECT_CALL(*curl_api_,
              EasySetOptStr(handle_, CURLOPT_INTERFACE, "host!192.168.1.13"))
      .Times(0);

  transport_->SetInterface("wlan0");
  transport_->SetLocalIpAddress("192.168.1.13");
  auto connection = transport_->CreateConnection(
      "http://foo.bar/get", request_type::kGet, {}, "", "", nullptr);

  testing::Mock::VerifyAndClearExpectations(curl_api_.get());
  EXPECT_NE(nullptr, connection.get());

  EXPECT_CALL(*curl_api_, EasyCleanup(handle_)).Times(1);
  connection.reset();
}

}  // namespace curl
}  // namespace http
}  // namespace brillo
