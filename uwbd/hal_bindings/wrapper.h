// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Used by Bindgen to generate FFI bindings to the NXP HAL library.

#ifndef UWBD_HAL_BINDINGS_WRAPPER_H_
#define UWBD_HAL_BINDINGS_WRAPPER_H_

// Needed for the uint_* type definitions
#include <stdint.h>

// The interface to the UWB HAL is exposed using the definitions in the
// following header files
#include <hal_nxpuwb.h>
#include <phNxpUciHal_Adaptation.h>

#endif  // UWBD_HAL_BINDINGS_WRAPPER_H_
