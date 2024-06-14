// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBPMT_BITS_PMT_METADATA_H_
#define LIBPMT_BITS_PMT_METADATA_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <libpmt/bits/pmt_data_interface.h>

namespace pmt {

struct DecodingContext;

// Sample data type required to get the proper value.
enum DataType : uint8_t {
  // Unsigned integer, read via SampleValue::u64_.
  UINT,
  // Signed integer, read via SampleValue::i64_.
  SINT,
  // Float, read via SampleValue::f_.
  FLOAT,
};

// Sample metadata used to decode and describe it.
struct SampleMetadata {
  // Name of this sample.
  const std::string name_;
  // Name of the sample group.
  const std::string group_;
  // Description of the sample.
  const std::string description_;
  // Type of this sample.
  DataType type_;
  // GUID identifying a PMT device this sample belongs to.
  Guid guid_;
};

// Sample value. Should be read according to SampleMetadata::type_.
union SampleValue {
  // Signed integer.
  int64_t i64_;
  // Unsigned integer for convenience.
  uint64_t u64_;
  // Floating point. For now there seems to be only a single float type in
  // PMT schemas.
  float f_;
};

// Transformation function for integer values. Called with the parameter 0
// reference, decoding context and index of this parameter in
// DecodingContext::info_. Will return the transformed value without modifying
// param0.
typedef int64_t (*IntegerTransform)(const SampleValue& param0,
                                    const DecodingContext* ctx,
                                    size_t idx);
// Transformation function for floating point values. Called with the parameter
// 0 reference, decoding context and index of this parameter in
// DecodingContext::info_. Will return the transformed value without modifying
// param0.
typedef float (*FloatTransform)(const SampleValue& param0,
                                const DecodingContext* ctx,
                                size_t idx);

// Data required to extract and decode a sample.
struct SampleDecodingInfo {
  // Byte offset in the binary data to the start of a 64bit word where the
  // sample is located.
  size_t offset_;
  union ValueTransform {
    // Transformation used if SampleMetadata::type_ != DataType::FLOAT.
    IntegerTransform to_int_;
    // Transformation used if SampleMetadata::type_ == DataType::FLOAT.
    FloatTransform to_float_;
  } transform_;
  // An index into DeviceDecodingContext::extra_args_. Only valid for samples
  // using extra parameters.
  uint16_t extra_arg_idx_;
  // The least significant bit of the sample in the data word.
  uint8_t lsb_;
  // The most significant bit of the sample in the data word.
  uint8_t msb_;
};

// Holds pointers to placeholders for extra parameters.
//
// NOTE: This has to be a pointer because parameters beyond parameter_0 can
// be in any GUID. In theory so could be parameter_0 (and the sample value be
// one of extra parameters) but there is currently no metadata like that.
// NOTE: Currently we only have metadata with a single extra parameter. We
// could have a vector<> here to be forward-compatible but that would put the
// data for each sample in a different place. This way we save on cache
// pressure. If at any point this changes, add another parameter here.
struct ExtraArgs {
  SampleValue* parameter_1_ = nullptr;
};

// User facing structure ultimately holding the decoded data.
//
// Data at the same index represents the same sample.
struct DecodingResult {
  // Metadata for each sample.
  std::vector<SampleMetadata> meta_;
  // Sample values.
  std::vector<SampleValue> values_;
};

// Structure holding all the data structures necessary for PMT data decoding.
//
// Data in ctx_, result_.meta_ and result_.values_ at the same index describes
// a single sample. Samples from a single GUID are contiguous, followed by the
// samples from the next GUID. Order of those samples is stable and will not
// change until PmtDecoder::SetUpDecoding() is called again.
struct DecodingContext {
  // Information necessary to decode every sample on this device.
  std::vector<SampleDecodingInfo> info_;
  // Extra parameters for sample transformations which use those.
  std::vector<ExtraArgs> extra_args_;
  // Structure where all the data is decoded to and later on returned to the
  // user.
  DecodingResult result_;
};

// Get Integer transform function for a given name.
//
// @param id Transformation::transformID.
// @return Transformation function pointer or nullptr if nothing found.
IntegerTransform GetIntegerTransform(const std::string_view& id);

// Get Float transform function for a given name.
//
// @param id Transformation::transformID.
// @return Transformation function pointer or nullptr if nothing found.
FloatTransform GetFloatTransform(const std::string_view& id);

}  // namespace pmt

#endif  // LIBPMT_BITS_PMT_METADATA_H_
