// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_IMPL_H_
#define HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_IMPL_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <google/protobuf/repeated_ptr_field.h>

#include "hardware_verifier/factory_hwid_processor.h"
#include "hardware_verifier/hardware_verifier.pb.h"

namespace hardware_verifier {

class FactoryHWIDProcessorImpl : public FactoryHWIDProcessor {
 public:
  FactoryHWIDProcessorImpl(const FactoryHWIDProcessorImpl&) = delete;
  FactoryHWIDProcessorImpl& operator=(const FactoryHWIDProcessorImpl&) = delete;

  // Factory method to create a |FactoryHWIDProcessorImpl|.
  // Returns |nullptr| if initialization fails.
  static std::unique_ptr<FactoryHWIDProcessorImpl> Create(
      const EncodingSpec& encoding_spec);

  std::optional<CategoryMapping<std::vector<std::string>>> DecodeFactoryHWID()
      const override;

  std::set<runtime_probe::ProbeRequest_SupportCategory>
  GetSkipZeroBitCategories() const override;

  std::optional<std::string> GenerateMaskedFactoryHWID() const override;

 private:
  explicit FactoryHWIDProcessorImpl(
      EncodingPattern& encoding_pattern,
      std::string& hwid_decode_bits,
      const google::protobuf::RepeatedPtrField<EncodedFields>& encoded_fields);
  EncodingPattern encoding_pattern_;
  CategoryMapping<EncodedFields> encoded_fields_;
  std::string hwid_decode_bits_;

  std::string GetMaskedComponentBits() const;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_IMPL_H_
