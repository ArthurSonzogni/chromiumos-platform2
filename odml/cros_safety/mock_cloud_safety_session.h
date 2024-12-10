// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CROS_SAFETY_MOCK_CLOUD_SAFETY_SESSION_H_
#define ODML_CROS_SAFETY_MOCK_CLOUD_SAFETY_SESSION_H_

#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/mojom/big_buffer.mojom.h"
#include "odml/mojom/cros_safety.mojom.h"

namespace cros_safety {

class MockCloudSafetySession : public cros_safety::mojom::CloudSafetySession {
 public:
  MockCloudSafetySession() = default;

  void AddReceiver(
      mojo::PendingReceiver<cros_safety::mojom::CloudSafetySession> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  void ClearReceivers() { receiver_set_.Clear(); }

  MOCK_METHOD(void,
              ClassifyTextSafety,
              (cros_safety::mojom::SafetyRuleset ruleset,
               const std::string& text,
               ClassifyTextSafetyCallback callback),
              (override));

  MOCK_METHOD(void,
              ClassifyImageSafety,
              (cros_safety::mojom::SafetyRuleset ruleset,
               const std::optional<std::string>& text,
               mojo_base::mojom::BigBufferPtr image,
               ClassifyImageSafetyCallback callback),
              (override));

 private:
  mojo::ReceiverSet<cros_safety::mojom::CloudSafetySession> receiver_set_;
};

}  // namespace cros_safety

#endif  // ODML_CROS_SAFETY_MOCK_CLOUD_SAFETY_SESSION_H_
