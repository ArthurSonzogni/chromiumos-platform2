// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/runtime_hwid_utils.h"

#include <base/no_destructor.h>
#include <chromeos/hardware_verifier/runtime_hwid_utils/runtime_hwid_utils.h>
#include <chromeos/hardware_verifier/runtime_hwid_utils/runtime_hwid_utils_impl.h>

namespace {

hardware_verifier::RuntimeHWIDUtils* GetDefaultInstance() {
  static base::NoDestructor<hardware_verifier::RuntimeHWIDUtilsImpl> instance;
  return instance.get();
}

hardware_verifier::RuntimeHWIDUtils* shared_instance = nullptr;

}  // namespace

namespace crash_runtime_hwid_utils {

hardware_verifier::RuntimeHWIDUtils* GetInstance() {
  if (shared_instance == nullptr) {
    shared_instance = GetDefaultInstance();
  }
  return shared_instance;
}

hardware_verifier::RuntimeHWIDUtils* ReplaceInstanceForTest(
    hardware_verifier::RuntimeHWIDUtils* instance) {
  auto original_instance =
      shared_instance == nullptr ? GetDefaultInstance() : shared_instance;
  shared_instance = instance;
  return original_instance;
}

}  // namespace crash_runtime_hwid_utils
