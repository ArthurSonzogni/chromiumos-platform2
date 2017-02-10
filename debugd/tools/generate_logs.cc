// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is meant for debugging use to manually trigger collection of
// debug logs.  Normally this can be done with dbus-send but dbus-send does
// not support passing file descriptors.

#include <stdio.h>
#include <stdlib.h>

#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/file_descriptor.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace {

// Because the logs can be huge, we set the D-Bus timeout to 2 minutes.
const int kDBusTimeoutMS = 120 * 1000;

const char kUsage[] =
  "Developer helper tool for getting extended debug logs from the system."
  "\n"
  "\n"
  "This calls back into debugd using the DumpDebugLogs dbus end point.";

// Returns a dynamic file name with datestamps in it.
std::string LogName(bool compress) {
  base::Time::Exploded now;
  base::Time::Now().LocalExplode(&now);

  return base::StringPrintf(
      "debug-logs_%04i%02i%02i-%02i%02i%02i.%s",
      now.year, now.month, now.day_of_month, now.hour, now.minute, now.second,
      compress ? "tgz" : "tar");
}

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(compress, true, "Compress the tarball");
  DEFINE_string(output, "", "Where to write the output");
  brillo::FlagHelper::Init(argc, argv, kUsage);

  base::FilePath output(FLAGS_output);
  if (output.empty())
    output = base::FilePath{"/tmp/" + LogName(FLAGS_compress)};

  base::ScopedFILE fp(base::OpenFile(output, "w"));
  if (fp == nullptr) {
    PLOG(ERROR) << "Could not write output: " << output.value();
    return EXIT_FAILURE;
  }

  // Set up dbus proxy for talking to debugd.
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));
  CHECK(bus->Connect());
  dbus::ObjectProxy* debugd_proxy = bus->GetObjectProxy(
      debugd::kDebugdServiceName,
      dbus::ObjectPath(debugd::kDebugdServicePath));

  // Send request for debug logs.
  dbus::MethodCall method_call(
      debugd::kDebugdInterface,
      debugd::kDumpDebugLogs);
  dbus::MessageWriter writer(&method_call);
  dbus::FileDescriptor stdout_fd(fileno(fp.get()));
  stdout_fd.CheckValidity();
  writer.AppendBool(FLAGS_compress);
  writer.AppendFileDescriptor(stdout_fd);

  // Wait for the response and process the result.
  LOG(INFO) << "Gathering logs, please wait";
  std::unique_ptr<dbus::Response> response(
      debugd_proxy->CallMethodAndBlock(
          &method_call, kDBusTimeoutMS));
  CHECK(response) << debugd::kDumpDebugLogs << " failed";
  LOG(INFO) << "Logs saved to " << output.value();

  return EXIT_SUCCESS;
}
