// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/factory_hwid_processor_impl.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/hwid/hwid_utils.h>
#include <google/protobuf/repeated_ptr_field.h>
#include <libcrossystem/crossystem.h>

#include "hardware_verifier/encoding_spec_loader.h"
#include "hardware_verifier/factory_hwid_processor.h"
#include "hardware_verifier/hardware_verifier.pb.h"
#include "hardware_verifier/system/context.h"

namespace hardware_verifier {

namespace {
constexpr int kIgnoreBitWidth = 1;
constexpr int kImageIDBitWidth = 4;
constexpr char kCrosSystemHWIDKey[] = "hwid";

uint32_t BinaryStringToUint32(const std::string_view& binary_str) {
  CHECK(!binary_str.empty() && binary_str.length() <= 32 &&
        binary_str.find_first_not_of("01") == std::string::npos);

  uint32_t res = 0;
  for (const auto& c : binary_str) {
    res <<= 1;
    if (c == '1') {
      // Set the last bit to 1.
      res |= 1;
    }
  }
  return res;
}

std::string ExtractHWIDComponentBits(const std::string& hwid_decode_bits) {
  return hwid_decode_bits.substr(kIgnoreBitWidth + kImageIDBitWidth);
}

std::string ExtractHeaderBits(const std::string& hwid_decode_bits) {
  return hwid_decode_bits.substr(0, kIgnoreBitWidth + kImageIDBitWidth);
}

std::optional<std::string> GetFactoryHWIDPrefix() {
  auto hwid = Context::Get()->crossystem()->VbGetSystemPropertyString(
      kCrosSystemHWIDKey);
  auto hwid_split = base::RSplitStringOnce(*hwid, ' ');
  if (!hwid_split.has_value()) {
    LOG(ERROR) << "Got malformed Factory HWID: " << *hwid;
    return std::nullopt;
  }
  return std::string(hwid_split->first);
}

bool ShouldSkipZeroBitCategory(int zero_bit_pos,
                               const std::string_view& hwid_component_bits) {
  return zero_bit_pos > hwid_component_bits.length() - 1;
}

std::optional<EncodingPattern> GetEncodingPattern(
    const std::string_view& decoded_bits, const EncodingSpec& encoding_spec) {
  auto image_id_bits =
      std::string(decoded_bits.substr(kIgnoreBitWidth, kImageIDBitWidth));
  auto image_id = BinaryStringToUint32(image_id_bits);

  for (const auto& pattern : encoding_spec.encoding_patterns()) {
    for (int id : pattern.image_ids()) {
      if (id == image_id) {
        return pattern;
      }
    }
  }
  LOG(ERROR) << "Encoding pattern not found for image ID: " << image_id;
  return std::nullopt;
}

// Helper function to get the component index from HWID decoded bits.
// Returns a |CategoryMapping| mapping component category to component index.
CategoryMapping<int> ExtractEncodedComponentIndex(
    const std::string_view& hwid_component_bits,
    const EncodingPattern& encoding_pattern) {
  CategoryMapping<std::vector<std::string>> component_bits;
  for (const auto& bit_range : encoding_pattern.bit_ranges()) {
    if (bit_range.start() >= hwid_component_bits.length()) {
      break;
    }
    const auto& category = bit_range.category();
    auto bit_length = bit_range.end() - bit_range.start() + 1;
    component_bits[category].push_back(
        std::string(hwid_component_bits.substr(bit_range.start(), bit_length)));
  }

  CategoryMapping<int> component_indexes;
  for (auto& [category, bits] : component_bits) {
    std::reverse(bits.begin(), bits.end());
    component_indexes[category] = BinaryStringToUint32(base::StrCat(bits));
  }

  // Handle 0-bit components.
  for (const auto& first_zero_bit : encoding_pattern.first_zero_bits()) {
    const auto& category = first_zero_bit.category();
    const auto& zero_bit_pos = first_zero_bit.zero_bit_position();
    if (component_indexes.find(category) != component_indexes.end() ||
        ShouldSkipZeroBitCategory(zero_bit_pos, hwid_component_bits)) {
      continue;
    }
    component_indexes[category] = 0;
  }

  return component_indexes;
}

// Helper function to convert the component index to component names by
// searching the index in encoded fields.
// Returns a |CategoryMapping| mapping component category to component names.
std::optional<CategoryMapping<std::vector<std::string>>>
ComponentIndexToComponentNames(
    const CategoryMapping<int>& component_indexes,
    const CategoryMapping<EncodedFields>& encoded_fields) {
  CategoryMapping<std::vector<std::string>> res;
  for (const auto& [category, encoded_comp_idx] : component_indexes) {
    auto encoded_fields_it = encoded_fields.find(category);
    if (encoded_fields_it == encoded_fields.end()) {
      LOG(ERROR) << "Category \"" << ProbeRequest_SupportCategory_Name(category)
                 << "\" not found in encoded fields.";
      return std::nullopt;
    }
    const auto& components_of_category =
        encoded_fields_it->second.encoded_components();
    auto encoded_comp_it = std::find_if(
        components_of_category.begin(), components_of_category.end(),
        [&](const EncodedComponents& component) {
          return component.index() == encoded_comp_idx;
        });
    if (encoded_comp_it == components_of_category.end()) {
      LOG(ERROR) << "No component found for category "
                 << ProbeRequest_SupportCategory_Name(category)
                 << " with index " << encoded_comp_idx;
      return std::nullopt;
    }

    for (const std::string& name : encoded_comp_it->component_names()) {
      res[category].push_back(name);
    }
  }
  return res;
}

}  // namespace

std::unique_ptr<FactoryHWIDProcessorImpl> FactoryHWIDProcessorImpl::Create(
    const EncodingSpecLoader& encoding_spec_loader) {
  auto spec = encoding_spec_loader.Load();
  if (!spec) {
    LOG(ERROR) << "Failed to load encoding spec.";
    return nullptr;
  }

  auto hwid = Context::Get()->crossystem()->VbGetSystemPropertyString(
      kCrosSystemHWIDKey);
  if (!hwid.has_value()) {
    LOG(ERROR) << "Failed to get HWID from crossystem.";
    return nullptr;
  }

  auto decoded_bits = brillo::hwid::DecodeHWID(*hwid);
  if (!decoded_bits.has_value() ||
      decoded_bits->length() <= kIgnoreBitWidth + kImageIDBitWidth) {
    LOG(ERROR) << "Got invalid HWID: " << *hwid;
    return nullptr;
  }

  auto encoding_pattern = GetEncodingPattern(*decoded_bits, *spec);
  if (!encoding_pattern.has_value()) {
    LOG(ERROR) << "Failed to get encoding pattern.";
    return nullptr;
  }

  return std::unique_ptr<FactoryHWIDProcessorImpl>(new FactoryHWIDProcessorImpl(
      *encoding_pattern, *decoded_bits, spec->encoded_fields()));
}

FactoryHWIDProcessorImpl::FactoryHWIDProcessorImpl(
    EncodingPattern& encoding_pattern,
    std::string& hwid_decode_bits,
    const google::protobuf::RepeatedPtrField<EncodedFields>& encoded_fields)
    : encoding_pattern_(encoding_pattern), hwid_decode_bits_(hwid_decode_bits) {
  for (const auto& field : encoded_fields) {
    encoded_fields_[field.category()] = field;
  }
}

std::optional<CategoryMapping<std::vector<std::string>>>
FactoryHWIDProcessorImpl::DecodeFactoryHWID() const {
  const auto hwid_component_bits = ExtractHWIDComponentBits(hwid_decode_bits_);
  auto component_indexes =
      ExtractEncodedComponentIndex(hwid_component_bits, encoding_pattern_);
  return ComponentIndexToComponentNames(component_indexes, encoded_fields_);
}

std::set<runtime_probe::ProbeRequest_SupportCategory>
FactoryHWIDProcessorImpl::GetSkipZeroBitCategories() const {
  std::set<runtime_probe::ProbeRequest_SupportCategory> skip_categories;
  const auto hwid_component_bits = ExtractHWIDComponentBits(hwid_decode_bits_);
  for (const auto& first_zero_bit : encoding_pattern_.first_zero_bits()) {
    if (ShouldSkipZeroBitCategory(first_zero_bit.zero_bit_position(),
                                  hwid_component_bits)) {
      skip_categories.insert(first_zero_bit.category());
    }
  }
  return skip_categories;
}

std::string FactoryHWIDProcessorImpl::GetMaskedComponentBits() const {
  std::string masked_component_bits =
      ExtractHWIDComponentBits(hwid_decode_bits_);
  for (const auto& bit_range : encoding_pattern_.bit_ranges()) {
    if (bit_range.start() >= masked_component_bits.length()) {
      break;
    }
    for (int idx = bit_range.start();
         idx <= std::min(bit_range.end(),
                         static_cast<int>(masked_component_bits.length()) - 1);
         idx++) {
      masked_component_bits[idx] = '0';
    }
  }
  return masked_component_bits;
}

std::optional<std::string> FactoryHWIDProcessorImpl::GenerateMaskedFactoryHWID()
    const {
  const auto factory_hwid_prefix = GetFactoryHWIDPrefix();
  if (!factory_hwid_prefix.has_value()) {
    return std::nullopt;
  }

  const auto masked_component_bits = GetMaskedComponentBits();
  const auto header_bits = ExtractHeaderBits(hwid_decode_bits_);
  return brillo::hwid::EncodeHWID(*factory_hwid_prefix,
                                  header_bits + masked_component_bits);
}

}  // namespace hardware_verifier
