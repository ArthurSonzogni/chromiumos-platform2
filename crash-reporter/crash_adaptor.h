// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CRASH_ADAPTOR_H_
#define CRASH_REPORTER_CRASH_ADAPTOR_H_

#include <base/memory/scoped_refptr.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/bus.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>

#include <crash-reporter/dbus_adaptors/org.chromium.CrashReporterInterface.h>

class CrashAdaptor : public org::chromium::CrashReporterInterfaceAdaptor,
                     public org::chromium::CrashReporterInterfaceInterface {
 public:
  // The constructor will be blocked until dbus object is initialized.
  // The instance of CrashAdaptor is used to send DebugDumpCreated signal.
  explicit CrashAdaptor(scoped_refptr<dbus::Bus> bus);
  CrashAdaptor(const CrashAdaptor&) = delete;
  CrashAdaptor& operator=(const CrashAdaptor&) = delete;

  ~CrashAdaptor() override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
};
#endif  // CRASH_REPORTER_CRASH_ADAPTOR_H_
