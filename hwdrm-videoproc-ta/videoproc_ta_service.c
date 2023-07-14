// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwdrm-videoproc-ta/videoproc_ta_service.h"

#include <stdint.h>
#include <string.h>

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <util.h>

#include "hwdrm-videoproc-ta/h264_parser.h"

#define PTA_MEM_UUID                                 \
  {                                                  \
    0x4477588a, 0x8476, 0x11e2, {                    \
      0xad, 0x15, 0xe4, 0x1f, 0x13, 0x90, 0xd6, 0x76 \
    }                                                \
  }
#define TZCMD_TA_MEM_FIRST_CMD 0x1000
#define TZCMD_TA_MEM_MAP (TZCMD_TA_MEM_FIRST_CMD + 3)
#define FLAG_TA_MAP_CACHED (1 << 2)

TEE_Result ParseH264SliceHeader(uint32_t param_types,
                                TEE_Param params[TEE_NUM_PARAMS]) {
  uint32_t ptypes = TEE_PARAM_TYPES(
      TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_VALUE_INPUT,
      TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_MEMREF_OUTPUT);

  if (param_types != ptypes) {
    EMSG("ParseH264SliceHeader failed with unsupported param types");
    return TEE_ERROR_NOT_SUPPORTED;
  }

  // Now we need to map the input handle to read from it.
  TEE_Result res = TEE_ERROR_GENERIC;
  const TEE_UUID uuid = PTA_MEM_UUID;
  TEE_TASessionHandle sess = TEE_HANDLE_NULL;
  TEE_Param map_params[TEE_NUM_PARAMS];
  uint32_t map_param_types =
      TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_VALUE_INPUT,
                      TEE_PARAM_TYPE_VALUE_INOUT, TEE_PARAM_TYPE_VALUE_INOUT);
  res = TEE_OpenTASession(&uuid, TEE_TIMEOUT_INFINITE, 0, NULL, &sess, NULL);
  if (res) {
    EMSG("Failure opening mem PTA of %d", res);
    return res;
  }

  // Input buffer secure buffer handle.
  map_params[0].value.a = params[0].value.a;
  map_params[0].value.b = FLAG_TA_MAP_CACHED;
  map_params[1].value.a = map_params[1].value.b = 0;  // offset
  map_params[2].value.a = map_params[2].value.b = 0;  // va
  map_params[3].value.a = map_params[3].value.b = 0;  // size

  res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE, TZCMD_TA_MEM_MAP,
                            map_param_types, map_params, NULL);

  if (res) {
    TEE_CloseTASession(sess);
    EMSG("Failure querying PTA mem of %d", res);
    return res;
  }

  void* in_addr =
      (void*)reg_pair_to_64(map_params[2].value.a, map_params[2].value.b);
  size_t in_size = reg_pair_to_64(map_params[3].value.a, map_params[3].value.b);

  // Verify the offset.
  if (params[1].value.a >= in_size) {
    res = TEE_ERROR_BAD_PARAMETERS;
    goto out;
  }

  // Verify the size of the input/output structs.
  if (params[2].memref.size != sizeof(struct StreamDataForSliceHeader) ||
      params[3].memref.size != sizeof(struct H264SliceHeaderData)) {
    res = TEE_ERROR_BAD_PARAMETERS;
    goto out;
  }

  struct H264SliceHeaderData slice_hdr;
  struct StreamDataForSliceHeader* stream_data = params[2].memref.buffer;
  if (!ParseSliceHeader(((uint8_t*)in_addr) + params[1].value.a,
                        in_size - params[1].value.a, stream_data, &slice_hdr)) {
    res = TEE_ERROR_BAD_FORMAT;
    goto out;
  }

  // Copy the slice header to the memref so it gets sent back.
  memcpy(params[3].memref.buffer, &slice_hdr, sizeof(slice_hdr));
  params[3].memref.size = sizeof(slice_hdr);
  res = TEE_SUCCESS;

out:
  TEE_CloseTASession(sess);
  tee_unmap(in_addr, in_size);
  return res;
}
