// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RESOURCED_VM_GRPC_INTERFACE_RESOURCED_CHROMIUM_GRPC_CLIENT_H_
#define RESOURCED_VM_GRPC_INTERFACE_RESOURCED_CHROMIUM_GRPC_CLIENT_H_

#define RESOURCED_CPU_UPDATE_INTERVAL_MS 100
#define RESOURCED_GRPC_CLIENT_PORT 5551
#define VMADDR_CID_HOST 2

#ifdef __cplusplus
extern "C" {
#endif
int32_t chromiumStartCpuPower(void);
int32_t chromiumWriteMaxCpuFreq(uint64_t freq);
int32_t chromiumStopCpuUpdates(void);
#ifdef __cplusplus
}
#endif

#endif  // RESOURCED_VM_GRPC_INTERFACE_RESOURCED_CHROMIUM_GRPC_CLIENT_H_
