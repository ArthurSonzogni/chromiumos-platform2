// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_LIFELINE_FD_SERVICE_H_
#define PATCHPANEL_LIFELINE_FD_SERVICE_H_

#include <map>
#include <memory>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>

namespace patchpanel {

// Service tracking file descriptors committed by DBus clients. A client file
// descriptor is registered with AddLifelineFD and implicitly unregistered with
// DeleteLifelineFD. When a client file descriptor becomes invalid (client
// process closed the file descriptor on their end), the callback provided by
// the caller is invoked and unregistration is automatically triggered. No
// further cleanup is necessary from the original caller. DeleteLifelineFD does
// not need to be called explicitly, instead it is sufficient to destroy the
// ScopedClosureRunner object returned by AddLifelineFD to the caller.
class LifelineFDService {
 public:
  LifelineFDService();
  LifelineFDService(const LifelineFDService&) = delete;
  LifelineFDService& operator=(const LifelineFDService&) = delete;
  ~LifelineFDService();

  // Register |lifeline_fd| for read events and trigger |on_lifeline_fd_event|
  // when an event happens on |lifeline_fd|. Returns a ScopedClosureRunner that
  // allows the caller to unregister early |lifeline_fd| and cancel
  // |on_lifeline_fd_event|, or return an invalid ScopedClosureRunner if the
  // registration failed. It is guaranteed that |lifeline_fd| is not closed
  // before the caller's |on_lifeline_fd_event| is invoked or before the caller
  // discards the returned ScopedClosureRunner. This allows the caller to use
  // the lifeline_fd int value as a stable key in conjunction with the lifeline
  // FD service.
  base::ScopedClosureRunner AddLifelineFD(
      base::ScopedFD lifeline_fd, base::OnceClosure on_lifeline_fd_event);

  std::vector<int> get_lifeline_fds_for_testing();

 private:
  // Helper struct to track the file descriptors committed by DBus clients along
  // the local callbacks that should be triggered when these file descriptors
  // get invalidated remotely.
  struct LifelineFDInfo {
    LifelineFDInfo(
        base::ScopedFD lifeline_fd,
        base::OnceClosure on_lifeline_fd_event,
        std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher);

    // The file descriptor committed by the DBus client and registered by the
    // local service to this LifelineFDService.
    base::ScopedFD lifeline_fd;
    // A callback registered by the local service along side |lifeline_fd|. Used
    // to notify the local service when a |lifeline_fd| is invalidated.
    base::OnceClosure on_lifeline_fd_event;
    // Watcher for being notified when the DBus client remotely invalidates
    // |lifeline_fd|. The watcher must be closed before |lifeline_fd| is closed.
    std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher;
  };

  // Unregister |lifeline_fd| and run its associated callback if |is_autoclose|
  // is true.
  void DeleteLifelineFD(bool is_autoclose, int lifeline_fd);

  // For each fd committed through a patchpanel's DBus API and tracked with a
  // lifeline FD, keep track of that file descriptor, of its file descriptor
  // watcher, and of the callback registered by the local service handling the
  // DBus RPC.
  std::map<int, LifelineFDInfo> lifeline_fds_;

  base::WeakPtrFactory<LifelineFDService> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_LIFELINE_FD_SERVICE_H_
