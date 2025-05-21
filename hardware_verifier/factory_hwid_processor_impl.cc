// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/factory_hwid_processor_impl.h"

#include <utility>

#include <base/check.h>
#include <base/logging.h>

#include "hardware_verifier/encoding_spec_loader.h"
#include "hardware_verifier/hardware_verifier.pb.h"

namespace hardware_verifier {

std::unique_ptr<FactoryHWIDProcessorImpl> FactoryHWIDProcessorImpl::Create(
    const EncodingSpecLoader& encoding_spec_loader) {
  auto spec = encoding_spec_loader.Load();
  if (!spec) {
    LOG(ERROR) << "Failed to load encoding spec.";
    return nullptr;
  }
  return std::unique_ptr<FactoryHWIDProcessorImpl>(
      new FactoryHWIDProcessorImpl(std::move(spec)));
}

FactoryHWIDProcessorImpl::FactoryHWIDProcessorImpl(
    std::unique_ptr<EncodingSpec> encoding_spec)
    : encoding_spec_(std::move(encoding_spec)) {
  CHECK(encoding_spec_ != nullptr);

  for (const auto& field : encoding_spec_->encoded_fields()) {
    encoded_fields_[field.category()] = &field;
  }
}

}  // namespace hardware_verifier
