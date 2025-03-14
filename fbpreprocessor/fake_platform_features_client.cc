// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/fake_platform_features_client.h"

namespace fbpreprocessor {

FakePlatformFeaturesClient::FakePlatformFeaturesClient() : allowed_(true) {}

void FakePlatformFeaturesClient::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void FakePlatformFeaturesClient::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void FakePlatformFeaturesClient::SetFinchEnabled(bool enabled) {
  allowed_ = enabled;
  for (auto& observer : observers_) {
    observer.OnFeatureChanged(allowed_);
  }
}

}  // namespace fbpreprocessor
