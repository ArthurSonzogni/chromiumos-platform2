// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crossystem.h"

#include <base/no_destructor.h>
#include <brillo/crossystem/crossystem.h>

namespace {

brillo::Crossystem* GetDefaultInstance() {
  static base::NoDestructor<brillo::CrossystemImpl> instance;
  return instance.get();
}

brillo::Crossystem* shared_instance = nullptr;

}  // namespace

namespace crossystem {

brillo::Crossystem* GetInstance() {
  if (shared_instance == nullptr)
    shared_instance = GetDefaultInstance();
  return shared_instance;
}

brillo::Crossystem* ReplaceInstanceForTest(brillo::Crossystem* instance) {
  auto original_instance =
      shared_instance == nullptr ? GetDefaultInstance() : shared_instance;
  shared_instance = instance;
  return original_instance;
}

}  // namespace crossystem
