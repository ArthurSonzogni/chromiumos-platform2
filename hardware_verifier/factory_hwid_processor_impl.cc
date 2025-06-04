// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/factory_hwid_processor_impl.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
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

  std::string hwid_component_bits =
      decoded_bits->substr(kIgnoreBitWidth + kImageIDBitWidth);

  return std::unique_ptr<FactoryHWIDProcessorImpl>(new FactoryHWIDProcessorImpl(
      *encoding_pattern, hwid_component_bits, spec->encoded_fields()));
}

FactoryHWIDProcessorImpl::FactoryHWIDProcessorImpl(
    EncodingPattern& encoding_pattern,
    std::string hwid_component_bits,
    const google::protobuf::RepeatedPtrField<EncodedFields>& encoded_fields)
    : encoding_pattern_(encoding_pattern),
      hwid_component_bits_(hwid_component_bits) {
  for (const auto& field : encoded_fields) {
    encoded_fields_[field.category()] = field;
  }
}

std::optional<CategoryMapping<std::vector<std::string>>>
FactoryHWIDProcessorImpl::DecodeFactoryHWID() const {
  auto component_indexes =
      ExtractEncodedComponentIndex(hwid_component_bits_, encoding_pattern_);
  return ComponentIndexToComponentNames(component_indexes, encoded_fields_);
}

std::set<runtime_probe::ProbeRequest_SupportCategory>
FactoryHWIDProcessorImpl::GetSkipZeroBitCategories() const {
  std::set<runtime_probe::ProbeRequest_SupportCategory> skip_categories;
  for (const auto& first_zero_bit : encoding_pattern_.first_zero_bits()) {
    if (ShouldSkipZeroBitCategory(first_zero_bit.zero_bit_position(),
                                  hwid_component_bits_)) {
      skip_categories.insert(first_zero_bit.category());
    }
  }
  return skip_categories;
}

}  // namespace hardware_verifier
