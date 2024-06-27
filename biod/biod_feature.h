// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_BIOD_FEATURE_H_
#define BIOD_BIOD_FEATURE_H_

#include <memory>

#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>

#include "biod/updater/firmware_selector.h"
#include "featured/feature_library.h"

namespace biod {

class BiodFeature {
 public:
  explicit BiodFeature(
      const scoped_refptr<dbus::Bus>& bus,
      feature::PlatformFeaturesInterface* feature_lib,
      std::unique_ptr<updater::FirmwareSelectorInterface> selector);
  BiodFeature(const BiodFeature&) = delete;
  BiodFeature& operator=(const BiodFeature&) = delete;

 private:
  void CheckFeatures();
  void AllowBetaFirmware(bool enabled);

  scoped_refptr<dbus::Bus> bus_;
  feature::PlatformFeaturesInterface* feature_lib_ = nullptr;
  std::unique_ptr<updater::FirmwareSelectorInterface> selector_;
};

}  // namespace biod

#endif  // BIOD_BIOD_FEATURE_H_
