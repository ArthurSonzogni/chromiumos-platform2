// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ANX7625_TA_ANX7625_TA_SERVICE_H_
#define ANX7625_TA_ANX7625_TA_SERVICE_H_

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

TEE_Result RegBlockRead(uint32_t param_types, TEE_Param params[TEE_NUM_PARAMS]);

TEE_Result RegBlockWrite(uint32_t param_types,
                         TEE_Param params[TEE_NUM_PARAMS]);

TEE_Result SetPowerStatus(uint32_t param_types,
                          TEE_Param params[TEE_NUM_PARAMS]);

TEE_Result GetPowerStatus(uint32_t param_types,
                          TEE_Param params[TEE_NUM_PARAMS]);

#endif  // ANX7625_TA_ANX7625_TA_SERVICE_H_
