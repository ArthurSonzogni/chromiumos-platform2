// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <stdint.h>
#include <grpcpp/grpcpp.h>
#include <pthread.h>
#include "proto_bindings/resourced_bridge.grpc.pb.h"
#include "resourced/vm_grpc/interface/resourced_chromium_grpc_client.h"
#include "resourced/vm_grpc/interface/resourced_chromium_grpc_server.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using resourced_bridge::v2::CpuInfoData;
using resourced_bridge::v2::CpuRaplPowerData;
using resourced_bridge::v2::EmptyMessage;
using resourced_bridge::v2::ResourcedComm;

static pthread_t grpcServerThread = 0;
static std::unique_ptr<Server> gRPCServer;
static cpuPowerDataSample_t curCpuPowerDataSample, prevCpuPowerDataSample;

// Inbound updates from resourced
class ResourcedCommServiceImpl final : public ResourcedComm::Service {
 public:
  Status VmInitData(ServerContext* context,
                    const CpuInfoData* cpuInfoData,
                    EmptyMessage* emptyMessage) override;
  Status CpuPowerUpdate(ServerContext* context,
                        const CpuRaplPowerData* cpuRaplPowerData,
                        EmptyMessage* emptyMessage) override;

  uint64_t m_cpuCurrFrequency = 0;
  uint64_t m_cpuMaxFrequency = 0;
  uint64_t m_cpuBaseFrequency = 0;
};

// Create object for grpc server class.
static ResourcedCommServiceImpl resourcedGrpcServer;

// Get init data from resourced
Status ResourcedCommServiceImpl::VmInitData(ServerContext* context,
                                            const CpuInfoData* cpuInfoData,
                                            EmptyMessage* emptyMessage) {
  if ((cpuInfoData) && (cpuInfoData->cpu_core_data_size() > 0)) {
    m_cpuCurrFrequency = cpuInfoData->cpu_core_data(0).cpu_freq_curr_khz();
    m_cpuMaxFrequency = cpuInfoData->cpu_core_data(0).cpu_freq_max_khz();
    m_cpuBaseFrequency = cpuInfoData->cpu_core_data(0).cpu_freq_base_khz();
  }

  //
  // Resourced sends Vminit signal when either resourced or
  // nvidia-powerd is started/re-started.
  // Therefore, send CPU update signal to resourced when
  // Vminit signal is received
  //
  if (chromiumStartCpuPower()) {
    return Status(grpc::UNKNOWN, "Unknown error");
  }

  return Status::OK;
}

// Receive CPU power data updates.
Status ResourcedCommServiceImpl::CpuPowerUpdate(
    ServerContext* context,
    const CpuRaplPowerData* cpuRaplPowerData,
    EmptyMessage* emptyMessage) {
  if (cpuRaplPowerData->cpu_energy() > 0) {
    struct timespec tv;

    if (clock_gettime(CLOCK_MONOTONIC, &tv) == 0) {
      prevCpuPowerDataSample = curCpuPowerDataSample;
      memset(&curCpuPowerDataSample, 0, sizeof(curCpuPowerDataSample));
      curCpuPowerDataSample.cpuPowerData = cpuRaplPowerData->cpu_energy();
      curCpuPowerDataSample.timeStamp.tv_sec = tv.tv_sec;
      curCpuPowerDataSample.timeStamp.tv_usec = tv.tv_nsec / 1000;
    }

    return Status::OK;
  } else {
    return Status(grpc::OUT_OF_RANGE, "Out of Range");
  }
}

void* startPowerdGrpcServer(void* arg) {
  // Create vsock address string for gRPC server
  static const std::string resourcedGrpcServerAddr =
      std::string("vsock:") + std::to_string(VMADDR_CID_ANY) +
      std::string(":") + std::to_string(RESOURCED_GRPC_SERVER_PORT);
  std::string serverAddress(resourcedGrpcServerAddr);

  ServerBuilder builder;
  builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
  builder.RegisterService(&resourcedGrpcServer);

  gRPCServer = builder.BuildAndStart();
  gRPCServer->Wait();

  return NULL;
}

int32_t startGrpcServer(void) {
  if (pthread_create(&grpcServerThread, NULL, &startPowerdGrpcServer, NULL)) {
    grpcServerThread = 0;
    return -1;
  }

  return 0;
}

int32_t initChromiumInterface(void) {
  return startGrpcServer();
}

int32_t shutdownChromiumInterface(void) {
  if (gRPCServer != NULL) {
    gpr_timespec rv;
    rv.tv_sec = 1;
    rv.tv_nsec = 0;
    rv.clock_type = GPR_CLOCK_MONOTONIC;
    gRPCServer->Shutdown(rv);
  }

  if (grpcServerThread) {
    struct timespec timeout;

    if (clock_gettime(CLOCK_REALTIME, &timeout) == -1) {
      return -1;
    }

    // Timeout after 5 second.
    timeout.tv_sec += RESOURCED_GRPC_SERVER_SHUTDOWN_TIMEOUT_SEC;
    if (pthread_timedjoin_np(grpcServerThread, NULL, &timeout) != 0) {
      return -1;
    }
  }

  return 0;
}

uint64_t chromiumReadCpuCurrFreq(void) {
  return resourcedGrpcServer.m_cpuCurrFrequency;
}

uint64_t chromiumReadCpuMaxFreq(void) {
  return resourcedGrpcServer.m_cpuMaxFrequency;
}

uint64_t chromiumReadCpuBaseFreq(void) {
  return resourcedGrpcServer.m_cpuBaseFrequency;
}

static double chromiumUpdateCpuPower(void) {
  struct timespec tv;
  struct timeval timeStamp;
  struct timeval diffTimeStamp;
  double intervalUs, cpuW;

  if ((curCpuPowerDataSample.cpuPowerData <=
       prevCpuPowerDataSample.cpuPowerData) ||
      clock_gettime(CLOCK_MONOTONIC, &tv)) {
    return 0.0;
  }

  timeStamp.tv_sec = tv.tv_sec;
  timeStamp.tv_usec = tv.tv_nsec / 1000;
  timersub(&timeStamp, &curCpuPowerDataSample.timeStamp, &diffTimeStamp);

  // Check if CPU power data is not stale.
  if (diffTimeStamp.tv_sec > CPU_POWER_MAX_VALID_TIME_SEC) {
    return 0.0;
  }

  //
  // cpuPowerData is in microjoule therefore use time interval in microsecond
  // to get CPU power in Watt unit.
  //
  timersub(&curCpuPowerDataSample.timeStamp, &prevCpuPowerDataSample.timeStamp,
           &diffTimeStamp);
  intervalUs = diffTimeStamp.tv_sec * 1000000.0 + diffTimeStamp.tv_usec;
  cpuW = static_cast<double>(curCpuPowerDataSample.cpuPowerData -
                             prevCpuPowerDataSample.cpuPowerData) /
         intervalUs;

  return cpuW;
}

double chromiumGetCpuPower(void) {
  return chromiumUpdateCpuPower();
}
