// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwdrm-videoproc-ta/h264_parser.h"

#include <string.h>

struct H264Bitstream {
  // Pointer to the next unread (not in curr_byte) byte in the stream.
  uint8_t* data;
  // Bytes left in the stream (without the curr_byte).
  uint32_t bytes_left;
  // Contents of the current byte; first unread bit starting at position
  // 8 - bits_left_in_byte from MSB.
  uint8_t curr_byte;
  // Number of bits remaining in curr_byte.
  uint8_t bits_left_in_byte;
  // Used in emulation prevention three byte detection (see spec).
  // Initially set to 0xffff to accept all initial two-byte sequences.
  uint16_t prev_two_bytes;
  // Number of emulation prevent bytes read in stream.
  uint32_t emulation_prevention_bytes;
};

static void InitBitReader(struct H264Bitstream* br,
                          uint8_t* data,
                          uint32_t size) {
  br->data = data;
  br->bytes_left = size;
  br->bits_left_in_byte = 0;
  br->prev_two_bytes = 0xFFFF;
  br->emulation_prevention_bytes = 0;
}

static bool UpdateCurrByte(struct H264Bitstream* br) {
  if (br->bytes_left < 1)
    return false;

  // Emulation prevention three-byte detection.
  // If a sequence of 0x000003 is found, skip (ignore) the last byte (0x03).
  if (*br->data == 0x03 && (br->prev_two_bytes & 0xffff) == 0) {
    // Detected 0x000003, skip last byte.
    ++br->data;
    --br->bytes_left;
    ++br->emulation_prevention_bytes;
    // Need another full three bytes before we can detect the sequence again.
    br->prev_two_bytes = 0xffff;

    if (br->bytes_left < 1)
      return false;
  }

  // Load a new byte and advance pointers.
  br->curr_byte = *br->data++;
  --br->bytes_left;
  br->bits_left_in_byte = 8;

  br->prev_two_bytes = ((br->prev_two_bytes & 0xff) << 8) | br->curr_byte;

  return true;
}

// Read |num_bits| (1 to 31 inclusive) from the stream and return them
// in |out|, with first bit in the stream as MSB in |out| at position
// (|num_bits| - 1).
static bool ReadBits(struct H264Bitstream* br,
                     uint8_t num_bits,
                     uint32_t* out) {
  if (num_bits >= 32)
    return false;
  int bits_left = num_bits;
  *out = 0;

  while (br->bits_left_in_byte < bits_left) {
    // Take all that's left in current byte, shift to make space for the rest.
    *out |= ((br->curr_byte & ((1u << br->bits_left_in_byte) - 1u))
             << (bits_left - br->bits_left_in_byte));
    bits_left -= br->bits_left_in_byte;

    if (!UpdateCurrByte(br))
      return false;
  }

  *out |= (br->curr_byte >> (br->bits_left_in_byte - bits_left));
  *out &= ((1u << num_bits) - 1u);
  br->bits_left_in_byte -= bits_left;

  return true;
}

static uint32_t NumBitsLeft(struct H264Bitstream* br) {
  return (br->bits_left_in_byte + br->bytes_left * 8);
}

static bool IsPSlice(struct H264SliceHeaderData* hdr) {
  return (hdr->slice_type % 5 == 0);
}

static bool IsBSlice(struct H264SliceHeaderData* hdr) {
  return (hdr->slice_type % 5 == 1);
}

static bool IsISlice(struct H264SliceHeaderData* hdr) {
  return (hdr->slice_type % 5 == 2);
}

static bool IsSPSlice(struct H264SliceHeaderData* hdr) {
  return (hdr->slice_type % 5 == 3);
}

static bool IsSISlice(struct H264SliceHeaderData* hdr) {
  return (hdr->slice_type % 5 == 4);
}

#define READ_BITS_OR_RETURN(num_bits, out) \
  do {                                     \
    if (!ReadBits(br, num_bits, out)) {    \
      return false;                        \
    }                                      \
  } while (0)

#define TRUE_OR_RETURN(a) \
  do {                    \
    if (!(a)) {           \
      return false;       \
    }                     \
  } while (0)

