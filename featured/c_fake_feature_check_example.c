// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdio.h>

#include "c_feature_library.h"

const struct VariationsFeature kCrOSLateBootMyAwesomeFeature = {
    .name = "CrOSLateBootMyAwesomeFeature",
    .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

int main(int argc, char* argv[]) {
  CFeatureLibrary lib = FakeCFeatureLibraryNew();

  // Will use default value
  printf("%d\n",
         CFeatureLibraryIsEnabledBlocking(lib, &kCrOSLateBootMyAwesomeFeature));

  // Override to true
  FakeCFeatureLibrarySetEnabled(lib, kCrOSLateBootMyAwesomeFeature.name, 1);
  printf("%d\n",
         CFeatureLibraryIsEnabledBlocking(lib, &kCrOSLateBootMyAwesomeFeature));

  // Override to false
  FakeCFeatureLibrarySetEnabled(lib, kCrOSLateBootMyAwesomeFeature.name, 0);
  printf("%d\n",
         CFeatureLibraryIsEnabledBlocking(lib, &kCrOSLateBootMyAwesomeFeature));

  // Reset to default value
  FakeCFeatureLibraryClearEnabled(lib, kCrOSLateBootMyAwesomeFeature.name);
  printf("%d\n",
         CFeatureLibraryIsEnabledBlocking(lib, &kCrOSLateBootMyAwesomeFeature));

  CFeatureLibraryDelete(lib);
}
