// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "proto_bindings/resourced_bridge.grpc.pb.h"
#include "resourced/vm_grpc/interface/resourced_chromium_grpc_client.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using resourced_bridge::v2::EmptyMessage;
using resourced_bridge::v2::RequestedCpuFrequency;
using resourced_bridge::v2::RequestedInterval;
using resourced_bridge::v2::ResourcedCommListener;
using resourced_bridge::v2::ReturnCode;

class ResourcedCommListenerClient {
 public:
  explicit ResourcedCommListenerClient(std::shared_ptr<Channel> channel)
      : stub_(ResourcedCommListener::NewStub(channel)) {}

  static std::string getResourcedGrpcClientAddr();
  int32_t startCpuUpdates(uint64_t intervalMs);
  int32_t stopCpuUpdates(void);
  int32_t setCpuFrequency(uint64_t freqVal);

 private:
  std::unique_ptr<ResourcedCommListener::Stub> stub_;
};

std::string ResourcedCommListenerClient::getResourcedGrpcClientAddr() {
  // Create vsock address string for gRPC client
  static const std::string resourcedGrpcClientAddr =
      std::string("vsock:") + std::to_string(VMADDR_CID_HOST) +
      std::string(":") + std::to_string(RESOURCED_GRPC_CLIENT_PORT);

  return resourcedGrpcClientAddr;
}

// Create object for grpc client class.
static ResourcedCommListenerClient resourcedGrpcClient(grpc::CreateChannel(
    ResourcedCommListenerClient::getResourcedGrpcClientAddr(),
    grpc::InsecureChannelCredentials()));

// Notify resourced to trigger CPU power data updates.
int32_t ResourcedCommListenerClient::startCpuUpdates(uint64_t intervalMs) {
  RequestedInterval requestedInterval;
  ReturnCode returnCode;
  ClientContext context;

  // supply frequency of CPU power update
  requestedInterval.set_interval_ms(intervalMs);

  Status status =
      stub_->StartCpuUpdates(&context, requestedInterval, &returnCode);

  if (!status.ok()) {
    return -1;
  }

  return 0;
}

// Notify resourced to stop sending CPU Power data.
int32_t ResourcedCommListenerClient::stopCpuUpdates(void) {
  EmptyMessage emptyMessage;
  ReturnCode returnCode;
  ClientContext context;

  Status status = stub_->StopCpuUpdates(&context, emptyMessage, &returnCode);

  if (!status.ok()) {
    return -1;
  }

  return 0;
}

// Notify resourced to set CPU limit.
int32_t ResourcedCommListenerClient::setCpuFrequency(uint64_t freqVal) {
  RequestedCpuFrequency requestedCpuFrequency;
  ReturnCode returnCode;
  ClientContext context;

  // supply CPU frequnecy value to set on the platform
  requestedCpuFrequency.set_freq_val(freqVal);

  Status status =
      stub_->SetCpuFrequency(&context, requestedCpuFrequency, &returnCode);

  if (!status.ok()) {
    return -1;
  }

  return 0;
}

// Start CPU power updates.
int32_t chromiumStartCpuPower(void) {
  if (resourcedGrpcClient.startCpuUpdates(RESOURCED_CPU_UPDATE_INTERVAL_MS)) {
    return -1;
  }

  return 0;
}

// Set CPU frequnecy on the chromium platform.
int32_t chromiumWriteMaxCpuFreq(uint64_t freq) {
  if (resourcedGrpcClient.setCpuFrequency(freq)) {
    return -1;
  }

  return 0;
}

// Stop CPU power updates.
int32_t chromiumStopCpuUpdates(void) {
  if (resourcedGrpcClient.stopCpuUpdates()) {
    return -1;
  }

  return 0;
}
