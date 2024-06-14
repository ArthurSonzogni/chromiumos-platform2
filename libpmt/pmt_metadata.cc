// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <unordered_map>

#include <libpmt/bits/pmt_metadata.h>

namespace pmt {

using IntegerTransformMap =
    std::unordered_map<std::string_view, IntegerTransform>;
using FloatTransformMap = std::unordered_map<std::string_view, FloatTransform>;

std::string_view trim(const std::string_view& str) {
  constexpr std::string_view whitespace(" \t");
  std::string_view result(str);
  result.remove_prefix(
      std::min(str.find_first_not_of(whitespace), result.size()));
  while (whitespace.find(result.back()) != whitespace.npos)
    result.remove_suffix(1);
  return result;
}

// In the metadata, this transformation is a rather baroque way of decoding an 8
// bit U2 number. Since the signed integer representation on x86 is U2, this is
// unnecessary.
int64_t signed_8bit(const SampleValue& param0,
                    const DecodingContext* ctx,
                    size_t idx) {
  return static_cast<int8_t>(param0.u64_ & 0xff);
}

int64_t passthrough(const SampleValue& param0,
                    const DecodingContext* ctx,
                    size_t idx) {
  return param0.i64_;
}

float bw_32B(const SampleValue& param0,
             const DecodingContext* ctx,
             size_t idx) {
  // <transform>$parameter_0 * 32 / 1e6 </transform>
  return (param0.u64_ * 32.0) / 1.0e6;
}

float bw_64B(const SampleValue& param0,
             const DecodingContext* ctx,
             size_t idx) {
  // <transform>$parameter_0 * 64 / 1e6 </transform>
  return (param0.u64_ * 64.0) / 1.0e6;
}

float bw_B(const SampleValue& param0, const DecodingContext* ctx, size_t idx) {
  // <transform>$parameter_0 / 1e6 </transform>
  return param0.u64_ / 1.0e6;
}

float bw_KB(const SampleValue& param0, const DecodingContext* ctx, size_t idx) {
  // <transform>$parameter_0 / 1e3 </transform>
  return param0.u64_ / 1.0e3;
}

float cep_volts(const SampleValue& param0,
                const DecodingContext* ctx,
                size_t idx) {
  // <transform>$parameter_0 * 0.002 </transform>
  return param0.u64_ * 0.002;
}

float clk_freq(const SampleValue& param0,
               const DecodingContext* ctx,
               size_t idx) {
  // <transform>$parameter_0 </transform>
  return param0.u64_ * 1.0;
}

float cycle_count(const SampleValue& param0,
                  const DecodingContext* ctx,
                  size_t idx) {
  // <transform>$parameter_0 </transform>
  return param0.u64_ * 1.0;
}

float energy_J(const SampleValue& param0,
               const DecodingContext* ctx,
               size_t idx) {
  // <transform>$parameter_0 / 1048576 </transform>
  return param0.u64_ / 1048576.0;
}

float event_counter(const SampleValue& param0,
                    const DecodingContext* ctx,
                    size_t idx) {
  // <transform>$parameter_0 </transform>
  return param0.u64_ * 1.0;
}

float gt_clk_cnt(const SampleValue& param0,
                 const DecodingContext* ctx,
                 size_t idx) {
  // <transform>$parameter_0 * 64 </transform>
  return param0.u64_ * 64.0;
}

float ipu_icc(const SampleValue& param0,
              const DecodingContext* ctx,
              size_t idx) {
  // <transform>$parameter_0 * 100 / 16 </transform>
  return (param0.u64_ * 100.0) / 16.0;
}

float ltr(const SampleValue& param0, const DecodingContext* ctx, size_t idx) {
  // <transform>$parameter_0 </transform>
  return param0.u64_ * 1.0;
}

float mc_cycles(const SampleValue& param0,
                const DecodingContext* ctx,
                size_t idx) {
  // <transform>$parameter_0 * 0.025 * 33.33 </transform>
  return param0.u64_ * 0.025 * 33.33;
}

float mc_on_time(const SampleValue& param0,
                 const DecodingContext* ctx,
                 size_t idx) {
  // <transform>$parameter_0 * 0.025 / 1e6 </transform>
  return param0.u64_ * 0.025 / 1.0e6;
}

float p0_div_p1_100(const SampleValue& param0,
                    const DecodingContext* ctx,
                    size_t idx) {
  // This transformation handles both 2-argument and 2-nd-implicit arguments.
  // The PmtDecoder::SetUpDecoding() should handle setting the proper
  // extra_args.

  // pkgc_block_cause:
  // <transform>$parameter_0 / PACKAGE_CSTATE_BLOCK_REFCNT * 100 </transform>
  // <transform>$parameter_0 / $parameter_1 * 100 </transform>
  // pkgc_wake_cause:
  // <transform>$parameter_0 / PACKAGE_CSTATE_WAKE_REFCNT * 100 </transform>
  // <transform>$parameter_0 / $parameter_1 * 100 </transform>
  return (param0.u64_ /
          ctx->extra_args_[ctx->info_[idx].extra_arg_idx_].parameter_1_->f_) *
         100.0;
}

float ratio_100(const SampleValue& param0,
                const DecodingContext* ctx,
                size_t idx) {
  // <transform>$parameter_0 * 0.1 </transform>
  return param0.u64_ * 0.1;
}

float ratio_16(const SampleValue& param0,
               const DecodingContext* ctx,
               size_t idx) {
  // <transform>$parameter_0 * 0.01667 </transform>
  return param0.u64_ * 0.01667;
}

float ratio_25(const SampleValue& param0,
               const DecodingContext* ctx,
               size_t idx) {
  // <transform>$parameter_0 * 0.025 </transform>
  return param0.u64_ * 0.025;
}

float ratio_33(const SampleValue& param0,
               const DecodingContext* ctx,
               size_t idx) {
  // <transform>$parameter_0 * 0.033 </transform>
  return param0.u64_ * 0.033;
}

float U10_7_3(const SampleValue& param0,
              const DecodingContext* ctx,
              size_t idx) {
  // <transform>( $parameter_0 &amp; 0x3ff ) / ( 2**3 ) </transform>
  return (param0.u64_ & 0x3ff) / 8.0;
}

float U11_9_2(const SampleValue& param0,
              const DecodingContext* ctx,
              size_t idx) {
  // <transform>( $parameter_0 &amp; 0x7ff ) / ( 2**2 ) </transform>
  return (param0.u64_ & 0x7ff) / 4.0;
}

float U16_1_15(const SampleValue& param0,
               const DecodingContext* ctx,
               size_t idx) {
  // <transform>( $parameter_0 &amp; 0xffff ) / ( 2**15 ) </transform>
  return (param0.u64_ & 0xffff) / 32768.0;
}

float U16_8_8(const SampleValue& param0,
              const DecodingContext* ctx,
              size_t idx) {
  // <transform>( $parameter_0 &amp; 0xffff ) / ( 2**8 ) </transform>
  return (param0.u64_ & 0xffff) / 256.0;
}

float U32_18_14(const SampleValue& param0,
                const DecodingContext* ctx,
                size_t idx) {
  // <transform>( $parameter_0 &amp; 0xffffffff ) / ( 2**14 ) </transform>
  return (param0.u64_ & 0xffffffff) / 16384.0;
}

float U8_1_7(const SampleValue& param0,
             const DecodingContext* ctx,
             size_t idx) {
  // <transform>( $parameter_0 &amp; 0xff ) / ( 2**7 ) </transform>
  return (param0.u64_ & 0xff) / 128.0;
}

float U9_1_8(const SampleValue& param0,
             const DecodingContext* ctx,
             size_t idx) {
  // <transform>( $parameter_0 &amp; 0x1ff ) / ( 2**8 ) </transform>
  return (param0.u64_ & 0x1ff) / 256.0;
}

float vid(const SampleValue& param0, const DecodingContext* ctx, size_t idx) {
  // <transform> ( 49 + $parameter_0 ) * 0.005 </transform>
  return (49 + param0.u64_) * 0.005;
}

float vr_energy(const SampleValue& param0,
                const DecodingContext* ctx,
                size_t idx) {
  // <transform>$parameter_0 / 16384 </transform>
  return param0.u64_ / 16384.0;
}

float wp_volts(const SampleValue& param0,
               const DecodingContext* ctx,
               size_t idx) {
  // <transform>$parameter_0 * 0.0025 </transform>
  return param0.u64_ * 0.0025;
}

float xtal_time(const SampleValue& param0,
                const DecodingContext* ctx,
                size_t idx) {
  // <transform>$parameter_0 / 38.4 * 1e6 </transform>
  return param0.u64_ / (38.4 * 1e6);
}

static FloatTransformMap kFloatTransforms = {
    {"bw_32B", bw_32B},
    {"bw_64B", bw_64B},
    {"bw_B", bw_B},
    {"bw_KB", bw_KB},
    {"cep_volts", cep_volts},
    {"clk_freq", clk_freq},
    {"cycle_count", cycle_count},
    {"energy_J", energy_J},
    {"event_counter", event_counter},
    {"gt_clk_cnt", gt_clk_cnt},
    {"ipu_icc", ipu_icc},
    {"ltr", ltr},
    {"mc_cycles", mc_cycles},
    {"mc_on_time", mc_on_time},
    {"pkgc_block_cause", p0_div_p1_100},
    {"pkgc_wake_cause", p0_div_p1_100},
    {"ratio_100", ratio_100},
    {"ratio_16", ratio_16},
    {"ratio_25", ratio_25},
    {"ratio_33", ratio_33},
    {"U10.7.3", U10_7_3},
    {"U11.9.2", U11_9_2},
    {"U16.1.15", U16_1_15},
    {"U16.8.8", U16_8_8},
    {"U32.18.14", U32_18_14},
    {"U8.1.7", U8_1_7},
    {"U9.1.8", U9_1_8},
    {"vid", vid},
    {"vr_energy", vr_energy},
    {"wp_volts", wp_volts},
    {"xtal_time", xtal_time},
};

static IntegerTransformMap kIntegerTransforms = {
    {"passthru", passthrough},
    {"S8.7.0", signed_8bit},
};

IntegerTransform GetIntegerTransform(const std::string_view& id) {
  if (kIntegerTransforms.contains(id))
    return kIntegerTransforms.at(id);
  return nullptr;
}

FloatTransform GetFloatTransform(const std::string_view& id) {
  if (kFloatTransforms.contains(id))
    return kFloatTransforms.at(id);
  return nullptr;
}

}  // namespace pmt
