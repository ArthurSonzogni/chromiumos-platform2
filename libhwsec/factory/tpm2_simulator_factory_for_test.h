// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FACTORY_TPM2_SIMULATOR_FACTORY_FOR_TEST_H_
#define LIBHWSEC_FACTORY_TPM2_SIMULATOR_FACTORY_FOR_TEST_H_

#include <memory>
#include <utility>

#include "libhwsec/factory/factory_impl.h"
#include "libhwsec/hwsec_export.h"
#include "libhwsec/structures/threading_mode.h"

namespace hwsec {

// Forward declarations
class MiddlewareOwner;
class MockBackend;
class Proxy;

// A TPM2 simulator factory implementation for testing.
//
// The default mode will run the middleware on current task runner, but that
// need to be used carefully in multi-thread environment.
//
// Example usage:
//   Tpm2SimulatorFactoryForTest factory;
//   StatusOr<bool> ready = factory.GetCryptohomeFrontend()->IsReady();

class HWSEC_EXPORT Tpm2SimulatorFactoryForTestData {
 protected:
  explicit Tpm2SimulatorFactoryForTestData(std::unique_ptr<Proxy> proxy);
  ~Tpm2SimulatorFactoryForTestData();

  std::unique_ptr<Proxy> proxy_;
  MockBackend* mock_backend_ptr_;
};

class HWSEC_EXPORT Tpm2SimulatorFactoryForTest
    : public Tpm2SimulatorFactoryForTestData,
      public FactoryImpl {
 public:
  explicit Tpm2SimulatorFactoryForTest(
      ThreadingMode mode = ThreadingMode::kCurrentThread);
  ~Tpm2SimulatorFactoryForTest() override;

  MockBackend& GetMockBackend() { return *mock_backend_ptr_; }
};

}  // namespace hwsec

#endif  // LIBHWSEC_FACTORY_TPM2_SIMULATOR_FACTORY_FOR_TEST_H_
