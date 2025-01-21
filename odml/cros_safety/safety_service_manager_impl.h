// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_IMPL_H_
#define ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_IMPL_H_

#include <string>

#include <base/gtest_prod_util.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "odml/cros_safety/safety_service_manager.h"
#include "odml/mojom/cros_safety.mojom-shared.h"
#include "odml/mojom/cros_safety_service.mojom.h"

namespace cros_safety {

// The SafetyServiceManagerImpl class requests the CrosSafetyService (registered
// by chrome) from mojo service manager. Internally it manages a single
// CloudSafetySession and OnDeviceSafetySession remote, and handle cases that
// the safety service or session get disconnected or the callbacks are dropped.
class SafetyServiceManagerImpl : public SafetyServiceManager {
 public:
  explicit SafetyServiceManagerImpl(
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
          service_manager);

  void PrepareImageSafetyClassifier(
      base::OnceCallback<void(bool)> callback) override;

  void ClassifyImageSafety(mojom::SafetyRuleset ruleset,
                           const std::optional<std::string>& text,
                           mojo_base::mojom::BigBufferPtr image,
                           ClassifySafetyCallback callback) override;

  void ClassifyTextSafety(mojom::SafetyRuleset ruleset,
                          const std::string& text,
                          ClassifySafetyCallback callback) override;

 private:
  FRIEND_TEST(SafetyServiceManagerImplTest, OnDeviceSafetySessionDisconnected);
  FRIEND_TEST(SafetyServiceManagerImplTest, CloudSafetySessionDisconnected);
  FRIEND_TEST(SafetyServiceManagerImplTest, SafetyServiceDisconnect);

  void OnSafetyServiceDisconnected(uint32_t error, const std::string& message);
  void OnCloudSafetySessionDisconnected(uint32_t error,
                                        const std::string& message);
  void OnOnDeviceSafetySessionDisconnected(uint32_t error,
                                           const std::string& message);

  void OnClassifySafetyDone(ClassifySafetyCallback callback,
                            mojom::SafetyRuleset ruleset,
                            mojom::SafetyClassifierVerdict verdict);
  void EnsureCloudSafetySessionCreated(base::OnceClosure callback);
  void GetCloudSafetySessionDone(base::OnceClosure callback,
                                 mojom::GetCloudSafetySessionResult result);
  void ClassifyImageSafetyInternal(mojom::SafetyRuleset ruleset,
                                   const std::optional<std::string>& text,
                                   mojo_base::mojom::BigBufferPtr image,
                                   ClassifySafetyCallback callback);
  void EnsureOnDeviceSafetySessionCreated(base::OnceClosure callback);
  void GetOnDeviceSafetySessionDone(
      base::OnceClosure callback, mojom::GetOnDeviceSafetySessionResult result);
  void ClassifyTextSafetyInternal(mojom::SafetyRuleset ruleset,
                                  const std::string& text,
                                  ClassifySafetyCallback callback);

  const mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
      service_manager_;

  mojo::Remote<cros_safety::mojom::CrosSafetyService> safety_service_;

  mojo::Remote<cros_safety::mojom::CloudSafetySession> cloud_safety_session_;
  mojo::Remote<cros_safety::mojom::OnDeviceSafetySession>
      on_device_safety_session_;

  base::WeakPtrFactory<SafetyServiceManagerImpl> weak_ptr_factory_{this};
};

}  // namespace cros_safety

#endif  // ODML_CROS_SAFETY_SAFETY_SERVICE_MANAGER_IMPL_H_
