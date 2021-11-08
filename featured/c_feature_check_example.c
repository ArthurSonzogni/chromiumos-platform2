// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdio.h>

#include "c_feature_library.h"

const struct Feature kCrOSLateBootMyAwesomeFeature = {
    .name = "CrOSLateBootMyAwesomeFeature",
    .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

int main(int argc, char* argv[]) {
  CFeatureLibrary lib = CFeatureLibraryNew();
  printf("%d\n",
         CFeatureLibraryIsEnabledBlocking(lib, &kCrOSLateBootMyAwesomeFeature));
  CFeatureLibraryDelete(lib);
}
