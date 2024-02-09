// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_HIBERNATE_INTERFACE_H_
#define UPDATE_ENGINE_COMMON_HIBERNATE_INTERFACE_H_

#include <memory>
#include <string>

namespace chromeos_update_engine {

// The abstract hibernate interface defines the interaction with and information
// about the system's hibernation state.
class HibernateInterface {
 public:
  HibernateInterface(const HibernateInterface&) = delete;
  HibernateInterface& operator=(const HibernateInterface&) = delete;

  virtual ~HibernateInterface() = default;

  // Returns true if the system is resuming from hibernate.
  virtual bool IsResuming() = 0;

  // Aborts a resume from hibernate, if one is in progress.
  virtual bool AbortResume(const std::string& reason) = 0;

 protected:
  HibernateInterface() = default;
};

// This factory function creates a new HibernateInterface instance for the
// current platform.
std::unique_ptr<HibernateInterface> CreateHibernateService();

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_HIBERNATE_INTERFACE_H_
