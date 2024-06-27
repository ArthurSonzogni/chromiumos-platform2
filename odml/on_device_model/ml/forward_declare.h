// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_FORWARD_DECLARE_H_
#define ODML_ON_DEVICE_MODEL_ML_FORWARD_DECLARE_H_

extern "C" {

struct DawnProcTable;
struct GpuConfig;
struct WGPUAdapterImpl;
typedef struct WGPUAdapterImpl* WGPUAdapter;
}

#endif  // ODML_ON_DEVICE_MODEL_ML_FORWARD_DECLARE_H_
