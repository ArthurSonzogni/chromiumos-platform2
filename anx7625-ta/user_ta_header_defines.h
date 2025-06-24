// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The name of this file must not be modified

#ifndef ANX7625_TA_USER_TA_HEADER_DEFINES_H_
#define ANX7625_TA_USER_TA_HEADER_DEFINES_H_

#include "anx7625-ta/anx7625_ta.h"

#define TA_UUID ANX7625_TA_UUID

#define TA_FLAGS                                     \
  (TA_FLAG_SINGLE_INSTANCE | TA_FLAG_MULTI_SESSION | \
   TA_FLAG_INSTANCE_KEEP_ALIVE)

// Provisioned stack size
#define TA_STACK_SIZE (2 * 1024)

// Provisioned heap size for TEE_Malloc() and friends
#define TA_DATA_SIZE (32 * 1024)

// The gpd.ta.version property
#define TA_VERSION "1.0"

// The gpd.ta.description property
#define TA_DESCRIPTION "ANX7625 controller Trusted Application"

#endif  // ANX7625_TA_USER_TA_HEADER_DEFINES_H_
