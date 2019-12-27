// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "tpm_manager/client/tpm_nvram_dbus_proxy.h"
#include "tpm_manager-client/tpm_manager/dbus-constants.h"

using testing::_;
using testing::Invoke;
using testing::StrictMock;
using testing::WithArgs;

namespace tpm_manager {

class TpmNvramDBusProxyTest : public testing::Test {
 public:
  ~TpmNvramDBusProxyTest() override = default;
  void SetUp() override {
    mock_object_proxy_ = new StrictMock<dbus::MockObjectProxy>(
        nullptr, "", dbus::ObjectPath(kTpmManagerServicePath));
    proxy_.set_object_proxy(mock_object_proxy_.get());
  }

 protected:
  scoped_refptr<StrictMock<dbus::MockObjectProxy>> mock_object_proxy_;
  TpmNvramDBusProxy proxy_;
};

TEST_F(TpmNvramDBusProxyTest, DefineSpace) {
  uint32_t nvram_index = 5;
  size_t nvram_size = 32;
  auto fake_dbus_call = [nvram_index, nvram_size](
      dbus::MethodCall* method_call,
      dbus::MockObjectProxy::ResponseCallback
      MIGRATE_WrapObjectProxyCallback(response_callback)) {
    // Verify request protobuf.
    dbus::MessageReader reader(method_call);
    DefineSpaceRequest request;
    EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&request));
    EXPECT_TRUE(request.has_index());
    EXPECT_EQ(nvram_index, request.index());
    EXPECT_TRUE(request.has_size());
    EXPECT_EQ(nvram_size, request.size());
    // Create reply protobuf.
    auto response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    DefineSpaceReply reply;
    reply.set_result(NVRAM_RESULT_SUCCESS);
    writer.AppendProtoAsArrayOfBytes(reply);
    std::move(MIGRATE_WrapObjectProxyCallback(response_callback))
        .Run(response.get());
  };
  EXPECT_CALL(*mock_object_proxy_,
              MIGRATE_CallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(WithArgs<0, 2>(Invoke(fake_dbus_call)));
  // Set expectations on the outputs.
  int callback_count = 0;
  auto callback = [](int* count, const DefineSpaceReply& reply) {
    (*count)++;
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  DefineSpaceRequest request;
  request.set_index(nvram_index);
  request.set_size(nvram_size);
  proxy_.DefineSpace(request, base::Bind(callback, &callback_count));
  EXPECT_EQ(1, callback_count);
}

TEST_F(TpmNvramDBusProxyTest, DestroySpaceRequest) {
  uint32_t nvram_index = 5;
  auto fake_dbus_call = [nvram_index](
      dbus::MethodCall* method_call,
      dbus::MockObjectProxy::ResponseCallback
      MIGRATE_WrapObjectProxyCallback(response_callback)) {
    // Verify request protobuf.
    dbus::MessageReader reader(method_call);
    DestroySpaceRequest request;
    EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&request));
    EXPECT_TRUE(request.has_index());
    EXPECT_EQ(nvram_index, request.index());
    // Create reply protobuf.
    auto response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    DestroySpaceReply reply;
    reply.set_result(NVRAM_RESULT_SUCCESS);
    writer.AppendProtoAsArrayOfBytes(reply);
    std::move(MIGRATE_WrapObjectProxyCallback(response_callback))
        .Run(response.get());
  };
  EXPECT_CALL(*mock_object_proxy_,
              MIGRATE_CallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(WithArgs<0, 2>(Invoke(fake_dbus_call)));
  // Set expectations on the outputs.
  int callback_count = 0;
  auto callback = [](int* count, const DestroySpaceReply& reply) {
    (*count)++;
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  DestroySpaceRequest request;
  request.set_index(nvram_index);
  proxy_.DestroySpace(request, base::Bind(callback, &callback_count));
  EXPECT_EQ(1, callback_count);
}
TEST_F(TpmNvramDBusProxyTest, WriteSpace) {
  uint32_t nvram_index = 5;
  std::string nvram_data("nvram_data");
  auto fake_dbus_call = [nvram_index, nvram_data](
      dbus::MethodCall* method_call,
      dbus::MockObjectProxy::ResponseCallback
      MIGRATE_WrapObjectProxyCallback(response_callback)) {
    // Verify request protobuf.
    dbus::MessageReader reader(method_call);
    WriteSpaceRequest request;
    EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&request));
    EXPECT_TRUE(request.has_index());
    EXPECT_EQ(nvram_index, request.index());
    EXPECT_TRUE(request.has_data());
    EXPECT_EQ(nvram_data, request.data());
    // Create reply protobuf.
    auto response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    WriteSpaceReply reply;
    reply.set_result(NVRAM_RESULT_SUCCESS);
    writer.AppendProtoAsArrayOfBytes(reply);
    std::move(MIGRATE_WrapObjectProxyCallback(response_callback))
        .Run(response.get());
  };
  EXPECT_CALL(*mock_object_proxy_,
              MIGRATE_CallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(WithArgs<0, 2>(Invoke(fake_dbus_call)));
  // Set expectations on the outputs.
  int callback_count = 0;
  auto callback = [](int* count, const WriteSpaceReply& reply) {
    (*count)++;
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  WriteSpaceRequest request;
  request.set_index(nvram_index);
  request.set_data(nvram_data);
  proxy_.WriteSpace(request, base::Bind(callback, &callback_count));
  EXPECT_EQ(1, callback_count);
}

TEST_F(TpmNvramDBusProxyTest, ReadSpace) {
  uint32_t nvram_index = 5;
  std::string nvram_data("nvram_data");
  auto fake_dbus_call = [nvram_index, nvram_data](
      dbus::MethodCall* method_call,
      dbus::MockObjectProxy::ResponseCallback
      MIGRATE_WrapObjectProxyCallback(response_callback)) {
    // Verify request protobuf.
    dbus::MessageReader reader(method_call);
    ReadSpaceRequest request;
    EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&request));
    EXPECT_TRUE(request.has_index());
    EXPECT_EQ(nvram_index, request.index());
    // Create reply protobuf.
    auto response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    ReadSpaceReply reply;
    reply.set_result(NVRAM_RESULT_SUCCESS);
    reply.set_data(nvram_data);
    writer.AppendProtoAsArrayOfBytes(reply);
    std::move(MIGRATE_WrapObjectProxyCallback(response_callback))
        .Run(response.get());
  };
  EXPECT_CALL(*mock_object_proxy_,
              MIGRATE_CallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(WithArgs<0, 2>(Invoke(fake_dbus_call)));
  // Set expectations on the outputs.
  int callback_count = 0;
  auto callback = [](int* count, const std::string& data,
                     const ReadSpaceReply& reply) {
    (*count)++;
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
    EXPECT_TRUE(reply.has_data());
    EXPECT_EQ(data, reply.data());
  };
  ReadSpaceRequest request;
  request.set_index(nvram_index);
  proxy_.ReadSpace(request, base::Bind(callback, &callback_count, nvram_data));
  EXPECT_EQ(1, callback_count);
}

