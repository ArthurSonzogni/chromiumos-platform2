// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/lifeline_fd_service.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>

namespace patchpanel {

LifelineFDService::LifelineFDService() = default;

LifelineFDService::~LifelineFDService() = default;

LifelineFDService::LifelineFDInfo::LifelineFDInfo(
    base::ScopedFD lifeline_fd,
    base::OnceClosure on_lifeline_fd_event,
    std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher)
    : lifeline_fd(std::move(lifeline_fd)),
      on_lifeline_fd_event(std::move(on_lifeline_fd_event)),
      watcher(std::move(watcher)) {}

std::vector<int> LifelineFDService::get_lifeline_fds_for_testing() {
  std::vector<int> fds;
  for (const auto& [fd, _] : lifeline_fds_) {
    fds.push_back(fd);
  }
  return fds;
}

base::ScopedClosureRunner LifelineFDService::AddLifelineFD(
    base::ScopedFD lifeline_fd, base::OnceClosure on_lifeline_fd_event) {
  if (!lifeline_fd.is_valid()) {
    LOG(ERROR) << __func__ << ": Invalid client file descriptor";
    return base::ScopedClosureRunner();
  }

  int fd = lifeline_fd.get();
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher =
      base::FileDescriptorWatcher::WatchReadable(
          fd, base::BindRepeating(&LifelineFDService::DeleteLifelineFD,
                                  // The callback will not outlive the object.
                                  base::Unretained(this), /*is_autoclose=*/true,
                                  fd));
  lifeline_fds_.try_emplace(fd, std::move(lifeline_fd),
                            std::move(on_lifeline_fd_event),
                            std::move(watcher));

  return base::ScopedClosureRunner(
      base::BindOnce(&LifelineFDService::DeleteLifelineFD,
                     weak_factory_.GetWeakPtr(), /*is_autoclose=*/false, fd));
}

void LifelineFDService::DeleteLifelineFD(bool is_autoclose, int lifeline_fd) {
  // Check if |lifeline_fd| is still registered. Double DeleteLifelineFD calls
  // for the same fd is expected to happen when the local service that
  // registered the client file descriptor deletes its ScopedClosureRunner after
  // being notified.
  auto it = lifeline_fds_.find(lifeline_fd);
  if (it == lifeline_fds_.end()) {
    return;
  }

  base::OnceClosure callback = std::move(it->second.on_lifeline_fd_event);
  // Only run |on_lifeline_fd_event| if |lifeline_fd| was remotely invalidated.
  if (is_autoclose) {
    std::move(callback).Run();
  }
  // Destroy the LifelineFDInfo entry and close |lifeline_fd| after the
  // |on_lifeline_fd_event| has run.
  lifeline_fds_.erase(it);
}

}  // namespace patchpanel
