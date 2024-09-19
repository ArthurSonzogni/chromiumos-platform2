// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>

#include <base/check.h>
#include <base/logging.h>
#include <dbus/dbus.h>
#include <perfetto/perfetto.h>

#include "dbus_perfetto_producer/dbus_monitor.h"
#include "dbus_perfetto_producer/perfetto_producer.h"

static volatile sig_atomic_t running = 1;

void SigHandler(int sig) {
  running = 0;
}

// This tool is only supported on shell as explained in README. Running this as
// the daemon may cause an error.
int main(int argc, char** argv) {
  DBusConnection* connection;
  DBusError error;
  DBusBusType type = DBUS_BUS_SYSTEM;
  dbus_error_init(&error);

  int fd[2];
  if (pipe(fd) != 0) {
    LOG(ERROR) << "Failed to create a pipe";
    exit(1);
  }

  int pid = fork();
  if (pid < 0) {
    LOG(ERROR) << "Failed to fork";
    exit(1);
  }

  // Child process (Perfetto Producer)
  if (pid == 0) {
    close(fd[1]);
    connection = dbus_bus_get(type, &error);
    if (!connection) {
      LOG(ERROR) << "Failed to open a connection";
      dbus_error_free(&error);
      exit(1);
    }

    // user data to be passed to a filter function
    Maps maps;

    if (!StoreProcessesNames(connection, &error, maps)) {
      LOG(ERROR) << "StoreProcessNames failed, exiting the program";
      exit(1);
    }

    perfetto::TracingInitArgs args;
    args.backends |= perfetto::kSystemBackend;
    perfetto::Tracing::Initialize(args);
    perfetto::TrackEvent::Register();

    // This function returns only when an error occurs
    PerfettoProducer(connection, &error, maps, fd[0]);

    LOG(ERROR) << "PerfettoProducer failed, exiting the program";
    close(fd[0]);
    exit(1);

    // Parent process (D-Bus Monitor)
  } else {
    signal(SIGINT, SigHandler);

    close(fd[0]);
    connection = dbus_bus_get(type, &error);
    if (!connection) {
      LOG(ERROR) << "Failed to open a connection";
      dbus_error_free(&error);
      exit(1);
    }

    if (!SetupConnection(connection, &error, fd[1])) {
      LOG(ERROR) << "DbusMonitor failed, exiting the program";
      close(fd[1]);
      exit(1);
    }
    LOG(INFO) << "Became a monitor. Start tracing.";

    while (running) {
      if (!dbus_connection_read_write_dispatch(connection, -1)) {
        // The message of disconnection is processed
        LOG(INFO) << "Disconnected";
        close(fd[1]);
        exit(1);
      }
    }

    // The tool is interrupted, which is an expected action to terminate.
    close(fd[1]);
    return 0;
  }
}
