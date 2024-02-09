// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_HIBERNATE_STUB_H_
#define UPDATE_ENGINE_COMMON_HIBERNATE_STUB_H_

#include <string>

#include "update_engine/common/hibernate_interface.h"

namespace chromeos_update_engine {

// An implementation of the DlcServiceInterface that does nothing.
class HibernateStub : public HibernateInterface {
 public:
  HibernateStub() = default;
  HibernateStub(const HibernateStub&) = delete;
  HibernateStub& operator=(const HibernateStub&) = delete;

  ~HibernateStub() = default;

  bool IsResuming() override;
  bool AbortResume(const std::string& reason) override;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_HIBERNATE_STUB_H_
