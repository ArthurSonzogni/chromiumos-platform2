// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_FAKE_PLATFORM_FEATURES_CLIENT_H_
#define FBPREPROCESSOR_FAKE_PLATFORM_FEATURES_CLIENT_H_

#include <base/observer_list.h>

#include "fbpreprocessor/platform_features_client.h"

namespace fbpreprocessor {

class FakePlatformFeaturesClient : public PlatformFeaturesClientInterface {
 public:
  FakePlatformFeaturesClient();
  ~FakePlatformFeaturesClient() override = default;

  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  bool FirmwareDumpsAllowedByFinch() const override { return allowed_; }

  // Tests can call |SetFinchEnabled()| to simulate the feature being
  // enabled/disabled. Default is enabled.
  void SetFinchEnabled(bool enabled);

 private:
  bool allowed_;
  base::ObserverList<Observer>::Unchecked observers_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_FAKE_PLATFORM_FEATURES_CLIENT_H_
