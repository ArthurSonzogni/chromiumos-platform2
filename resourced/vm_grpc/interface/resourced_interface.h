// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RESOURCED_VM_GRPC_INTERFACE_RESOURCED_INTERFACE_H_
#define RESOURCED_VM_GRPC_INTERFACE_RESOURCED_INTERFACE_H_
#include <stdint.h>

int32_t resourcedInterfaceOpen(void);
int32_t resourcedInterfaceClose(void);
int32_t resourcedInterfaceCpuUpdateStart(void);
int32_t resourcedInterfaceCpuUpdateStop(void);
uint64_t resourcedInterfaceGetCpuPower(void);
uint64_t resourcedInterfaceReadCpuCurrFreq(uint32_t cpu);
uint64_t resourcedInterfaceReadCpuMaxFreq(uint32_t cpu);
uint64_t resourcedInterfaceReadCpuBaseFreq(uint32_t cpu);
int32_t resourcedInterfaceWriteMaxCpuFreq(uint32_t cpu, uint64_t freq);

#endif  // RESOURCED_VM_GRPC_INTERFACE_RESOURCED_INTERFACE_H_
