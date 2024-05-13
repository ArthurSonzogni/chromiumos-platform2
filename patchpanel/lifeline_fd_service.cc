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
    base::OnceClosure on_lifeline_fd_closed,
    std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher)
    : lifeline_fd(std::move(lifeline_fd)),
      on_lifeline_fd_closed(std::move(on_lifeline_fd_closed)),
      watcher(std::move(watcher)) {}

std::vector<int> LifelineFDService::get_lifeline_fds_for_testing() {
  std::vector<int> fds;
  for (const auto& [fd, _] : lifeline_fds_) {
    fds.push_back(fd);
  }
  return fds;
}

base::ScopedClosureRunner LifelineFDService::AddLifelineFD(
    base::ScopedFD client_fd, base::OnceClosure on_lifeline_fd_closed) {
  if (!client_fd.is_valid()) {
    LOG(ERROR) << __func__ << ": Invalid client file descriptor";
    return base::ScopedClosureRunner();
  }

  // Dup the client fd into our own: this guarantees that the fd number will
  // be stable and tied to the actual kernel resources used by the client.
  // The duped fd will be watched for read events. The original fd is discarded.
  base::ScopedFD lifeline_fd = base::ScopedFD(dup(client_fd.get()));
  if (!lifeline_fd.is_valid()) {
    PLOG(ERROR) << __func__ << ": dup() failed";
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
                            std::move(on_lifeline_fd_closed),
                            std::move(watcher));

  return base::ScopedClosureRunner(
      base::BindOnce(&LifelineFDService::DeleteLifelineFD,
                     weak_factory_.GetWeakPtr(), /*is_autoclose=*/false, fd));
}

void LifelineFDService::DeleteLifelineFD(bool is_autoclose, int lifeline_fd) {
  // Check if |lifeline_fd| is still registered. Double DeleteLifelineFD for
  // the same fd is expected to happen when the local service that registered
  // the client file descriptor deletes its ScopedClosureRunner.
  auto it = lifeline_fds_.find(lifeline_fd);
  if (it == lifeline_fds_.end()) {
    return;
  }

  base::OnceClosure callback = std::move(it->second.on_lifeline_fd_closed);
  lifeline_fds_.erase(it);
  // Only run |on_lifeline_fd_closed| if |lifeline_fd| was remotely invalidated.
  if (is_autoclose) {
    std::move(callback).Run();
  }
}

}  // namespace patchpanel
