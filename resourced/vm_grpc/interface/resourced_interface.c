// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "resourced/vm_grpc/interface/resourced_interface.h"
#include "resourced/vm_grpc/interface/resourced_chromium_grpc_server.h"
#include "resourced/vm_grpc/interface/resourced_chromium_grpc_client.h"

#define RESOURCED_EXPORT_SYMBOL __attribute__((visibility("default")))

/*
 * Open the resourced interface.
 * Return 0 on success or -1 on error.
 */
RESOURCED_EXPORT_SYMBOL
int32_t resourcedInterfaceOpen(void) {
  return initChromiumInterface();
}

/*
 * Close the resourced interface.
 * Return 0 on success or -1 on error.
 */
RESOURCED_EXPORT_SYMBOL
int32_t resourcedInterfaceClose(void) {
  return shutdownChromiumInterface();
}

/*
 * Return the current CPU power in MiliWatt.
 * Return 0.0 if it fails to get CPU power.
 */
RESOURCED_EXPORT_SYMBOL
uint64_t resourcedInterfaceGetCpuPower(void) {
  return (uint64_t)(chromiumGetCpuPower() * 1000);
}

/*
 * Return the current CPU frequency in kHz.
 * Return 0 if it fails to get default CPU frequency.
 */
RESOURCED_EXPORT_SYMBOL
uint64_t resourcedInterfaceReadCpuCurrFreq(uint32_t cpu) {
  return chromiumReadCpuCurrFreq();
}

/*
 * Return the max CPU frequency in kHz.
 * Return 0 if it fails to get default CPU frequency.
 */
RESOURCED_EXPORT_SYMBOL
uint64_t resourcedInterfaceReadCpuMaxFreq(uint32_t cpu) {
  return chromiumReadCpuMaxFreq();
}

/*
 * Return the base CPU frequency in kHz.
 * Return 0 if it fails to get default CPU frequency.
 */
RESOURCED_EXPORT_SYMBOL
uint64_t resourcedInterfaceReadCpuBaseFreq(uint32_t cpu) {
  return chromiumReadCpuBaseFreq();
}

/*
 * Takes the max CPU frequency in kHz and set the same.
 * Return 0 if it fails to set Max CPU frequency.
 */
RESOURCED_EXPORT_SYMBOL
int32_t resourcedInterfaceWriteMaxCpuFreq(uint32_t cpu, uint64_t freq) {
  return chromiumWriteMaxCpuFreq(freq);
}

/*
 * Start the periodic CPU power update from the resourced.
 * Return 0 on success or -1 on error.
 */
RESOURCED_EXPORT_SYMBOL
int32_t resourcedInterfaceCpuUpdateStart(void) {
  return chromiumStartCpuPower();
}

/*
 * Stop the periodic CPU power update from the resourced.
 * Return 0 on success or -1 on error.
 */
RESOURCED_EXPORT_SYMBOL
int32_t resourcedInterfaceCpuUpdateStop(void) {
  return chromiumStopCpuUpdates();
}
