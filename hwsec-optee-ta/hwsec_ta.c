// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-optee-ta/hwsec_ta.h"

#include <stdint.h>

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include "hwsec-optee-ta/hwsec_ta_service.h"
#include "hwsec-optee-ta/hwsec_session.h"

#define SELF_TEST_CMD 0
#define READ_COUNTER_CMD 1
#define INCREASE_COUNTER_CMD 2

TEE_Result TA_CreateEntryPoint(void) {
  return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {}

// Note: The session may become invalidated state after a suspend & resume.
TEE_Result TA_OpenSessionEntryPoint(uint32_t __maybe_unused param_types,
                                    TEE_Param __maybe_unused
                                        params[TEE_NUM_PARAMS],
                                    void** sess_ctx) {
  TEE_Result res = TEE_ERROR_GENERIC;

  TpmSession* session = TEE_Malloc(sizeof(TpmSession), 0);
  if (!session)
    return TEE_ERROR_OUT_OF_MEMORY;

  *sess_ctx = session;

  res = OpenHwsecSession(session);
  if (res != TEE_SUCCESS) {
    EMSG("OpenHwsecSession failed with code 0x%x", res);
    TA_CloseSessionEntryPoint(*sess_ctx);
    return res;
  }

  return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void* sess_ctx) {
  TpmSession* session = sess_ctx;

  if (CloseHwsecSession(session) != TEE_SUCCESS) {
    EMSG("CloseHwsecSession failed");
  }

  TEE_Free(session);
}

TEE_Result TA_InvokeCommandEntryPoint(void* sess_ctx,
                                      uint32_t cmd_id,
                                      uint32_t param_types,
                                      TEE_Param params[TEE_NUM_PARAMS]) {
  switch (cmd_id) {
    case SELF_TEST_CMD:
      return HwsecSelfTest(param_types, params);
    case READ_COUNTER_CMD:
      return HwsecReadCounter(sess_ctx, param_types, params);
    case INCREASE_COUNTER_CMD:
      return HwsecIncreaseCounter(sess_ctx, param_types, params);
    default:
      return TEE_ERROR_BAD_PARAMETERS;
  }
}
