// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/port_tracker.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

namespace {
const size_t kMaxInterfaceNameLen = 16;
const int kMaxEvents = 10;
const int64_t kLifelineIntervalSeconds = 5;
const int kInvalidHandle = -1;
}

namespace permission_broker {

PortTracker::PortTracker(org::chromium::FirewalldProxyInterface* firewalld)
    : task_runner_{base::MessageLoopForIO::current()->task_runner()},
      epfd_{kInvalidHandle},
      vpn_lifeline_{kInvalidHandle},
      firewalld_{firewalld} {}

// Test-only.
PortTracker::PortTracker(scoped_refptr<base::SequencedTaskRunner> task_runner,
                         org::chromium::FirewalldProxyInterface* firewalld)
    : task_runner_{task_runner},
      epfd_{kInvalidHandle},
      vpn_lifeline_{kInvalidHandle},
      firewalld_{firewalld} {}

PortTracker::~PortTracker() {
  if (epfd_ >= 0) {
    close(epfd_);
  }
}

bool PortTracker::ProcessTcpPort(uint16_t port,
                                 const std::string& iface,
                                 int dbus_fd) {
  // |iface| should be a short string.
  if (iface.length() >= kMaxInterfaceNameLen) {
    LOG(ERROR) << "Interface name '" << iface << "' is too long";
    return false;
  }

  Hole hole = std::make_pair(port, iface);
  if (tcp_fds_.find(hole) != tcp_fds_.end()) {
    // This could potentially happen when a requesting process has just been
    // restarted but scheduled lifeline FD check hasn't been performed yet, so
    // we might have stale descriptors around. Force the FD check to see
    // if they will be removed now.
    CheckLifelineFds(false);

    // Then try again. If this still fails, we know it's an invalid request.
    if (tcp_fds_.find(hole) != tcp_fds_.end()) {
      LOG(ERROR) << "Hole already punched";
      return false;
    }
  }

  // We use |lifeline_fd| to track the lifetime of the process requesting
  // port access.
  int lifeline_fd = AddLifelineFd(dbus_fd);
  if (lifeline_fd < 0) {
    LOG(ERROR) << "Tracking lifeline fd for TCP port " << port << " failed";
    return false;
  }

  // Track the hole.
  tcp_holes_[lifeline_fd] = hole;
  tcp_fds_[hole] = lifeline_fd;

  bool success;
  firewalld_->PunchTcpHole(port, iface, &success, nullptr);
  if (!success) {
    // If we fail to punch the hole in the firewall, stop tracking the lifetime
    // of the process.
    LOG(ERROR) << "Failed to punch hole for TCP port " << port;
    DeleteLifelineFd(lifeline_fd);
    tcp_holes_.erase(lifeline_fd);
    tcp_fds_.erase(hole);
    return false;
  }
  return true;
}

bool PortTracker::ProcessUdpPort(uint16_t port,
                                 const std::string& iface,
                                 int dbus_fd) {
  // |iface| should be a short string.
  if (iface.length() >= kMaxInterfaceNameLen) {
    LOG(ERROR) << "Interface name '" << iface << "' is too long";
    return false;
  }

  Hole hole = std::make_pair(port, iface);
  if (udp_fds_.find(hole) != udp_fds_.end()) {
    // This could potentially happen when a requesting process has just been
    // restarted but scheduled lifeline FD check hasn't been performed yet, so
    // we might have stale descriptors around. Force the FD check to see
    // if they will be removed now.
    CheckLifelineFds(false);

    // Then try again. If this still fails, we know it's an invalid request.
    if (udp_fds_.find(hole) != udp_fds_.end()) {
      LOG(ERROR) << "Hole already punched";
      return false;
    }
  }

  // We use |lifeline_fd| to track the lifetime of the process requesting
  // port access.
  int lifeline_fd = AddLifelineFd(dbus_fd);
  if (lifeline_fd < 0) {
    LOG(ERROR) << "Tracking lifeline fd for UDP port " << port << " failed";
    return false;
  }

  // Track the hole.
  udp_holes_[lifeline_fd] = hole;
  udp_fds_[hole] = lifeline_fd;

  bool success;
  firewalld_->PunchUdpHole(port, iface, &success, nullptr);
  if (!success) {
    // If we fail to punch the hole in the firewall, stop tracking the lifetime
    // of the process.
    LOG(ERROR) << "Failed to punch hole for UDP port " << port;
    DeleteLifelineFd(lifeline_fd);
    udp_holes_.erase(lifeline_fd);
    udp_fds_.erase(hole);
    return false;
  }
  return true;
}

bool PortTracker::ReleaseTcpPort(uint16_t port, const std::string& iface) {
  Hole hole = std::make_pair(port, iface);
  auto p = tcp_fds_.find(hole);
  if (p == tcp_fds_.end()) {
    LOG(ERROR) << "Not tracking TCP port " << port << " on interface '" << iface
               << "'";
    return false;
  }

  int fd = p->second;
  bool plugged = PlugFirewallHole(fd);
  bool deleted = DeleteLifelineFd(fd);
  // PlugFirewallHole() prints an error message on failure,
  // but DeleteLifelineFd() does not, and even if it did,
  // we mock it out in tests.
  if (!deleted) {
    LOG(ERROR) << "Failed to delete file descriptor " << fd
               << " from epoll instance";
  }
  return plugged && deleted;
}

bool PortTracker::ReleaseUdpPort(uint16_t port, const std::string& iface) {
  Hole hole = std::make_pair(port, iface);
  auto p = udp_fds_.find(hole);
  if (p == udp_fds_.end()) {
    LOG(ERROR) << "Not tracking UDP port " << port << " on interface '" << iface
               << "'";
    return false;
  }

  int fd = p->second;
  bool plugged = PlugFirewallHole(fd);
  bool deleted = DeleteLifelineFd(fd);
  // PlugFirewallHole() prints an error message on failure,
  // but DeleteLifelineFd() does not, and even if it did,
  // we mock it out in tests.
  if (!deleted) {
    LOG(ERROR) << "Failed to delete file descriptor " << fd
               << " from epoll instance";
  }
  return plugged && deleted;
}

bool PortTracker::ProcessVpnSetup(
    const std::vector<std::string>& usernames,
    const std::string& interface,
    int dbus_fd) {
  if (vpn_lifeline_ != kInvalidHandle) {
    LOG(ERROR) << "Already tracking a VPN lifeline";
    return false;
  }

  // We use |lifeline_fd| to track the lifetime of the process requesting
  // VPN setup.
  int lifeline_fd = AddLifelineFd(dbus_fd);
  if (lifeline_fd < 0) {
    LOG(ERROR) << "Tracking lifeline fd for VPN failed";
    return false;
  }

  bool success;
  firewalld_->RequestVpnSetup(usernames, interface, &success, nullptr);
  if (!success) {
    LOG(ERROR) << "Failed to set up rules for VPN";
    DeleteVpnRules();
    DeleteLifelineFd(lifeline_fd);
    return false;
  }
  vpn_usernames_ = usernames;
  vpn_interface_ = interface;
  vpn_lifeline_ = lifeline_fd;

  return true;
}

bool PortTracker::DeleteVpnRules() {
  bool success = true;

  firewalld_->RemoveVpnSetup(vpn_usernames_, vpn_interface_, &success, nullptr);
  vpn_usernames_.clear();
  vpn_interface_.clear();
  vpn_lifeline_ = kInvalidHandle;

  return success;
}

bool PortTracker::RemoveVpnSetup() {
  if (vpn_lifeline_ == kInvalidHandle) {
    LOG(ERROR) << "RemoveVpnSetup called without VPN rules set";
    return false;
  }
  DeleteLifelineFd(vpn_lifeline_);
  if (!DeleteVpnRules()) {
    LOG(ERROR) << "Failed to delete VPN rules";
    return false;
  }
  return true;
}

int PortTracker::AddLifelineFd(int dbus_fd) {
  if (!InitializeEpollOnce()) {
    return kInvalidHandle;
  }
  int fd = dup(dbus_fd);

  struct epoll_event epevent;
  epevent.events = EPOLLIN;  // EPOLLERR | EPOLLHUP are always waited for.
  epevent.data.fd = fd;
  LOG(INFO) << "Adding file descriptor " << fd << " to epoll instance";
  if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &epevent) != 0) {
    PLOG(ERROR) << "epoll_ctl(EPOLL_CTL_ADD)";
    return kInvalidHandle;
  }

  // If this is the first port request, start lifeline checks.
  if ((tcp_holes_.size() + udp_holes_.size() == 0) &&
      vpn_lifeline_ == kInvalidHandle) {
    LOG(INFO) << "Starting lifeline checks";
    ScheduleLifelineCheck();
  }

  return fd;
}

