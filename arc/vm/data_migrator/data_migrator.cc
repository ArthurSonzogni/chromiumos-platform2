// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/bind.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/threading/thread.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>

#include <arcvm_data_migrator/proto_bindings/arcvm_data_migrator.pb.h>

#include "arc/vm/data_migrator/dbus_adaptors/org.chromium.ArcVmDataMigrator.h"

namespace {

class DBusAdaptor : public org::chromium::ArcVmDataMigratorAdaptor,
                    public org::chromium::ArcVmDataMigratorInterface {
 public:
  explicit DBusAdaptor(scoped_refptr<dbus::Bus> bus)
      : org::chromium::ArcVmDataMigratorAdaptor(this),
        dbus_object_(nullptr, bus, GetObjectPath()) {
    exported_object_ = bus->GetExportedObject(
        dbus::ObjectPath(arc::data_migrator::kArcVmDataMigratorServicePath));
  }

  ~DBusAdaptor() override {
    // TODO(momohatt): Cancel migration running on migration_thread_.
  }

  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  // Registers the D-Bus object and interfaces.
  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
    RegisterWithDBusObject(&dbus_object_);
    dbus_object_.RegisterAsync(std::move(cb));
  }

  // org::chromium::ArcVmDataMigratorInterface overrides:
  bool StartMigration(
      brillo::ErrorPtr* error,
      const arc::data_migrator::StartMigrationRequest& request) override {
    // TODO(momohatt): Mount an ext4 disk image of Android /data.

    // Unretained is safe to use here because |migration_thread_| will be joined
    // on the destruction of |this|.
    auto migrate =
        base::BindOnce(&DBusAdaptor::Migrate, base::Unretained(this));
    migration_thread_ = std::make_unique<base::Thread>("migration_helper");
    migration_thread_->Start();
    migration_thread_->task_runner()->PostTask(FROM_HERE, std::move(migrate));

    return true;
  }

  // TODO(momohatt): Add StopMigration as a D-Bus method?

 private:
  void Migrate() {
    // TODO(momohatt): Trigger migration.

    arc::data_migrator::DataMigrationProgress progress;
    progress.set_status(arc::data_migrator::DATA_MIGRATION_SUCCESS);
    SendMigrationProgressSignal(progress);
  }

  void SendMigrationProgressSignal(
      const arc::data_migrator::DataMigrationProgress& progress) {
    dbus::Signal signal(arc::data_migrator::kArcVmDataMigratorInterface,
                        arc::data_migrator::kMigrationProgressSignal);
    dbus::MessageWriter writer(&signal);
    writer.AppendProtoAsArrayOfBytes(progress);

    exported_object_->SendSignal(&signal);
  }

  std::unique_ptr<base::Thread> migration_thread_;
  brillo::dbus_utils::DBusObject dbus_object_;
  dbus::ExportedObject* exported_object_;  // Owned by the Bus object
};

class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon()
      : DBusServiceDaemon(arc::data_migrator::kArcVmDataMigratorServiceName) {}
  ~Daemon() override = default;

  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 protected:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    adaptor_ = std::make_unique<DBusAdaptor>(bus_);
    adaptor_->RegisterAsync(
        sequencer->GetHandler("RegisterAsync() failed.", true));
  }

 private:
  std::unique_ptr<DBusAdaptor> adaptor_;
};

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  return Daemon().Run();
}
