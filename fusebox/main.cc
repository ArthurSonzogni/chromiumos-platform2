// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>
#include <unistd.h>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/strings/stringprintf.h>

#include <brillo/daemons/dbus_daemon.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_proxy.h>

#include "fusebox/dbus_adaptors/org.chromium.FuseBoxClient.h"
#include "fusebox/file_system.h"
#include "fusebox/file_system_fake.h"
#include "fusebox/fuse_frontend.h"
#include "fusebox/fuse_path_inodes.h"
#include "fusebox/make_stat.h"
#include "fusebox/util.h"

using base::CommandLine;

namespace {

static std::string* g_device;

void SetupLogging() {
  brillo::InitLog(brillo::kLogToStderr);

  static std::string device;
  device = CommandLine::ForCurrentProcess()->GetSwitchValueASCII("storage");
  LOG_IF(INFO, !device.empty()) << "device: " << device;
  g_device = &device;
}

static bool g_use_fake_file_system;

fusebox::FileSystem* CreateFakeFileSystem() {
  static base::NoDestructor<fusebox::FileSystemFake> fake_file_system;
  return fake_file_system.get();
}

fusebox::InodeTable& GetInodeTable() {
  static base::NoDestructor<fusebox::InodeTable> inode_table;
  return *inode_table;
}

dbus::MethodCall GetFuseBoxServerMethodCall(
    const char* method = fusebox::kFuseBoxOperationMethod) {
  return dbus::MethodCall(fusebox::kFuseBoxServiceInterface, method);
}

}  // namespace

namespace fusebox {

class FuseBoxClient : public org::chromium::FuseBoxClientInterface,
                      public org::chromium::FuseBoxClientAdaptor,
                      public FileSystem {
 public:
  FuseBoxClient(scoped_refptr<dbus::Bus> bus, FuseMount* fuse)
      : org::chromium::FuseBoxClientAdaptor(this),
        dbus_object_(nullptr, bus, dbus::ObjectPath(kFuseBoxClientPath)),
        bus_(bus),
        fuse_(fuse) {}
  FuseBoxClient(const FuseBoxClient&) = delete;
  FuseBoxClient& operator=(const FuseBoxClient&) = delete;
  virtual ~FuseBoxClient() = default;

  void RegisterDBusObjectsAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
    RegisterWithDBusObject(&dbus_object_);
    dbus_object_.RegisterAsync(cb);

    auto path = dbus::ObjectPath(fusebox::kFuseBoxServicePath);
    dbus_proxy_ = bus_->GetObjectProxy(fusebox::kFuseBoxServiceName, path);
  }

  int StartFuseSession(base::OnceClosure quit_callback) {
    quit_callback_ = std::move(quit_callback);

    fuse_frontend_.reset(new FuseFrontend(fuse_));
    auto* fs = g_use_fake_file_system ? CreateFakeFileSystem() : this;
    if (!fuse_frontend_->CreateFuseSession(fs, FileSystem::FuseOps()))
      return EX_SOFTWARE;

    auto stop = base::BindOnce(&FuseBoxClient::Stop, base::Unretained(this));
    fuse_frontend_->StartFuseSession(std::move(stop));
    return EX_OK;
  }

  void Stop() {
    fuse_frontend_.reset();
    if (quit_callback_) {
      std::move(quit_callback_).Run();
    }
  }

  void Init(void* userdata, struct fuse_conn_info*) override {
    CHECK(userdata);
  }

  void GetAttr(std::unique_ptr<AttrRequest> request, fuse_ino_t ino) override {
    if (request->IsInterrupted())
      return;

    Node* node = GetInodeTable().Lookup(ino);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << " getattr " << ino;
      return;
    }

    dbus::MethodCall method = GetFuseBoxServerMethodCall();
    dbus::MessageWriter writer(&method);

    writer.AppendString("stat");
    auto item = *g_device + GetInodeTable().GetPath(node);
    writer.AppendString(item);