bool PortTracker::DeleteLifelineFd(int fd) {
  if (epfd_ < 0) {
    LOG(ERROR) << "epoll instance not created";
    return false;
  }

  LOG(INFO) << "Deleting file descriptor " << fd << " from epoll instance";
  if (epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, NULL) != 0) {
    PLOG(ERROR) << "epoll_ctl(EPOLL_CTL_DEL)";
    return false;
  }

  // AddLifelineFd() calls dup(), so this function should close the fd.
  // We still return true since at this point the fd has been deleted from the
  // epoll instance.
  if (IGNORE_EINTR(close(fd)) < 0) {
    PLOG(ERROR) << "close(lifeline_fd)";
  }
  return true;
}

void PortTracker::CheckLifelineFds(bool reschedule_check) {
  if (epfd_ < 0) {
    return;
  }
  struct epoll_event epevents[kMaxEvents];
  int nready = epoll_wait(epfd_, epevents, kMaxEvents, 0 /* do not block */);
  if (nready < 0) {
    PLOG(ERROR) << "epoll_wait(0)";
    return;
  }
  if (nready == 0) {
    if (reschedule_check)
      ScheduleLifelineCheck();
    return;
  }

  for (size_t eidx = 0; eidx < (size_t)nready; eidx++) {
    uint32_t events = epevents[eidx].events;
    int fd = epevents[eidx].data.fd;
    if ((events & (EPOLLHUP | EPOLLERR))) {
      // The process that requested this port has died/exited,
      // so we need to plug the hole.
      PlugFirewallHole(fd);
      DeleteLifelineFd(fd);
    }
  }

  if (reschedule_check) {
    // If there's still processes to track, schedule lifeline checks.
    if (tcp_holes_.size() + tcp_holes_.size() > 0 ||
        vpn_lifeline_ != kInvalidHandle) {
      ScheduleLifelineCheck();
    } else {
      LOG(INFO) << "Stopping lifeline checks";
    }
  }
}

