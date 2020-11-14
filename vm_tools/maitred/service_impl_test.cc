// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <netinet/in.h>

#include <base/macros.h>
#include <google/protobuf/text_format.h>
#include <gtest/gtest.h>

#include <string>

#include "vm_tools/maitred/service_impl.h"

namespace vm_tools {
namespace maitred {
namespace {

constexpr char kValidAddress[] = "100.115.92.26";
constexpr char kValidNetmask[] = "255.255.255.252";
constexpr char kValidGateway[] = "100.115.92.25";
constexpr char kInvalidConfig[] = R"(ipv4_config {
address: 0
netmask: 0
gateway: 0
})";

class ServiceTest : public ::testing::Test {
 public:
  ServiceTest();
  ServiceTest(const ServiceTest&) = delete;
  ServiceTest& operator=(const ServiceTest&) = delete;

  ~ServiceTest() override = default;

 protected:
  ServiceImpl service_impl_;
};

ServiceTest::ServiceTest() : service_impl_(nullptr) {}

}  // namespace

// Tests that ConfigureNetwork will reject invalid input.
TEST_F(ServiceTest, ConfigureNetwork_InvalidInput) {
  grpc::ServerContext ctx;

  vm_tools::NetworkConfigRequest request;
  ASSERT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kInvalidConfig, &request));

  vm_tools::EmptyMessage response;

  grpc::Status invalid(grpc::INVALID_ARGUMENT, "invalid argument");

  // None of the fields are set.
  grpc::Status result =
      service_impl_.ConfigureNetwork(&ctx, &request, &response);
  EXPECT_EQ(invalid.error_code(), result.error_code());

  // Only one field is valid.
  struct in_addr in;
  ASSERT_GT(inet_pton(AF_INET, kValidNetmask, &in), 0);
  request.mutable_ipv4_config()->set_netmask(in.s_addr);
  result = service_impl_.ConfigureNetwork(&ctx, &request, &response);
  EXPECT_EQ(invalid.error_code(), result.error_code());

  // Two fields are set.
  ASSERT_GT(inet_pton(AF_INET, kValidAddress, &in), 0);
  request.mutable_ipv4_config()->set_address(in.s_addr);
  result = service_impl_.ConfigureNetwork(&ctx, &request, &response);
  EXPECT_EQ(invalid.error_code(), result.error_code());

  // Two different fields are set.
  request.mutable_ipv4_config()->set_address(0);
  ASSERT_GT(inet_pton(AF_INET, kValidGateway, &in), 0);
  request.mutable_ipv4_config()->set_gateway(in.s_addr);
  result = service_impl_.ConfigureNetwork(&ctx, &request, &response);
  EXPECT_EQ(invalid.error_code(), result.error_code());
}

TEST_F(ServiceTest, SetTime_Zero) {
  grpc::ServerContext ctx;
  vm_tools::SetTimeRequest request;
  google::protobuf::Timestamp* time = request.mutable_time();
  // Clearly-invalid (near-epoch) past time.
  time->set_seconds(0x0);
  time->set_nanos(0x0deadbee);

  grpc::Status result = service_impl_.SetTime(&ctx, &request,
                                              /*response=*/nullptr);

  EXPECT_EQ(result.error_code(), grpc::INVALID_ARGUMENT);
}

TEST_F(ServiceTest, SetTime_GoodTime) {
  struct timeval current;
  ASSERT_EQ(gettimeofday(&current, nullptr), 0);

  grpc::ServerContext ctx;
  vm_tools::SetTimeRequest request;
  google::protobuf::Timestamp* time = request.mutable_time();

  time->set_seconds(current.tv_sec + 20);
  time->set_nanos(current.tv_usec * 1000);

  grpc::Status result = service_impl_.SetTime(&ctx, &request,
                                              /*response=*/nullptr);

  // Can't set time in gtest env.
  EXPECT_EQ(result.error_code(), grpc::INTERNAL);
}

TEST_F(ServiceTest, GetKernelVersion) {
  grpc::ServerContext ctx;
  vm_tools::EmptyMessage empty;
  vm_tools::GetKernelVersionResponse grpc_response;
  grpc::Status result =
      service_impl_.GetKernelVersion(&ctx, &empty, &grpc_response);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(std::string(), grpc_response.kernel_release());
  EXPECT_NE(std::string(), grpc_response.kernel_version());
}

}  // namespace maitred
}  // namespace vm_tools
