// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RESOURCED_VM_GRPC_INTERFACE_RESOURCED_CHROMIUM_GRPC_SERVER_H_
#define RESOURCED_VM_GRPC_INTERFACE_RESOURCED_CHROMIUM_GRPC_SERVER_H_

#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>

#define RESOURCED_GRPC_SERVER_PORT 5553
#define RESOURCED_GRPC_SERVER_SHUTDOWN_TIMEOUT_SEC 5
#define VMADDR_CID_ANY -1U

// Max time for which CPU power data is valid
#define CPU_POWER_MAX_VALID_TIME_SEC 2

typedef struct {
  int64_t cpuPowerData;
  struct timeval timeStamp;
} cpuPowerDataSample_t;

#ifdef __cplusplus
extern "C" {
#endif
int32_t initChromiumInterface(void);
int32_t shutdownChromiumInterface(void);
double chromiumGetCpuPower(void);
uint64_t chromiumReadCpuCurrFreq(void);
uint64_t chromiumReadCpuMaxFreq(void);
uint64_t chromiumReadCpuBaseFreq(void);
#ifdef __cplusplus
}
#endif

#endif  // RESOURCED_VM_GRPC_INTERFACE_RESOURCED_CHROMIUM_GRPC_SERVER_H_
