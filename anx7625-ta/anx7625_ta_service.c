// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "anx7625-ta/anx7625_ta_service.h"

#include <stdbool.h>
#include <stdint.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <util.h>

// TODO: Fix the MTK I2C TA UUID
#define PTA_MTK_I2C_UUID \
  {0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}

// TODO: Fix the MTK I2C TA command offsets
#define TZCMD_TA_MTK_I2C_FIRST_CMD 0x1000
#define TZCMD_TA_MTK_I2C_READ (TZCMD_TA_MTK_I2C_FIRST_CMD + 0)
#define TZCMD_TA_MTK_I2C_WRITE (TZCMD_TA_MTK_I2C_FIRST_CMD + 1)

// Because of the TA_FLAGS, there will be one instance of the ANX7625 TA, which
// does not get torn down until OPTEE reboots or is powered down. The
// `is_powered_on_` state variable holds the data for whether or not a session
// has powered on the device.
static bool is_powered_on_ = false;

// RegBlockRead: I2C read interface
// Parameters:
// - VALUE_INPUT: addresses
//   - u32 a: slave address (lower 8 bits set)
//   - u32 b: register address (lower 8 bits set)
// - MEMREF_INOUT: target
//   - u8* buffer: where to write to
//   - u32 size: number of bytes to read
TEE_Result RegBlockRead(uint32_t param_types,
                        TEE_Param params[TEE_NUM_PARAMS]) {
  uint32_t ptypes =
      TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_MEMREF_INOUT,
                      TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);

  if (param_types != ptypes) {
    EMSG("RegBlockRead failed with unsupported param types");
    return TEE_ERROR_NOT_SUPPORTED;
  }

  if (!is_powered_on_) {
    return TEE_ERROR_TARGET_DEAD;
  }

  // Enforces that only the lower 8 bits are set for the slave address.
  params[0].value.a &= 0xFF;
  // Enforces that only the lower 8 bits are set for the register address.
  params[0].value.b &= 0xFF;

  // TODO: See if we should only certain register reads.

  // Assumes the MTK I2C TA read command parameters match the parameters of
  // RegBlockRead().
  // TODO: Change the parameters to RegBlockRead() if they don't match, or
  // create the MTK I2C TA parameters from `params`.
  const TEE_UUID uuid = PTA_MTK_I2C_UUID;
  TEE_TASessionHandle sess = TEE_HANDLE_NULL;
  TEE_Result res =
      TEE_OpenTASession(&uuid, TEE_TIMEOUT_INFINITE, 0, NULL, &sess, NULL);
  if (res) {
    EMSG("Failure opening MTK I2C PTA of %d", res);
    return res;
  }

  res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE, TZCMD_TA_MTK_I2C_READ,
                            param_types, params, NULL);

  if (res) {
    EMSG("Failure of %d while reading from register", res);
  }

  TEE_CloseTASession(sess);
  return res;
}

// RegBlockWrite: I2C write interface
// Parameters:
// - VALUE_INPUT: addresses
//   - u32 a: slave address (lower 8 bits set)
//   - u32 b: register address (lower 8 bits set)
// - MEMREF_INPUT: value
//   - u8* buffer: data to write
//   - u32 size: number of bytes to write
TEE_Result RegBlockWrite(uint32_t param_types,
                         TEE_Param params[TEE_NUM_PARAMS]) {
  uint32_t ptypes =
      TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_MEMREF_INPUT,
                      TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);

  if (param_types != ptypes) {
    EMSG("RegBlockWrite failed with unsupported param types");
    return TEE_ERROR_NOT_SUPPORTED;
  }

  if (!is_powered_on_) {
    return TEE_ERROR_TARGET_DEAD;
  }

  // Enforces that only the lower 8 bits are set for the slave address.
  params[0].value.a &= 0xFF;
  // Enforces that only the lower 8 bits are set for the register address.
  params[0].value.b &= 0xFF;

  // TODO: Allow only certain register writes to ensure the HDCP status is not
  // settable from the kernel.
  if (params[0].value.a != 0x10) {
    EMSG("RegBlockWrite failed due unsupported slave address.");
    return TEE_ERROR_SECURITY;
  }

  // Uses the switch statement to define an allow-list of register addresses.
  switch (params[0].value.b) {
    case 0x01:
      // fallthrough
    case 0x02:
      // fallthrough
    case 0x03:
      // Supported register address.
      break;
    default:
      EMSG("RegBlockWrite failed due unsupported write register address.");
      return TEE_ERROR_SECURITY;
  }

  // Assumes the MTK I2C TA write command parameters match the parameters of
  // RegBlockWrite().
  // TODO: Change the parameters to RegBlockRead() if they don't match, or
  // create the MTK I2C TA parameters from `params`.
  const TEE_UUID uuid = PTA_MTK_I2C_UUID;
  TEE_TASessionHandle sess = TEE_HANDLE_NULL;
  TEE_Result res =
      TEE_OpenTASession(&uuid, TEE_TIMEOUT_INFINITE, 0, NULL, &sess, NULL);
  if (res) {
    EMSG("Failure opening MTK I2C PTA of %d", res);
    return res;
  }

  res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE, TZCMD_TA_MTK_I2C_READ,
                            param_types, params, NULL);

  if (res) {
    EMSG("Failure of %d while writing to register", res);
  }

  TEE_CloseTASession(sess);
  return res;
}

// SetPowerStatus: Sets anx7625's power status to OPTEE, if anx7625 is powered
// off, then GetPowerStatus() should return false and register reading and
// writing will fail.
// Parameters:
// - VALUE_INPUT: addresses
//   - u32 a: 1 if powered on. 0 if powered off.
//   - u32 b: not set.
TEE_Result SetPowerStatus(uint32_t param_types,
                          TEE_Param params[TEE_NUM_PARAMS]) {
  uint32_t ptypes =
      TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_NONE,
                      TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);

  if (param_types != ptypes) {
    EMSG("SetPowerStatus failed with unsupported param types");
    return TEE_ERROR_NOT_SUPPORTED;
  }

  if (params[0].value.a > 1) {
    return TEE_ERROR_BAD_PARAMETERS;
  }

  const bool new_is_powered_on = !!params[0].value.a;
  if (new_is_powered_on && is_powered_on_) {
    // Unexpected power on request w
    EMSG("SetPowerStatus failed to turn on an already powered-on ANX7625");
    return TEE_ERROR_GENERIC;
  }

  is_powered_on_ = new_is_powered_on;

  // TODO: Figure out how to avoid state abuse. We don't want the userspace to
  // turn the status off and on in a way that abuses the HDCP polling in
  // WTPI_CURRENT_HDCP_STATUS.
  // For example, is there anything that we should do when turning on or off the
  // ANX7625?
  // if (is_powered_on_) {
  //  ...
  //  } else {
  //  ...
  //  }
  // Alternately, we could use the secure clock to rate limit this function.
  return TEE_SUCCESS;
}

// GetPowerStatus: Gets anx7625's power status.
// Parameters:
// - VALUE_OUTPUT: addresses
//   - u32 a: 1 if powered on. 0 if powered off.
//   - u32 b: not set.
TEE_Result GetPowerStatus(uint32_t param_types,
                          TEE_Param params[TEE_NUM_PARAMS]) {
  uint32_t ptypes =
      TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT, TEE_PARAM_TYPE_NONE,
                      TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);

  if (param_types != ptypes) {
    EMSG("GetPowerStatus failed with unsupported param types");
    return TEE_ERROR_NOT_SUPPORTED;
  }

  params[0].value.a = is_powered_on_ ? 1 : 0;
  return TEE_SUCCESS;
}
