// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "anx7625-ta/anx7625_ta.h"

#include <stdint.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include "anx7625-ta/anx7625_ta_service.h"

#define ANX7625_REG_BLOCK_READ_CMD 1
#define ANX7625_REG_BLOCK_WRITE_CMD 2
#define ANX7625_SET_POWER_STATUS_CMD 3
#define ANX7625_GET_POWER_STATUS_CMD 4

TEE_Result TA_CreateEntryPoint(void) {
  return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
                                    TEE_Param params[TEE_NUM_PARAMS],
                                    void** sess_ctx) {
  (void)param_types;
  (void)params;
  (void)sess_ctx;
  return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void* sess_ctx) {
  (void)sess_ctx;
}

TEE_Result TA_InvokeCommandEntryPoint(void* sess_ctx,
                                      uint32_t cmd_id,
                                      uint32_t param_types,
                                      TEE_Param params[TEE_NUM_PARAMS]) {
  (void)sess_ctx;
  switch (cmd_id) {
    case ANX7625_REG_BLOCK_READ_CMD:
      return RegBlockRead(param_types, params);
    case ANX7625_REG_BLOCK_WRITE_CMD:
      return RegBlockWrite(param_types, params);
    case ANX7625_SET_POWER_STATUS_CMD:
      return SetPowerStatus(param_types, params);
    case ANX7625_GET_POWER_STATUS_CMD:
      return GetPowerStatus(param_types, params);
    default:
      return TEE_ERROR_BAD_PARAMETERS;
  }
}
