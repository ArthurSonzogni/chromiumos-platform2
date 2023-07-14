// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The name of this file must not be modified

#ifndef HWDRM_VIDEOPROC_TA_USER_TA_HEADER_DEFINES_H_
#define HWDRM_VIDEOPROC_TA_USER_TA_HEADER_DEFINES_H_

#include "hwdrm-videoproc-ta/videoproc_ta.h"

#define TA_UUID VIDEOPROC_TA_UUID

#define TA_FLAGS TA_FLAG_CACHE_MAINTENANCE

// Provisioned stack size
#define TA_STACK_SIZE (2 * 1024)

// Provisioned heap size for TEE_Malloc() and friends
#define TA_DATA_SIZE (32 * 1024)

// The gpd.ta.version property
#define TA_VERSION "1.0"

// The gpd.ta.description property
#define TA_DESCRIPTION "HWDRM Video Processing Trusted Application"

#endif  // HWDRM_VIDEOPROC_TA_USER_TA_HEADER_DEFINES_H_
