// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwdrm-videoproc-ta/videoproc_ta_service.h"

#include <stdint.h>
#include <string.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <util.h>

#include "h264_parser.h"
#include "hwdrm-videoproc-ta/h264_parser.h"

#define PTA_MEM_UUID \
  {0x4477588a, 0x8476, 0x11e2, {0xad, 0x15, 0xe4, 0x1f, 0x13, 0x90, 0xd6, 0x76}}
#define TZCMD_TA_MEM_FIRST_CMD 0x1000
#define TZCMD_TA_MEM_MAP (TZCMD_TA_MEM_FIRST_CMD + 3)
#define FLAG_TA_MAP_CACHED (1 << 2)

// TODO(hiroh): Remove these functions once Chromium side is upreved.
static void FillStreamDataFromDeprecated(
    struct StreamDataForSliceHeader* out,
    const struct StreamDataForSliceHeaderDeprecated* in) {
#define IN2OUT(x)   \
  do {              \
    out->x = in->x; \
  } while (0)

  memset(out, 0, sizeof(struct StreamDataForSliceHeader));
  out->version = 0;
  IN2OUT(log2_max_frame_num_minus4);
  IN2OUT(log2_max_pic_order_cnt_lsb_minus4);
  IN2OUT(pic_order_cnt_type);
  IN2OUT(num_ref_idx_l0_default_active_minus1);
  IN2OUT(num_ref_idx_l1_default_active_minus1);
  IN2OUT(weighted_bipred_idc);
  IN2OUT(chroma_array_type);
  IN2OUT(frame_mbs_only_flag);
  IN2OUT(bottom_field_pic_order_in_frame_present_flag);
  IN2OUT(delta_pic_order_always_zero_flag);
  IN2OUT(redundant_pic_cnt_present_flag);
  IN2OUT(weighted_pred_flag);
#undef IN2OUT
}

static void FillSliceHeaderDeprecated(struct H264SliceHeaderDataDeprecated* out,
                                      const struct H264SliceHeaderData* in) {
#define IN2OUT(x)   \
  do {              \
    out->x = in->x; \
  } while (0)

  memset(out, 0, sizeof(struct H264SliceHeaderDataDeprecated));

  IN2OUT(nal_ref_idc);
  IN2OUT(idr_pic_flag);
  IN2OUT(slice_type);
  IN2OUT(field_pic_flag);
  IN2OUT(frame_num);
  IN2OUT(idr_pic_id);
  IN2OUT(pic_order_cnt_lsb);
  IN2OUT(delta_pic_order_cnt_bottom);
  IN2OUT(delta_pic_order_cnt0);
  IN2OUT(delta_pic_order_cnt1);
  out->ref_pic_fields.bits.no_output_of_prior_pics_flag =
      in->no_output_of_prior_pics_flag;
  out->ref_pic_fields.bits.long_term_reference_flag =
      in->long_term_reference_flag;
  out->ref_pic_fields.bits.adaptive_ref_pic_marking_mode_flag =
      in->adaptive_ref_pic_marking_mode_flag;
  out->ref_pic_fields.bits.dec_ref_pic_marking_count =
      in->dec_ref_pic_marking_size;

  for (size_t i = 0; i < in->dec_ref_pic_marking_size; ++i) {
    out->memory_management_control_operation[i] =
        in->dec_ref_pic_marking[i].memory_management_control_operation;
    out->difference_of_pic_nums_minus1[i] =
        in->dec_ref_pic_marking[i].difference_of_pic_nums_minus1;
    out->long_term_pic_num[i] = in->dec_ref_pic_marking[i].long_term_pic_num;
    out->max_long_term_frame_idx_plus1[i] =
        in->dec_ref_pic_marking[i].max_long_term_frame_idx_plus1;
    out->long_term_frame_idx[i] =
        in->dec_ref_pic_marking[i].long_term_frame_idx;
  }

  IN2OUT(dec_ref_pic_marking_bit_size);
  IN2OUT(pic_order_cnt_bit_size);
#undef IN2OUT
}

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
    EMSG("Too large offset: offset=%zu, buffer size=%zu",
         (size_t)params[1].value.a, in_size);
    res = TEE_ERROR_BAD_PARAMETERS;
    goto out;
  }
  // Verify the size of the input/output structs.
  if (params[2].memref.size !=
          sizeof(struct StreamDataForSliceHeaderDeprecated) &&
      params[2].memref.size != sizeof(struct StreamDataForSliceHeader)) {
    res = TEE_ERROR_BAD_PARAMETERS;
    goto out;
  }
  if (params[3].memref.size != sizeof(struct H264SliceHeaderDataDeprecated) &&
      params[3].memref.size != sizeof(struct H264SliceHeaderData)) {
    res = TEE_ERROR_BAD_PARAMETERS;
    goto out;
  }
  // Verify the memory types on the input/output data.
  res = TEE_CheckMemoryAccessRights(
      TEE_MEMORY_ACCESS_READ | TEE_MEMORY_ACCESS_NONSECURE |
          TEE_MEMORY_ACCESS_ANY_OWNER,
      params[2].memref.buffer, params[2].memref.size);
  if (res != TEE_SUCCESS) {
    EMSG("Invalid input memory");
    goto out;
  }
  res = TEE_CheckMemoryAccessRights(
      TEE_MEMORY_ACCESS_WRITE | TEE_MEMORY_ACCESS_NONSECURE |
          TEE_MEMORY_ACCESS_ANY_OWNER,
      params[3].memref.buffer, params[3].memref.size);
  if (res != TEE_SUCCESS) {
    EMSG("Invalid output memory");
    goto out;
  }

  struct H264SliceHeaderData slice_hdr;
  struct StreamDataForSliceHeader* stream_data = params[2].memref.buffer;
  struct StreamDataForSliceHeader stream_data_in_stack;
  if (params[2].memref.size ==
      sizeof(struct StreamDataForSliceHeaderDeprecated)) {
    FillStreamDataFromDeprecated(
        &stream_data_in_stack,
        (struct StreamDataForSliceHeaderDeprecated*)params[2].memref.buffer);
    stream_data = &stream_data_in_stack;
  }
  if (!ParseSliceHeader(((uint8_t*)in_addr) + params[1].value.a,
                        in_size - params[1].value.a, stream_data, &slice_hdr)) {
    EMSG("ParseSliceHeader failed");
    res = TEE_ERROR_BAD_FORMAT;
    goto out;
  }
  // Copy the slice header to the memref so it gets sent back.
  if (params[3].memref.size == sizeof(struct H264SliceHeaderDataDeprecated)) {
    struct H264SliceHeaderDataDeprecated* slice_hdr_out =
        params[3].memref.buffer;
    FillSliceHeaderDeprecated(slice_hdr_out, &slice_hdr);
    params[3].memref.size = sizeof(struct H264SliceHeaderDataDeprecated);
  } else {
    memcpy(params[3].memref.buffer, &slice_hdr, sizeof(slice_hdr));
    params[3].memref.size = sizeof(slice_hdr);
  }
  res = TEE_SUCCESS;

out:
  TEE_CloseTASession(sess);
  tee_unmap(in_addr, in_size);
  return res;
}
