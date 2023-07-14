// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwdrm-videoproc-ta/videoproc_ta.h"

#include <stdint.h>

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include "hwdrm-videoproc-ta/videoproc_ta_service.h"

#define PARSE_H264_SLICE_HEADER_CMD 1

TEE_Result TA_CreateEntryPoint(void) {
  return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
                                    TEE_Param params[TEE_NUM_PARAMS],
                                    void** sess_ctx) {
  return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void* sess_ctx) {}

TEE_Result TA_InvokeCommandEntryPoint(void* sess_ctx,
                                      uint32_t cmd_id,
                                      uint32_t param_types,
                                      TEE_Param params[TEE_NUM_PARAMS]) {
  switch (cmd_id) {
    case PARSE_H264_SLICE_HEADER_CMD:
      return ParseH264SliceHeader(param_types, params);
    default:
      return TEE_ERROR_BAD_PARAMETERS;
  }
}
