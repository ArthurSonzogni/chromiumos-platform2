// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <base/callback.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <dbus/bus.h>

#include "featured/feature_library.h"

const struct Feature kCrOSLateBootMyAwesomeFeature = {
    .name = "CrOSLateBootMyAwesomeFeature",
    .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

void EnabledCallback(base::RepeatingClosure quit_closure, bool enabled) {
  LOG(INFO) << "Enabled? " << enabled;
  quit_closure.Run();
}

int main(int argc, char* argv[]) {
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher(task_executor.task_runner());

  base::RunLoop run_loop;
  auto quit_closure = run_loop.QuitClosure();

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));

  std::unique_ptr<feature::PlatformFeatures> feature_lib =
      feature::PlatformFeatures::New(bus);
  feature_lib->IsEnabled(kCrOSLateBootMyAwesomeFeature,
                         base::BindOnce(&EnabledCallback, quit_closure));
  run_loop.Run();
}
