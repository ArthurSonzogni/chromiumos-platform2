// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/grpc_async_adapter/async_grpc_client.h"

#include "diagnostics/grpc_async_adapter/async_grpc_constants.h"

namespace diagnostics {
namespace internal {

AsyncGrpcClientBase::AsyncGrpcClientBase(
    scoped_refptr<base::SequencedTaskRunner> task_runner)
    : dispatcher_(&completion_queue_, task_runner) {
  dispatcher_.Start();
}

AsyncGrpcClientBase::~AsyncGrpcClientBase() = default;

void AsyncGrpcClientBase::ShutDown(const base::Closure& on_shutdown_callback) {
  dispatcher_.Shutdown(on_shutdown_callback);
}

// static
std::shared_ptr<grpc::Channel> AsyncGrpcClientBase::CreateGrpcChannel(
    const std::string& target_uri) {
  grpc::ChannelArguments arguments;
  arguments.SetMaxSendMessageSize(kMaxGrpcMessageSize);
  arguments.SetMaxReceiveMessageSize(kMaxGrpcMessageSize);
  return grpc::CreateCustomChannel(
      target_uri, grpc::InsecureChannelCredentials(), arguments);
}

}  // namespace internal
}  // namespace diagnostics