// Exp-Golomb code parsing as specified in H.26x specifications.
// Read one unsigned exp-Golomb code from the stream and return in |*out|.
#define READ_UE_OR_RETURN(out)                          \
  do {                                                  \
    uint32_t _bit = 0;                                  \
    uint32_t _num_bits_processed = -1;                  \
    do {                                                \
      READ_BITS_OR_RETURN(1, &_bit);                    \
      _num_bits_processed++;                            \
    } while (_bit == 0);                                \
    if (_num_bits_processed > 31) {                     \
      return false;                                     \
    }                                                   \
    *out = (1u << _num_bits_processed) - 1u;            \
    uint32_t _rest;                                     \
    if (_num_bits_processed == 31) {                    \
      READ_BITS_OR_RETURN(_num_bits_processed, &_rest); \
      if (_rest == 0) {                                 \
        break;                                          \
      } else {                                          \
        return false;                                   \
      }                                                 \
    }                                                   \
    if (_num_bits_processed > 0) {                      \
      READ_BITS_OR_RETURN(_num_bits_processed, &_rest); \
      *out += _rest;                                    \
    }                                                   \
  } while (0)

// Read one signed exp-Golomb code from the stream and return in |*out|.
#define READ_SE_OR_RETURN(out)      \
  do {                              \
    uint32_t ue = 0;                \
    READ_UE_OR_RETURN(&ue);         \
    if (ue % 2 == 0) {              \
      *out = -(((int32_t)ue) / 2);  \
    } else {                        \
      *out = ((int32_t)ue) / 2 + 1; \
    }                               \
  } while (0)

static bool SkipRefPicListModification(struct H264Bitstream* br,
                                       uint32_t num_ref_idx_active_minus1) {
  if (num_ref_idx_active_minus1 >= 32)
    return false;

  for (int i = 0; i < 32; ++i) {
    int modification_of_pic_nums_idc;
    int data;
    READ_UE_OR_RETURN(&modification_of_pic_nums_idc);
    TRUE_OR_RETURN(modification_of_pic_nums_idc < 4);

    switch (modification_of_pic_nums_idc) {
      case 0:
      case 1:
        READ_UE_OR_RETURN(&data);  // abs_diff_pic_num_minus1
        break;

      case 2:
        READ_UE_OR_RETURN(&data);  // long_term_pic_num
        break;

      case 3:
        // Per spec, list cannot be empty.
        if (i == 0)
          return false;
        return true;

      default:
        return false;
    }
  }

  // If we got here, we didn't get loop end marker prematurely,
  // so make sure it is there for our client.
  int modification_of_pic_nums_idc;
  READ_UE_OR_RETURN(&modification_of_pic_nums_idc);
  TRUE_OR_RETURN(modification_of_pic_nums_idc == 3);

  return true;
}

static bool SkipWeightingFactors(struct H264Bitstream* br,
                                 int num_ref_idx_active_minus1,
                                 int chroma_array_type) {
  for (int i = 0; i < num_ref_idx_active_minus1 + 1; ++i) {
    uint32_t data;
    int32_t sdata;
    READ_BITS_OR_RETURN(1, &data);  // luma_weight_flag
    if (data) {
      READ_SE_OR_RETURN(&sdata);  // luma_weight[i]
      READ_SE_OR_RETURN(&sdata);  // luma_offset[i]
    }

    if (chroma_array_type != 0) {
      READ_BITS_OR_RETURN(1, &data);  // chroma_weight_flag
      if (data) {
        for (int j = 0; j < 2; ++j) {
          READ_SE_OR_RETURN(&sdata);  // chroma_weight[i][j]
          READ_SE_OR_RETURN(&sdata);  // chroma_offset[i][j]
        }
      }
    }
  }

  return true;
}

