// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_EVDEV_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_EVDEV_UTILS_H_

#include <libevdev/libevdev.h>
#include <memory>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

class EvdevUtil {
 public:
  class Delegate {
   public:
    // Check if |dev| is the target device.
    virtual bool IsTarget(libevdev* dev) = 0;
    // Deal with the events and report to the caller through observer.
    virtual void FireEvent(const input_event& event, libevdev* dev) = 0;
    // Initialization fail. Delegate should reset the observer.
    virtual void InitializationFail() = 0;
    // Collect properties here and report to the caller through observer.
    virtual void ReportProperties(libevdev* dev) = 0;
  };

  struct ScopedLibevdevDeleter {
    inline void operator()(libevdev* x) const { libevdev_free(x); }
  };

  using ScopedLibevdev = std::unique_ptr<libevdev, ScopedLibevdevDeleter>;

  explicit EvdevUtil(Delegate* delegate);
  EvdevUtil(const EvdevUtil& oth) = delete;
  EvdevUtil(EvdevUtil&& oth) = delete;
  virtual ~EvdevUtil();

 private:
  void Initialize();
  bool Initialize(const base::FilePath& path);

  // When |fd_| is readable, it reads event from it and tries to fire event
  // through |FireEvent|.
  void OnEvdevEvent();

  // The fd of opened evdev node.
  base::ScopedFD fd_;
  // The watcher to monitor if the |fd_| is readable.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
  // The libevdev device object.
  ScopedLibevdev dev_;
  // Delegate to implement dedicated behaviors for different evdev devices.
  Delegate* const delegate_;
};

class EvdevAudioJackObserver final : public EvdevUtil::Delegate {
 public:
  explicit EvdevAudioJackObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::AudioJackObserver>
          observer);

  // EvdevUtil::Delegate overrides.
  bool IsTarget(libevdev* dev) override;
  void FireEvent(const input_event& event, libevdev* dev) override;
  void InitializationFail() override;
  void ReportProperties(libevdev* dev) override;

 private:
  EvdevUtil evdev_util_{this};
  mojo::Remote<ash::cros_healthd::mojom::AudioJackObserver> observer_;
};

class EvdevTouchpadObserver final : public EvdevUtil::Delegate {
 public:
  explicit EvdevTouchpadObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::TouchpadObserver> observer);

  // EvdevUtil::Delegate overrides.
  bool IsTarget(libevdev* dev) override;
  void FireEvent(const input_event& event, libevdev* dev) override;
  void InitializationFail() override;
  void ReportProperties(libevdev* dev) override;

 private:
  mojo::Remote<ash::cros_healthd::mojom::TouchpadObserver> observer_;
  EvdevUtil evdev_util_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_EVDEV_UTILS_H_
