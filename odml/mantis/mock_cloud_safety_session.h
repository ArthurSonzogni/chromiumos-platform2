// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_MOCK_CLOUD_SAFETY_SESSION_H_
#define ODML_MANTIS_MOCK_CLOUD_SAFETY_SESSION_H_

#include <string>
#include <utility>

#include <gmock/gmock.h>

namespace mantis {

class MockCloudSafetySession : public cros_safety::mojom::CloudSafetySession {
 public:
  MockCloudSafetySession() = default;

  void AddReceiver(
      mojo::PendingReceiver<cros_safety::mojom::CloudSafetySession> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

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

}  // namespace mantis

#endif  // ODML_MANTIS_MOCK_CLOUD_SAFETY_SESSION_H_