void PortTracker::ScheduleLifelineCheck() {
  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::Bind(&PortTracker::CheckLifelineFds, base::Unretained(this), true),
      base::TimeDelta::FromSeconds(kLifelineIntervalSeconds));
}

bool PortTracker::PlugFirewallHole(int fd) {
  bool dbus_sucess = false;
  Hole hole;
  if (tcp_holes_.find(fd) != tcp_holes_.end()) {
    // It was a TCP hole.
    hole = tcp_holes_[fd];
    firewalld_->PlugTcpHole(hole.first, hole.second, &dbus_sucess, nullptr);
    tcp_holes_.erase(fd);
    tcp_fds_.erase(hole);
    if (!dbus_sucess) {
      LOG(ERROR) << "Failed to plug hole for TCP port " << hole.first
                 << " on interface '" << hole.second << "'";
      return false;
    }
  } else if (udp_holes_.find(fd) != udp_holes_.end()) {
    // It was a UDP hole.
    hole = udp_holes_[fd];
    firewalld_->PlugUdpHole(hole.first, hole.second, &dbus_sucess, nullptr);
    udp_holes_.erase(fd);
    udp_fds_.erase(hole);
    if (!dbus_sucess) {
      LOG(ERROR) << "Failed to plug hole for UDP port " << hole.first
                 << " on interface '" << hole.second << "'";
      return false;
    }
  } else if (fd == vpn_lifeline_) {
    if (!DeleteVpnRules()) {
      LOG(ERROR) << "Failed to delete VPN rules";
      return false;
    }
  } else {
    LOG(ERROR) << "File descriptor " << fd << " was not being tracked";
    return false;
  }
  return true;
}

bool PortTracker::InitializeEpollOnce() {
  if (epfd_ < 0) {
    // |size| needs to be > 0, but is otherwise ignored.
    LOG(INFO) << "Creating epoll instance";
    epfd_ = epoll_create(1 /* size */);
    if (epfd_ < 0) {
      PLOG(ERROR) << "epoll_create()";
      return false;
    }
  }
  return true;
}

}  // namespace permission_broker