bool ParseSliceHeader(uint8_t* slice_header,
                      uint32_t header_size,
                      struct StreamDataForSliceHeader* stream_data,
                      struct H264SliceHeaderData* hdr_out) {
  // Be very strict about bitstream conformance, we don't want this used as a
  // tool to extract data from anything else.
  if (header_size < 4)
    return false;
  if (slice_header[0] != 0 || slice_header[1] != 0 || slice_header[2] != 1)
    return false;

  // Initialize the reader, skip the 3 byte start code.
  struct H264Bitstream bitstream;
  struct H264Bitstream* br = &bitstream;
  InitBitReader(br, slice_header + 3, header_size - 3);

  memset(hdr_out, 0, sizeof(*hdr_out));

  // Parse the NALU header.
  uint32_t data;
  // Forbidden zero bit.
  READ_BITS_OR_RETURN(1, &data);
  TRUE_OR_RETURN(data == 0);

  READ_BITS_OR_RETURN(2, &data);
  hdr_out->nal_ref_idc = (uint8_t)data;
  READ_BITS_OR_RETURN(5, &data);
  uint8_t nal_unit_type = (uint8_t)data;

  // It should only be a slice header NALU, nothing else is allowed here.
  if (nal_unit_type != 1 && nal_unit_type != 5)
    return false;

  hdr_out->idr_pic_flag = (nal_unit_type == 5);

  READ_UE_OR_RETURN(&data);  // first_mb_in_slice
  READ_UE_OR_RETURN(&data);
  hdr_out->slice_type = (uint8_t)data;
  TRUE_OR_RETURN(hdr_out->slice_type < 10);

  READ_UE_OR_RETURN(&data);  // pic_parameter_set_id

  TRUE_OR_RETURN(stream_data->log2_max_frame_num_minus4 < 13);
  READ_BITS_OR_RETURN(stream_data->log2_max_frame_num_minus4 + 4,
                      &hdr_out->frame_num);
  if (!stream_data->frame_mbs_only_flag) {
    READ_BITS_OR_RETURN(1, &data);
    hdr_out->field_pic_flag = (uint8_t)data;
  }

  if (hdr_out->idr_pic_flag)
    READ_UE_OR_RETURN(&hdr_out->idr_pic_id);

  size_t bits_left_at_pic_order_cnt_start = NumBitsLeft(br);
  if (stream_data->pic_order_cnt_type == 0) {
    TRUE_OR_RETURN(stream_data->log2_max_pic_order_cnt_lsb_minus4 < 13);
    READ_BITS_OR_RETURN(stream_data->log2_max_pic_order_cnt_lsb_minus4 + 4,
                        &hdr_out->pic_order_cnt_lsb);
    if (stream_data->bottom_field_pic_order_in_frame_present_flag &&
        !hdr_out->field_pic_flag)
      READ_SE_OR_RETURN(&hdr_out->delta_pic_order_cnt_bottom);
  }

  if (stream_data->pic_order_cnt_type == 1 &&
      !stream_data->delta_pic_order_always_zero_flag) {
    READ_SE_OR_RETURN(&hdr_out->delta_pic_order_cnt0);
    if (stream_data->bottom_field_pic_order_in_frame_present_flag &&
        !hdr_out->field_pic_flag)
      READ_SE_OR_RETURN(&hdr_out->delta_pic_order_cnt1);
  }

  hdr_out->pic_order_cnt_bit_size =
      bits_left_at_pic_order_cnt_start - NumBitsLeft(br);

  if (stream_data->redundant_pic_cnt_present_flag) {
    READ_UE_OR_RETURN(&data);  // redundant_pic_cnt
    TRUE_OR_RETURN(data < 128);
  }

  if (IsBSlice(hdr_out))
    READ_BITS_OR_RETURN(1, &data);  // direct_spatial_mv_pred_flag

  int num_ref_idx_l0_active_minus1 = 0;
  int num_ref_idx_l1_active_minus1 = 0;
  if (IsPSlice(hdr_out) || IsSPSlice(hdr_out) || IsBSlice(hdr_out)) {
    uint32_t num_ref_idx_active_override_flag;
    READ_BITS_OR_RETURN(1, &num_ref_idx_active_override_flag);
    if (num_ref_idx_active_override_flag) {
      READ_UE_OR_RETURN(&num_ref_idx_l0_active_minus1);
      if (IsBSlice(hdr_out))
        READ_UE_OR_RETURN(&num_ref_idx_l1_active_minus1);
    } else {
      num_ref_idx_l0_active_minus1 =
          stream_data->num_ref_idx_l0_default_active_minus1;
      if (IsBSlice(hdr_out)) {
        num_ref_idx_l1_active_minus1 =
            stream_data->num_ref_idx_l1_default_active_minus1;
      }
    }
  }

  if (!IsISlice(hdr_out) && !IsSISlice(hdr_out)) {
    uint32_t ref_pic_list_modification_flag_l0;
    READ_BITS_OR_RETURN(1, &ref_pic_list_modification_flag_l0);
    if (ref_pic_list_modification_flag_l0) {
      if (!SkipRefPicListModification(br, num_ref_idx_l0_active_minus1))
        return false;
    }
  }

  if (IsBSlice(hdr_out)) {
    uint32_t ref_pic_list_modification_flag_l1;
    READ_BITS_OR_RETURN(1, &ref_pic_list_modification_flag_l1);
    if (ref_pic_list_modification_flag_l1) {
      if (!SkipRefPicListModification(br, num_ref_idx_l1_active_minus1))
        return false;
    }
  }

  if ((stream_data->weighted_pred_flag &&
       (IsPSlice(hdr_out) || IsSPSlice(hdr_out))) ||
      (stream_data->weighted_bipred_idc == 1 && IsBSlice(hdr_out))) {
    READ_UE_OR_RETURN(&data);  // luma_log2_weight_denom

    if (stream_data->chroma_array_type != 0)
      READ_UE_OR_RETURN(&data);  // chroma_log2_weight_denom

    if (!SkipWeightingFactors(br, num_ref_idx_l0_active_minus1,
                              stream_data->chroma_array_type)) {
      return false;
    }

    if (IsBSlice(hdr_out)) {
      if (!SkipWeightingFactors(br, num_ref_idx_l1_active_minus1,
                                stream_data->chroma_array_type)) {
        return false;
      }
    }
  }

  if (hdr_out->nal_ref_idc != 0) {
    size_t bits_left_at_start = NumBitsLeft(br);

    if (hdr_out->idr_pic_flag) {
      READ_BITS_OR_RETURN(1, &data);
      hdr_out->ref_pic_fields.bits.no_output_of_prior_pics_flag = data;
      READ_BITS_OR_RETURN(1, &data);
      hdr_out->ref_pic_fields.bits.long_term_reference_flag = data;
    } else {
      READ_BITS_OR_RETURN(1, &data);
      hdr_out->ref_pic_fields.bits.adaptive_ref_pic_marking_mode_flag = data;

      if (hdr_out->ref_pic_fields.bits.adaptive_ref_pic_marking_mode_flag) {
        size_t i;
        for (i = 0; i < 32; ++i) {
          READ_UE_OR_RETURN(&hdr_out->memory_management_control_operation[i]);
          if (hdr_out->memory_management_control_operation[i] == 0)
            break;
          hdr_out->ref_pic_fields.bits.dec_ref_pic_marking_count++;

          if (hdr_out->memory_management_control_operation[i] == 1 ||
              hdr_out->memory_management_control_operation[i] == 3)
            READ_UE_OR_RETURN(&hdr_out->difference_of_pic_nums_minus1[i]);

          if (hdr_out->memory_management_control_operation[i] == 2)
            READ_UE_OR_RETURN(&hdr_out->long_term_pic_num[i]);

          if (hdr_out->memory_management_control_operation[i] == 3 ||
              hdr_out->memory_management_control_operation[i] == 6)
            READ_UE_OR_RETURN(&hdr_out->long_term_frame_idx[i]);

          if (hdr_out->memory_management_control_operation[i] == 4)
            READ_UE_OR_RETURN(&hdr_out->max_long_term_frame_idx_plus1[i]);

          if (hdr_out->memory_management_control_operation[i] > 6)
            return false;
        }
        // We should break at the last field and never hit 32.
        if (i == 32)
          return false;
      }
    }

    hdr_out->dec_ref_pic_marking_bit_size =
        bits_left_at_start - NumBitsLeft(br);
  }

  return true;
}
