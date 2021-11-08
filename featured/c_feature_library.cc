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

void CFeatureLibraryDelete(CFeatureLibrary handle) {
  auto* library = reinterpret_cast<feature::PlatformFeatures*>(handle);
  library->ShutdownBus();
  delete library;
}

extern "C" int CFeatureLibraryIsEnabledBlocking(
    CFeatureLibrary handle, const struct Feature* const feature) {
  auto* library = reinterpret_cast<feature::PlatformFeatures*>(handle);
  return library->IsEnabledBlocking(*feature);
}
