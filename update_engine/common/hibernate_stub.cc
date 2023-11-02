//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

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
