// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CROSSYSTEM_H_
#define CRASH_REPORTER_CROSSYSTEM_H_

#include <brillo/crossystem/crossystem.h>

namespace crossystem {

// Gets the singleton instance of brillo::Crossystem that provides
// functionalities to access and modify the system properties.
brillo::Crossystem* GetInstance();

// Replaces the singleton instance of brillo::Crossystem for testing.
// It returns the old instance before replacing so that the caller can
// replace it back easily.
brillo::Crossystem* ReplaceInstanceForTest(brillo::Crossystem* instance);

}  // namespace crossystem

#endif  // CRASH_REPORTER_CROSSYSTEM_H_