TEST_F(TpmNvramDBusProxyTest, LockSpace) {
  uint32_t nvram_index = 5;
  auto fake_dbus_call = [nvram_index](
      dbus::MethodCall* method_call,
      dbus::MockObjectProxy::ResponseCallback
      MIGRATE_WrapObjectProxyCallback(response_callback)) {
    // Verify request protobuf.
    dbus::MessageReader reader(method_call);
    LockSpaceRequest request;
    EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&request));
    EXPECT_TRUE(request.has_index());
    EXPECT_EQ(nvram_index, request.index());
    // Create reply protobuf.
    auto response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    LockSpaceReply reply;
    reply.set_result(NVRAM_RESULT_SUCCESS);
    writer.AppendProtoAsArrayOfBytes(reply);
    std::move(MIGRATE_WrapObjectProxyCallback(response_callback))
        .Run(response.get());
  };
  EXPECT_CALL(*mock_object_proxy_,
              MIGRATE_CallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(WithArgs<0, 2>(Invoke(fake_dbus_call)));
  // Set expectations on the outputs.
  int callback_count = 0;
  auto callback = [](int* callback_count, const LockSpaceReply& reply) {
    (*callback_count)++;
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
  };
  LockSpaceRequest request;
  request.set_index(nvram_index);
  proxy_.LockSpace(request, base::Bind(callback, &callback_count));
  EXPECT_EQ(1, callback_count);
}

