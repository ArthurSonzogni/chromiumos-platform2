// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_DELEGATE_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_DELEGATE_IMPL_H_

#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/mojom/delegate.mojom.h"

namespace diagnostics {

class DelegateImpl : public ash::cros_healthd::mojom::Delegate {
 public:
  DelegateImpl();
  DelegateImpl(const DelegateImpl&) = delete;
  DelegateImpl& operator=(const DelegateImpl&) = delete;
  ~DelegateImpl() override;

  // ash::cros_healthd::mojom::Delegate overrides.
  void GetFingerprintFrame(
      ash::cros_healthd::mojom::FingerprintCaptureType type,
      GetFingerprintFrameCallback callback) override;
  void GetFingerprintInfo(GetFingerprintInfoCallback callback) override;
  void SetLedColor(ash::cros_healthd::mojom::LedName name,
                   ash::cros_healthd::mojom::LedColor color,
                   SetLedColorCallback callback) override;
  void ResetLedColor(ash::cros_healthd::mojom::LedName name,
                     ResetLedColorCallback callback) override;
  void MonitorAudioJack(
      mojo::PendingRemote<ash::cros_healthd::mojom::AudioJackObserver> observer)
      override;
  void MonitorTouchpad(
      mojo::PendingRemote<ash::cros_healthd::mojom::TouchpadObserver> observer)
      override;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_DELEGATE_IMPL_H_
