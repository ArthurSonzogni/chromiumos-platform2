// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/hibernate_stub.h"

#include <memory>

namespace chromeos_update_engine {

std::unique_ptr<HibernateInterface> CreateHibernateService() {
  return std::make_unique<HibernateStub>();
}

bool HibernateStub::IsResuming() {
  return false;
}

bool HibernateStub::AbortResume(const std::string& reason) {
  return true;
}

}  // namespace chromeos_update_engine