TEST_F(TpmNvramDBusProxyTest, ListSpaces) {
  constexpr uint32_t nvram_index_list[] = {3, 4, 5};
  auto fake_dbus_call = [nvram_index_list](
      dbus::MethodCall* method_call,
      dbus::MockObjectProxy::ResponseCallback
      MIGRATE_WrapObjectProxyCallback(response_callback)) {
    // Verify request protobuf.
    dbus::MessageReader reader(method_call);
    ListSpacesRequest request;
    EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&request));
    // Create reply protobuf.
    auto response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    ListSpacesReply reply;
    reply.set_result(NVRAM_RESULT_SUCCESS);
    for (auto index : nvram_index_list) {
      reply.add_index_list(index);
    }
    writer.AppendProtoAsArrayOfBytes(reply);
    std::move(MIGRATE_WrapObjectProxyCallback(response_callback))
        .Run(response.get());
  };
  EXPECT_CALL(*mock_object_proxy_,
              MIGRATE_CallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(WithArgs<0, 2>(Invoke(fake_dbus_call)));
  // Set expectations on the outputs.
  int callback_count = 0;
  auto callback = [](int* count, const std::vector<const uint32_t>& index_list,
                     const ListSpacesReply& reply) {
    (*count)++;
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
    EXPECT_EQ(index_list.size(), reply.index_list_size());
    for (size_t i = 0; i < 3; i++) {
      EXPECT_EQ(index_list[i], reply.index_list(i));
    }
  };
  ListSpacesRequest request;
  proxy_.ListSpaces(request, base::Bind(callback, &callback_count,
                                        std::vector<const uint32_t>(
                                            std::begin(nvram_index_list),
                                            std::end(nvram_index_list))));
  EXPECT_EQ(1, callback_count);
}

TEST_F(TpmNvramDBusProxyTest, GetSpaceInfo) {
  uint32_t nvram_index = 5;
  size_t nvram_size = 32;
  auto fake_dbus_call = [nvram_index, nvram_size](
      dbus::MethodCall* method_call,
      dbus::MockObjectProxy::ResponseCallback
      MIGRATE_WrapObjectProxyCallback(response_callback)) {
    // Verify request protobuf.
    dbus::MessageReader reader(method_call);
    GetSpaceInfoRequest request;
    EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&request));
    EXPECT_TRUE(request.has_index());
    EXPECT_EQ(nvram_index, request.index());
    // Create reply protobuf.
    auto response = dbus::Response::CreateEmpty();
    dbus::MessageWriter writer(response.get());
    GetSpaceInfoReply reply;
    reply.set_result(NVRAM_RESULT_SUCCESS);
    reply.set_size(nvram_size);
    writer.AppendProtoAsArrayOfBytes(reply);
    std::move(MIGRATE_WrapObjectProxyCallback(response_callback))
        .Run(response.get());
  };
  EXPECT_CALL(*mock_object_proxy_,
              MIGRATE_CallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(WithArgs<0, 2>(Invoke(fake_dbus_call)));
  // Set expectations on the outputs.
  int callback_count = 0;
  auto callback = [](int* count, size_t size, const GetSpaceInfoReply& reply) {
    (*count)++;
    EXPECT_EQ(NVRAM_RESULT_SUCCESS, reply.result());
    EXPECT_TRUE(reply.has_size());
    EXPECT_EQ(size, reply.size());
  };
  GetSpaceInfoRequest request;
  request.set_index(nvram_index);
  proxy_.GetSpaceInfo(request,
                      base::Bind(callback, &callback_count, nvram_size));
  EXPECT_EQ(1, callback_count);
}

}  // namespace tpm_manager
