// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "featured/c_feature_library.h"

#include <dbus/bus.h>

#include "featured/feature_library.h"

extern "C" CFeatureLibrary CFeatureLibraryNew() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));

  return reinterpret_cast<CFeatureLibrary>(
      feature::PlatformFeatures::New(bus).release());
}

extern "C" void CFeatureLibraryDelete(CFeatureLibrary handle) {
  auto* library = reinterpret_cast<feature::PlatformFeaturesInterface*>(handle);
  library->ShutdownBus();
  delete library;
}

extern "C" int CFeatureLibraryIsEnabledBlocking(
    CFeatureLibrary handle, const struct VariationsFeature* const feature) {
  auto* library = reinterpret_cast<feature::PlatformFeaturesInterface*>(handle);
  return library->IsEnabledBlocking(*feature);
}

extern "C" CFeatureLibrary FakeCFeatureLibraryNew() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));

  return reinterpret_cast<CFeatureLibrary>(
      new feature::FakePlatformFeatures(bus));
}

extern "C" void FakeCFeatureLibrarySetEnabled(CFeatureLibrary handle,
                                              const char* const feature,
                                              int enabled) {
  auto* library = dynamic_cast<feature::FakePlatformFeatures*>(
      reinterpret_cast<feature::PlatformFeaturesInterface*>(handle));
  library->SetEnabled(feature, enabled);
}

extern "C" void FakeCFeatureLibraryClearEnabled(CFeatureLibrary handle,
                                                const char* const feature) {
  auto* library = dynamic_cast<feature::FakePlatformFeatures*>(
      reinterpret_cast<feature::PlatformFeaturesInterface*>(handle));
  library->ClearEnabled(feature);
}