    auto stat_response =
        base::BindOnce(&FuseBoxClient::StatResponse, base::Unretained(this),
                       std::move(request), node->ino);
    constexpr auto timeout = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT;
    dbus_proxy_->CallMethod(&method, timeout, std::move(stat_response));
  }

  const double kStatTimeoutSeconds = 5.0;

  void StatResponse(std::unique_ptr<AttrRequest> request,
                    fuse_ino_t ino,
                    dbus::Response* response) {
    if (request->IsInterrupted())
      return;

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response)) {
      request->ReplyError(error);
      return;
    }

    Node* node = GetInodeTable().Lookup(ino);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "getattr " << ino;
      return;
    }

    struct stat stat = GetServerStat(ino, &reader);
    request->ReplyAttr(stat, kStatTimeoutSeconds);
  }

  void Lookup(std::unique_ptr<EntryRequest> request,
              fuse_ino_t parent,
              const char* name) override {
    if (request->IsInterrupted())
      return;

    Node* parent_node = GetInodeTable().Lookup(parent);
    if (!parent_node) {
      request->ReplyError(errno);
      PLOG(ERROR) << " lookup " << parent;
      return;
    }

    dbus::MethodCall method = GetFuseBoxServerMethodCall();
    dbus::MessageWriter writer(&method);

    writer.AppendString("stat");
    const base::FilePath path(GetInodeTable().GetPath(parent_node));
    auto item = *g_device + path.Append(name).value();
    writer.AppendString(item);

    auto lookup_response =
        base::BindOnce(&FuseBoxClient::LookupResponse, base::Unretained(this),
                       std::move(request), parent, name);
    constexpr auto timeout = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT;
    dbus_proxy_->CallMethod(&method, timeout, std::move(lookup_response));
  }

  const double kEntryTimeoutSeconds = 5.0;

  void LookupResponse(std::unique_ptr<EntryRequest> request,
                      fuse_ino_t parent,
                      const char* name,
                      dbus::Response* response) {
    if (request->IsInterrupted())
      return;

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response)) {
      request->ReplyError(error);
      return;
    }

    Node* node = GetInodeTable().Lookup(parent, name);
    if (!node)
      node = GetInodeTable().Create(parent, name);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << " lookup " << parent << " " << name;
      return;
    }

    fuse_entry_param entry = {0};
    entry.ino = node->ino;
    entry.attr = GetServerStat(node->ino, &reader);
    entry.attr_timeout = kStatTimeoutSeconds;
    entry.entry_timeout = kEntryTimeoutSeconds;

    request->ReplyEntry(entry);
  }

  void ReadDirBatchResponse(uint64_t file_handle,
                            int32_t file_error,
                            const std::vector<uint8_t>& list,
                            bool has_more) override {
    // TODO(noel): implement.
  }

 private:
  // Client D-Bus object.
  brillo::dbus_utils::DBusObject dbus_object_;

  // Server D-Bus proxy.
  scoped_refptr<dbus::ObjectProxy> dbus_proxy_;

  // D-Bus.
  scoped_refptr<dbus::Bus> bus_;

  // Fuse mount: not owned.
  FuseMount* fuse_ = nullptr;

  // Fuse user-space frontend.
  std::unique_ptr<FuseFrontend> fuse_frontend_;

  // Quit callback.
  base::OnceClosure quit_callback_;
};

class FuseBoxDaemon : public brillo::DBusServiceDaemon {
 public:
  explicit FuseBoxDaemon(FuseMount* fuse)
      : DBusServiceDaemon(kFuseBoxClientName), fuse_(fuse) {}
  FuseBoxDaemon(const FuseBoxDaemon&) = delete;
  FuseBoxDaemon& operator=(const FuseBoxDaemon&) = delete;
  ~FuseBoxDaemon() = default;

 protected:
  // brillo::DBusServiceDaemon overrides.

  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    bus_->AssertOnDBusThread();

    client_.reset(new FuseBoxClient(bus_, fuse_));
    client_->RegisterDBusObjectsAsync(
        sequencer->GetHandler("D-Bus register async failed", true));
  }

  int OnEventLoopStarted() override {
    bus_->AssertOnDBusThread();

    int ret = brillo::DBusServiceDaemon::OnEventLoopStarted();
    if (ret != EX_OK)
      return ret;

    auto quit = base::BindOnce(&Daemon::Quit, base::Unretained(this));
    return client_->StartFuseSession(std::move(quit));
  }

  void OnShutdown(int* exit_code) override {
    bus_->AssertOnDBusThread();

    DBusServiceDaemon::OnShutdown(exit_code);
    client_.reset();
  }

 private:
  // Fuse mount: not owned.
  FuseMount* fuse_ = nullptr;

  // Fuse user-space client.
  std::unique_ptr<FuseBoxClient> client_;
};

int Run(char** mountpoint, fuse_chan* chan) {
  LOG(INFO) << base::StringPrintf("fusebox %s [%d]", *mountpoint, getpid());
  FuseMount fuse = FuseMount(mountpoint, chan);
  auto daemon = FuseBoxDaemon(&fuse);
  return daemon.Run();
}

}  // namespace fusebox

int main(int argc, char** argv) {
  CommandLine::Init(argc, argv);
  SetupLogging();

  if (CommandLine::ForCurrentProcess()->HasSwitch("fake"))
    g_use_fake_file_system = true;

  fuse_args args = FUSE_ARGS_INIT(argc, argv);
  char* mountpoint = nullptr;

  if (fuse_parse_cmdline(&args, &mountpoint, nullptr, nullptr) == -1) {
    PLOG(ERROR) << "fuse_parse_cmdline() failed";
    return EX_USAGE;
  }

  if (!mountpoint) {
    LOG(ERROR) << "fuse_parse_cmdline() mountpoint expected";
    return ENODEV;
  }

  fuse_chan* chan = fuse_mount(mountpoint, &args);
  if (!chan) {
    PLOG(ERROR) << "fuse_mount() [" << mountpoint << "] failed";
    return ENODEV;
  }

  int exit_code = fusebox::Run(&mountpoint, chan);

  if (!mountpoint) {  // Kernel removed the FUSE mountpoint: umount(8).
    exit_code = ENODEV;
  } else {
    fuse_unmount(mountpoint, nullptr);
  }

  errno = exit_code;
  LOG_IF(ERROR, exit_code) << "fusebox exiting";
  fuse_opt_free_args(&args);

  return exit_code;
}
