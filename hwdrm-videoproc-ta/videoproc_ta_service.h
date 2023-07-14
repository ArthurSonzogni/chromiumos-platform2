// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWDRM_VIDEOPROC_TA_VIDEOPROC_TA_SERVICE_H_
#define HWDRM_VIDEOPROC_TA_VIDEOPROC_TA_SERVICE_H_

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

TEE_Result ParseH264SliceHeader(uint32_t param_types,
                                TEE_Param params[TEE_NUM_PARAMS]);

#endif  // HWDRM_VIDEOPROC_TA_VIDEOPROC_TA_SERVICE_H_
