// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_FORWARD_DECLARE_H_
#define ODML_ON_DEVICE_MODEL_ML_FORWARD_DECLARE_H_

extern "C" {

struct DawnProcTable;
struct WGPUAdapterImpl;
struct WGPUDeviceImpl;
struct WGPUTextureImpl;
struct WGPUAdapterProperties;
typedef struct WGPUAdapterImpl* WGPUAdapter;
typedef struct WGPUDeviceImpl* WGPUDevice;
typedef struct WGPUTextureImpl* WGPUTexture;
}

#endif  // ODML_ON_DEVICE_MODEL_ML_FORWARD_DECLARE_H_
