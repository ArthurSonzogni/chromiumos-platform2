// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_IMPL_H_
#define HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_IMPL_H_

#include <memory>

#include "hardware_verifier/encoding_spec_loader.h"
#include "hardware_verifier/factory_hwid_processor.h"
#include "hardware_verifier/hardware_verifier.pb.h"

namespace hardware_verifier {

class FactoryHWIDProcessorImpl : FactoryHWIDProcessor {
 public:
  FactoryHWIDProcessorImpl(const FactoryHWIDProcessorImpl&) = delete;
  FactoryHWIDProcessorImpl& operator=(const FactoryHWIDProcessorImpl&) = delete;

  // Factory method to create a |FactoryHWIDProcessorImpl|.
  // Returns |nullptr| if initialization fails.
  static std::unique_ptr<FactoryHWIDProcessorImpl> Create(
      const EncodingSpecLoader& encoding_spec_loader);

 private:
  explicit FactoryHWIDProcessorImpl(
      std::unique_ptr<EncodingSpec> encoding_spec);
  std::unique_ptr<EncodingSpec> encoding_spec_;
  CategoryMapping<const EncodedFields*> encoded_fields_;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_FACTORY_HWID_PROCESSOR_IMPL_H_
