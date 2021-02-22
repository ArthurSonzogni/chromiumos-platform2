// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/scoped_ns.h"

#include <fcntl.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <string>

namespace patchpanel {

ScopedNS::ScopedNS(pid_t pid, Type type) : valid_(false) {
  std::string current_ns_path;
  std::string target_ns_path;
  switch (type) {
    case Mount:
      nstype_ = CLONE_NEWNS;
      current_ns_path = "/proc/self/ns/mnt";
      target_ns_path = "/proc/" + std::to_string(pid) + "/ns/mnt";
      break;
    case Network:
      nstype_ = CLONE_NEWNET;
      current_ns_path = "/proc/self/ns/net";
      target_ns_path = "/proc/" + std::to_string(pid) + "/ns/net";
      break;
    default:
      LOG(ERROR) << "Unsupported namespace type " << type;
      return;
  }

  ns_fd_.reset(open(target_ns_path.c_str(), O_RDONLY | O_CLOEXEC));
  if (!ns_fd_.is_valid()) {
    PLOG(ERROR) << "Could not open namespace " << target_ns_path;
    return;
  }
  self_fd_.reset(open(current_ns_path.c_str(), O_RDONLY | O_CLOEXEC));
  if (!self_fd_.is_valid()) {
    PLOG(ERROR) << "Could not open host namespace " << current_ns_path;
    return;
  }
  if (setns(ns_fd_.get(), nstype_) != 0) {
    PLOG(ERROR) << "Could not enter namespace " << target_ns_path;
    return;
  }
  valid_ = true;
}

ScopedNS::~ScopedNS() {
  if (valid_) {
    if (setns(self_fd_.get(), nstype_) != 0)
      PLOG(FATAL) << "Could not re-enter host namespace type " << nstype_;
  }
}

}  // namespace patchpanel
