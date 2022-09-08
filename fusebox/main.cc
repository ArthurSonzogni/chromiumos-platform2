// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>
#include <unistd.h>

#include <map>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/numerics/safe_conversions.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_proxy.h>

#include "fusebox/built_in.h"
#include "fusebox/dbus_adaptors/org.chromium.FuseBoxReverseService.h"
#include "fusebox/file_system.h"
#include "fusebox/file_system_fake.h"
#include "fusebox/file_system_type.h"
#include "fusebox/fuse_file_handles.h"
#include "fusebox/fuse_frontend.h"
#include "fusebox/fuse_path_inodes.h"
#include "fusebox/make_stat.h"
#include "fusebox/proto_bindings/fusebox.pb.h"
#include "fusebox/util.h"

namespace fusebox {

#define kFuseBoxClientPath kFuseBoxReverseServicePath

class FuseBoxClient : public org::chromium::FuseBoxReverseServiceInterface,
                      public org::chromium::FuseBoxReverseServiceAdaptor,
                      public FileSystem {
 public:
  FuseBoxClient(scoped_refptr<dbus::Bus> bus, FuseMount* fuse)
      : org::chromium::FuseBoxReverseServiceAdaptor(this),
        dbus_object_(nullptr, bus, dbus::ObjectPath(kFuseBoxClientPath)),
        bus_(bus),
        fuse_(fuse) {}
  FuseBoxClient(const FuseBoxClient&) = delete;
  FuseBoxClient& operator=(const FuseBoxClient&) = delete;
  virtual ~FuseBoxClient() = default;

  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
    RegisterWithDBusObject(&dbus_object_);
    dbus_object_.RegisterAsync(std::move(cb));

    const auto path = dbus::ObjectPath(kFuseBoxServicePath);
    dbus_proxy_ = bus_->GetObjectProxy(kFuseBoxServiceName, path);
  }

  int StartFuseSession(base::OnceClosure stop_callback) {
    CHECK(stop_callback);

    fuse_frontend_.reset(new FuseFrontend(fuse_));
    FileSystem* fs = fuse_->fake ? CreateFakeFileSystem() : this;
    if (!fuse_frontend_->CreateFuseSession(fs, FileSystem::FuseOps()))
      return EX_SOFTWARE;

    dbus_proxy_->SetNameOwnerChangedCallback(base::BindRepeating(
        &FuseBoxClient::ServiceOwnerChanged, base::Unretained(this)));
    fuse_frontend_->StartFuseSession(std::move(stop_callback));
    return EX_OK;
  }

  void ServiceOwnerChanged(const std::string&, const std::string& owner) {
    if (owner.empty()) {
      PLOG(ERROR) << "service owner changed";
      fuse_frontend_->StopFuseSession(errno);
    }
  }

  bool TestIsAlive() override {
    return dbus_proxy_.get() && fuse_frontend_;  // setup and serving
  }

  static FileSystem* CreateFakeFileSystem() {
    static base::NoDestructor<FileSystemFake> fake_file_system;
    return fake_file_system.get();
  }

  static InodeTable& GetInodeTable() {
    static base::NoDestructor<InodeTable> inode_table;
    return *inode_table;
  }

  template <typename Signature>
  void CallFuseBoxServerMethod(dbus::MethodCall* method_call,
                               base::OnceCallback<Signature> callback) {
    constexpr auto timeout = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT;
    dbus_proxy_->CallMethod(method_call, timeout, std::move(callback));
  }

  void Init(void* userdata, struct fuse_conn_info*) override {
    VLOG(1) << "init";

    Node* root = GetInodeTable().Lookup(FUSE_ROOT_ID);
    struct stat root_stat = MakeTimeStat(S_IFDIR | 0770);
    root_stat = MakeStat(root->ino, root_stat);
    GetInodeTable().SetStat(root->ino, root_stat);

    CHECK_EQ(0, AttachStorage("built_in", INO_BUILT_IN));
    BuiltInEnsureNodes(GetInodeTable());
    CHECK_EQ(0, AttachStorage(kMonikerAttachStorageArg));

    if (!fuse_->name.empty())
      CHECK_EQ(0, AttachStorage(fuse_->name));

    CHECK(userdata) << "FileSystem (userdata) is required";
  }

  void GetAttr(std::unique_ptr<AttrRequest> request, ino_t ino) override {
    VLOG(1) << "getattr " << ino;

    if (request->IsInterrupted())
      return;

    Node* node = GetInodeTable().Lookup(ino);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "getattr";
      return;
    }

    if (node->parent <= FUSE_ROOT_ID) {
      struct stat stat;
      CHECK(GetInodeTable().GetStat(node->ino, &stat));
      request->ReplyAttr(stat, kStatTimeoutSeconds);
      return;
    } else if (node->parent == INO_BUILT_IN) {
      struct stat stat;
      BuiltInGetStat(node->ino, &stat);
      request->ReplyAttr(stat, kStatTimeoutSeconds);
      return;
    }

    dbus::MethodCall method(kFuseBoxServiceInterface, kStatMethod);
    dbus::MessageWriter writer(&method);

    auto path = GetInodeTable().GetDevicePath(node);
    writer.AppendString(path);

    auto stat_response =
        base::BindOnce(&FuseBoxClient::StatResponse, base::Unretained(this),
                       std::move(request), node->ino);
    CallFuseBoxServerMethod(&method, std::move(stat_response));
  }

  void StatResponse(std::unique_ptr<AttrRequest> request,
                    ino_t ino,
                    dbus::Response* response) {
    VLOG(1) << "getattr-resp " << ino;

    if (request->IsInterrupted())
      return;

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response, "getattr")) {
      request->ReplyError(error);
      return;
    }

    Node* node = GetInodeTable().Lookup(ino);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "getattr-resp";
      return;
    }

    Device device = GetInodeTable().GetDevice(node);
    bool read_only = device.mode == "ro";

    struct stat stat = GetServerStat(ino, &reader, read_only);
    request->ReplyAttr(stat, kStatTimeoutSeconds);
  }

  void Lookup(std::unique_ptr<EntryRequest> request,
              ino_t parent,
              const char* name) override {
    VLOG(1) << "lookup " << parent << "/" << name;

    if (request->IsInterrupted())
      return;

    Node* parent_node = GetInodeTable().Lookup(parent);
    if (!parent_node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "lookup parent";
      return;
    }

    if (parent <= FUSE_ROOT_ID) {
      RootLookup(std::move(request), name);
      return;
    } else if (parent == INO_BUILT_IN) {
      BuiltInLookup(std::move(request), name);
      return;
    }

    dbus::MethodCall method(kFuseBoxServiceInterface, kStatMethod);
    dbus::MessageWriter writer(&method);

    std::string path = GetInodeTable().GetDevicePath(parent_node);
    writer.AppendString(path.append("/").append(name));

    auto lookup_response =
        base::BindOnce(&FuseBoxClient::LookupResponse, base::Unretained(this),
                       std::move(request), parent, std::string(name));
    CallFuseBoxServerMethod(&method, std::move(lookup_response));
  }

  void RootLookup(std::unique_ptr<EntryRequest> request, std::string name) {
    VLOG(1) << "root-lookup" << FUSE_ROOT_ID << "/" << name;

    auto it = device_dir_entry_.find(name);
    if (it == device_dir_entry_.end()) {
      errno = request->ReplyError(ENOENT);
      PLOG(ERROR) << "lookup-local";
      return;
    }

    fuse_entry_param entry = {0};
    entry.ino = static_cast<fuse_ino_t>(it->second.ino);
    CHECK(GetInodeTable().GetStat(it->second.ino, &entry.attr));
    entry.attr_timeout = kStatTimeoutSeconds;
    entry.entry_timeout = kEntryTimeoutSeconds;

    request->ReplyEntry(entry);
  }

  void LookupResponse(std::unique_ptr<EntryRequest> request,
                      ino_t parent,
                      std::string name,
                      dbus::Response* response) {
    VLOG(1) << "lookup-resp " << parent << "/" << name;

    if (request->IsInterrupted())
      return;

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response, "lookup")) {
      request->ReplyError(error);
      return;
    }

    Node* node = GetInodeTable().Ensure(parent, name.c_str());
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "lookup-resp";
      return;
    }

    Device device = GetInodeTable().GetDevice(node);
    bool read_only = device.mode == "ro";

    fuse_entry_param entry = {0};
    entry.ino = static_cast<fuse_ino_t>(node->ino);
    entry.attr = GetServerStat(node->ino, &reader, read_only);
    entry.attr_timeout = kStatTimeoutSeconds;
    entry.entry_timeout = kEntryTimeoutSeconds;

    request->ReplyEntry(entry);
  }

  void SetAttr(std::unique_ptr<AttrRequest> request,
               ino_t ino,
               struct stat* attr,
               int to_set) override {
    VLOG(1) << "SetAttr ino " << ino << " fh " << request->fh();

    if (request->IsInterrupted())
      return;

    Node* node = GetInodeTable().Lookup(ino);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "setattr";
      return;
    } else if (node->ino < FIRST_UNRESERVED_INO) {
      errno = request->ReplyError(errno ? errno : EPERM);
      PLOG(ERROR) << "setattr";
      return;
    }

    // Allow setting file size truncate(2) to support file write(2).
    const int kAllowedToSet = FUSE_SET_ATTR_SIZE;

    constexpr auto allowed_to_set = [](int to_set) {
      if (to_set & ~kAllowedToSet)
        return ENOTSUP;
      if (!to_set)  // Nothing to_set? error EINVAL.
        return EINVAL;
      return 0;
    };

    VLOG(1) << "to_set " << ToSetFlagsToString(to_set);
    if (errno = allowed_to_set(to_set); errno) {
      request->ReplyError(errno);
      PLOG(ERROR) << "setattr";
      return;
    }

    dbus::MethodCall method(kFuseBoxServiceInterface, kTruncateMethod);
    dbus::MessageWriter writer(&method);

    auto path = GetInodeTable().GetDevicePath(node);
    writer.AppendString(path);
    writer.AppendInt64(base::strict_cast<int64_t>(attr->st_size));

    auto truncate_response =
        base::BindOnce(&FuseBoxClient::TruncateResponse, base::Unretained(this),
                       std::move(request), node->ino);
    CallFuseBoxServerMethod(&method, std::move(truncate_response));
  }

  void TruncateResponse(std::unique_ptr<AttrRequest> request,
                        ino_t ino,
                        dbus::Response* response) {
    VLOG(1) << "truncate-resp " << ino;

    if (request->IsInterrupted())
      return;

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response, "truncate")) {
      request->ReplyError(error);
      return;
    }

    Node* node = GetInodeTable().Lookup(ino);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "truncate-resp";
      return;
    }

    Device device = GetInodeTable().GetDevice(node);
    bool read_only = device.mode == "ro";

    struct stat stat = GetServerStat(ino, &reader, read_only);
    request->ReplyAttr(stat, kStatTimeoutSeconds);
  }

  void OpenDir(std::unique_ptr<OpenRequest> request, ino_t ino) override {
    VLOG(1) << "opendir " << ino;

    if (request->IsInterrupted())
      return;

    if ((request->flags() & O_ACCMODE) != O_RDONLY) {
      errno = request->ReplyError(EACCES);
      PLOG(ERROR) << "opendir";
      return;
    }

    Node* node = GetInodeTable().Lookup(ino);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "opendir";
      return;
    }

    uint64_t handle = fusebox::OpenFile();
    request->ReplyOpen(handle);
  }

  void ReadDir(std::unique_ptr<DirEntryRequest> request,
               ino_t ino,
               off_t off) override {
    VLOG(1) << "readdir fh " << request->fh() << " off " << off;

    if (request->IsInterrupted())
      return;

    Node* node = GetInodeTable().Lookup(ino);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "readdir";
      return;
    }

    uint64_t handle = fusebox::GetFile(request->fh());
    if (!handle) {
      errno = request->ReplyError(EBADF);
      PLOG(ERROR) << "readdir";
      return;
    }

    auto it = readdir_.find(handle);
    if (it != readdir_.end()) {
      DirEntryResponse* response = it->second.get();
      response->Append(std::move(request));
      return;
    }

    auto& buffer = readdir_[handle];
    buffer.reset(new DirEntryResponse(node->ino, handle));
    buffer->Append(std::move(request));

    if (node->ino <= FUSE_ROOT_ID) {
      RootReadDir(off, buffer.get());
      return;
    } else if (node->ino == INO_BUILT_IN) {
      BuiltInReadDir(off, buffer.get());
      return;
    }

    dbus::MethodCall method(kFuseBoxServiceInterface, kReadDirMethod);
    dbus::MessageWriter writer(&method);

    auto path = GetInodeTable().GetDevicePath(node);
    writer.AppendString(path);
    writer.AppendUint64(handle);

    auto readdir_started =
        base::BindOnce(&FuseBoxClient::ReadDirStarted, base::Unretained(this),
                       node->ino, handle);
    CallFuseBoxServerMethod(&method, std::move(readdir_started));
  }

  void RootReadDir(off_t off, DirEntryResponse* response) {
    VLOG(1) << "root-readdir fh " << response->handle() << " off " << off;

    std::vector<DirEntry> entries;
    for (const auto& item : device_dir_entry_)
      entries.push_back(item.second);

    response->Append(std::move(entries), true);
  }

  void ReadDirStarted(ino_t ino, uint64_t handle, dbus::Response* response) {
    VLOG(1) << "readdir-resp fh " << handle;

    dbus::MessageReader reader(response);
    int error = GetResponseErrno(&reader, response, "readdir");
    if (!error)
      return;

    auto it = readdir_.find(handle);
    if (it != readdir_.end()) {
      DirEntryResponse* response = it->second.get();
      response->Append(error);
    }
  }

  void ReplyToReadDir(uint64_t handle,
                      int32_t error,
                      const std::vector<uint8_t>& list,
                      bool has_more) override {
    VLOG(1) << "reply-to-readdir fh " << handle;

    auto it = readdir_.find(handle);
    if (it == readdir_.end())
      return;

    DirEntryResponse* response = it->second.get();
    if (error) {
      errno = response->Append(error);
      PLOG(ERROR) << "reply-to-readdir";
      return;
    }

    const ino_t parent = response->parent();
    if (!GetInodeTable().Lookup(parent)) {
      response->Append(errno);
      PLOG(ERROR) << "reply-to-readdir parent";
      return;
    }

    fusebox::DirEntryListProto proto;
    std::vector<fusebox::DirEntry> entries;
    CHECK(proto.ParseFromArray(list.data(), list.size()));

    for (const auto& item : proto.entries()) {
      const char* name = item.name().c_str();
      if (Node* node = GetInodeTable().Ensure(parent, name)) {
        Device device = GetInodeTable().GetDevice(node);
        mode_t mode = item.is_directory() ? S_IFDIR | 0770 : S_IFREG | 0770;
        auto item_perms = MakeStatModeBits(mode, device.mode == "ro");
        entries.push_back({node->ino, item.name(), item_perms});
      } else {
        response->Append(errno);
        PLOG(ERROR) << "reply-to-readdir";
        return;
      }
    }

    response->Append(std::move(entries), !has_more);
  }

  void ReleaseDir(std::unique_ptr<OkRequest> request, ino_t ino) override {
    VLOG(1) << "releasedir fh " << request->fh();

    if (request->IsInterrupted())
      return;

    if (!fusebox::GetFile(request->fh())) {
      errno = request->ReplyError(EBADF);
      PLOG(ERROR) << "releasedir";
      return;
    }

    fusebox::CloseFile(request->fh());
    readdir_.erase(request->fh());
    request->ReplyOk();
  }

  void MkDir(std::unique_ptr<EntryRequest> request,
             ino_t parent,
             const char* name,
             mode_t mode) override {
    VLOG(1) << "mkdir " << parent << "/" << name;

    if (request->IsInterrupted())
      return;

    errno = 0;
    if ((request->flags() & O_ACCMODE) != O_RDONLY) {
      errno = request->ReplyError(EACCES);
      PLOG(ERROR) << "mkdir";
      return;
    }

    Node* parent_node = GetInodeTable().Lookup(parent);
    if (!parent_node || (parent < FIRST_UNRESERVED_INO)) {
      errno = request->ReplyError(errno ? errno : EPERM);
      PLOG(ERROR) << "mkdir";
      return;
    }

    Device device = GetInodeTable().GetDevice(parent_node);
    bool read_only = device.mode == "ro";

    if (read_only) {
      errno = request->ReplyError(EROFS);
      PLOG(ERROR) << "mkdir";
      return;
    }

    Node* node = GetInodeTable().Create(parent, name);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "mkdir child";
      return;
    }

    dbus::MethodCall method(kFuseBoxServiceInterface, kMkDirMethod);
    dbus::MessageWriter writer(&method);

    auto path = GetInodeTable().GetDevicePath(node);
    writer.AppendString(path);

    auto mkdir_response =
        base::BindOnce(&FuseBoxClient::MkDirResponse, base::Unretained(this),
                       std::move(request), node->ino);
    CallFuseBoxServerMethod(&method, std::move(mkdir_response));
  }

  void MkDirResponse(std::unique_ptr<EntryRequest> request,
                     ino_t ino,
                     dbus::Response* response) {
    VLOG(1) << "mkdir-resp " << ino;

    if (request->IsInterrupted()) {
      GetInodeTable().Forget(ino);
      return;
    }

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response, "mkdir")) {
      GetInodeTable().Forget(ino);
      request->ReplyError(error);
      return;
    }

    fuse_entry_param entry = {0};
    entry.ino = static_cast<fuse_ino_t>(ino);
    entry.attr = GetServerStat(ino, &reader);
    entry.attr_timeout = kEntryTimeoutSeconds;
    entry.entry_timeout = kEntryTimeoutSeconds;

    request->ReplyEntry(entry);
  }

  void Open(std::unique_ptr<OpenRequest> request, ino_t ino) override {
    VLOG(1) << "open " << ino;

    if (request->IsInterrupted())
      return;

    Node* node = GetInodeTable().Lookup(ino);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "open";
      return;
    } else if (node->parent <= FUSE_ROOT_ID) {
      errno = request->ReplyError(errno ? errno : EPERM);
      PLOG(ERROR) << "open";
      return;
    }

    Device device = GetInodeTable().GetDevice(node);

    constexpr auto mtp_device_type = [](const Device& device) {
      if (device.name.rfind(kMTPType, 0) == 0)
        return std::string(kMTPType);
      return std::string();
    };

    const std::string path = GetInodeTable().GetDevicePath(node);
    const std::string type = mtp_device_type(device);

    int mode = request->flags() & O_ACCMODE;
    if (mode == O_RDONLY) {
      uint64_t handle = fusebox::OpenFile();
      fusebox::SetFileData(handle, path, type);
      request->ReplyOpen(handle);
      return;
    }

    if (device.mode == "ro") {
      errno = request->ReplyError(EROFS);
      PLOG(ERROR) << "open";
      return;
    }

    if (type != kMTPType) {
      uint64_t handle = fusebox::OpenFile();
      fusebox::SetFileData(handle, path, type);
      request->ReplyOpen(handle);
      return;
    }

    CHECK(request->flags() & O_ACCMODE);  // open for write

    dbus::MethodCall method(kFuseBoxServiceInterface, kOpenMethod);
    dbus::MessageWriter writer(&method);

    writer.AppendString(path);
    VLOG(1) << "open flags " << OpenFlagsToString(request->flags());
    writer.AppendInt32(request->flags());

    auto open_response =
        base::BindOnce(&FuseBoxClient::OpenResponse, base::Unretained(this),
                       std::move(request), node->ino, path, type);
    CallFuseBoxServerMethod(&method, std::move(open_response));
  }

  void OpenResponse(std::unique_ptr<OpenRequest> request,
                    ino_t ino,
                    std::string path,
                    std::string type,
                    dbus::Response* response) {
    VLOG(1) << "open-resp " << ino;

    if (request->IsInterrupted())
      return;

    CHECK(request->flags() & O_ACCMODE);  // open for write

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response, "open")) {
      request->ReplyError(error);
      return;
    }

    base::ScopedFD fd;
    reader.PopFileDescriptor(&fd);

    uint64_t handle = fusebox::OpenFile(std::move(fd));
    fusebox::SetFileData(handle, path, type);

    request->ReplyOpen(handle);
  }

  void Read(std::unique_ptr<BufferRequest> request,
            ino_t ino,
            size_t size,
            off_t off) override {
    VLOG(1) << "read fh " << request->fh() << " off " << off << " size "
            << size;

    if (request->IsInterrupted())
      return;

    if (size > SSIZE_MAX) {
      errno = request->ReplyError(EINVAL);
      PLOG(ERROR) << "read";
      return;
    }

    if (ino < FIRST_UNRESERVED_INO) {
      BuiltInRead(std::move(request), ino, size, off);
      return;
    }

    if (!fusebox::GetFile(request->fh())) {
      errno = request->ReplyError(EBADF);
      PLOG(ERROR) << "read";
      return;
    }

    int fd = fusebox::GetFileDescriptor(request->fh());
    if (fd != -1) {
      ReadFileDescriptor(std::move(request), ino, fd, size, off);
      return;
    }

    dbus::MethodCall method(kFuseBoxServiceInterface, kReadMethod);
    dbus::MessageWriter writer(&method);

    auto path = fusebox::GetFileData(request->fh()).path;
    writer.AppendString(path);
    writer.AppendInt64(base::strict_cast<int64_t>(off));
    writer.AppendInt32(base::saturated_cast<int32_t>(size));

    auto read_response =
        base::BindOnce(&FuseBoxClient::ReadResponse, base::Unretained(this),
                       std::move(request), ino, size, off);
    CallFuseBoxServerMethod(&method, std::move(read_response));
  }

  void ReadResponse(std::unique_ptr<BufferRequest> request,
                    ino_t ino,
                    size_t size,
                    off_t off,
                    dbus::Response* response) {
    VLOG(1) << "read-resp fh " << request->fh() << " off " << off << " size "
            << size;

    if (request->IsInterrupted())
      return;

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response, "read")) {
      request->ReplyError(error);
      return;
    }

    if (!fusebox::GetFile(request->fh())) {
      errno = request->ReplyError(EBADF);
      PLOG(ERROR) << "read-resp";
      return;
    }

    const uint8_t* buf = nullptr;
    size_t length = 0;
    reader.PopArrayOfBytes(&buf, &length);

    request->ReplyBuffer(buf, length);
  }

  void ReadFileDescriptor(std::unique_ptr<BufferRequest> request,
                          ino_t ino,
                          int fd,
                          size_t size,
                          off_t off) {
    VLOG(1) << "read-fd fh " << request->fh() << " off " << off << " size "
            << size;

    DCHECK_LE(size, SSIZE_MAX);
    std::vector<char> buf(size);

    DCHECK_NE(-1, fd);
    ssize_t length = HANDLE_EINTR(pread(fd, buf.data(), size, off));
    if (length == -1) {
      request->ReplyError(errno);
      PLOG(ERROR) << "read-fd";
      return;
    }

    request->ReplyBuffer(buf.data(), length);
  }

  void Write(std::unique_ptr<WriteRequest> request,
             ino_t ino,
             const char* buf,
             size_t size,
             off_t off) override {
    VLOG(1) << "write ino " << ino << " off " << off << " size " << size;

    if (request->IsInterrupted())
      return;

    if (size > SSIZE_MAX) {
      errno = request->ReplyError(EINVAL);
      PLOG(ERROR) << "write";
      return;
    }

    if (ino < FIRST_UNRESERVED_INO) {
      errno = request->ReplyError(errno ? errno : EPERM);
      PLOG(ERROR) << "write";
      return;
    }

    if (!fusebox::GetFile(request->fh())) {
      errno = request->ReplyError(EBADF);
      PLOG(ERROR) << "write";
      return;
    }

    int fd = fusebox::GetFileDescriptor(request->fh());
    if (fd != -1) {
      WriteFileDescriptor(std::move(request), ino, fd, buf, size, off);
      return;
    }

    dbus::MethodCall method(kFuseBoxServiceInterface, kWriteMethod);
    dbus::MessageWriter writer(&method);

    auto path = fusebox::GetFileData(request->fh()).path;
    writer.AppendString(path);
    writer.AppendInt64(base::strict_cast<int64_t>(off));
    writer.AppendArrayOfBytes(reinterpret_cast<const uint8_t*>(buf), size);

    auto write_response =
        base::BindOnce(&FuseBoxClient::WriteResponse, base::Unretained(this),
                       std::move(request), ino, size, off);
    CallFuseBoxServerMethod(&method, std::move(write_response));
  }

  void WriteResponse(std::unique_ptr<WriteRequest> request,
                     ino_t ino,
                     size_t size,
                     off_t off,
                     dbus::Response* response) {
    VLOG(1) << "write-resp fh " << request->fh() << " off " << off << " size "
            << size;

    if (request->IsInterrupted())
      return;

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response, "write")) {
      request->ReplyError(error);
      return;
    }

    if (!fusebox::GetFile(request->fh())) {
      errno = request->ReplyError(EBADF);
      PLOG(ERROR) << "write-resp";
      return;
    }

    uint32_t length = 0;
    reader.PopUint32(&length);

    request->ReplyWrite(length);
  }

  void WriteFileDescriptor(std::unique_ptr<WriteRequest> request,
                           ino_t ino,
                           int fd,
                           const char* buf,
                           size_t size,
                           off_t off) {
    VLOG(1) << "write-fd fh " << request->fh() << " off " << off << " size "
            << size;

    DCHECK_LE(size, SSIZE_MAX);

    DCHECK_NE(-1, fd);
    ssize_t length = HANDLE_EINTR(pwrite(fd, buf, size, off));
    if (length == -1) {
      request->ReplyError(errno);
      PLOG(ERROR) << "write-fd";
      return;
    }

    request->ReplyWrite(length);
  }

  void Release(std::unique_ptr<OkRequest> request, ino_t ino) override {
    VLOG(1) << "release fh " << request->fh();

    if (request->IsInterrupted())
      return;

    if (!fusebox::GetFile(request->fh())) {
      errno = request->ReplyError(EBADF);
      PLOG(ERROR) << "release";
      return;
    }

    auto data = fusebox::GetFileData(request->fh());
    fusebox::CloseFile(request->fh());

    if (data.type != kMTPType) {
      request->ReplyOk();
      return;
    }

    // TODO(crbug.com/1249754): implement kCloseMethod. ReplyOk here for now,
    // to suppress method "not implemented" error logs.
    if (data.type == kMTPType) {
      request->ReplyOk();
      return;
    }

    dbus::MethodCall method(kFuseBoxServiceInterface, kCloseMethod);
    dbus::MessageWriter writer(&method);

    writer.AppendString(data.path);

    auto release_response =
        base::BindOnce(&FuseBoxClient::ReleaseResponse, base::Unretained(this),
                       std::move(request), ino);
    CallFuseBoxServerMethod(&method, std::move(release_response));
  }

  void ReleaseResponse(std::unique_ptr<OkRequest> request,
                       ino_t ino,
                       dbus::Response* response) {
    VLOG(1) << "release-resp fh " << request->fh();

    if (request->IsInterrupted())
      return;

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response, "release")) {
      request->ReplyError(error);
      return;
    }

    request->ReplyOk();
  }

  void Create(std::unique_ptr<CreateRequest> request,
              ino_t parent,
              const char* name,
              mode_t mode) override {
    VLOG(1) << "create " << parent << "/" << name;

    if (request->IsInterrupted())
      return;

    errno = 0;
    if (!S_ISREG(mode)) {
      errno = request->ReplyError(ENOTSUP);
      PLOG(ERROR) << "create: regular file expected";
      return;
    }

    Node* parent_node = GetInodeTable().Lookup(parent);
    if (!parent_node || parent < FIRST_UNRESERVED_INO) {
      errno = request->ReplyError(errno ? errno : EPERM);
      PLOG(ERROR) << "create";
      return;
    }

    Device device = GetInodeTable().GetDevice(parent_node);
    bool read_only = device.mode == "ro";

    if (read_only) {
      errno = request->ReplyError(EROFS);
      PLOG(ERROR) << "create";
      return;
    }

    Node* node = GetInodeTable().Create(parent, name);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "create child";
      return;
    }

    dbus::MethodCall method(kFuseBoxServiceInterface, kCreateMethod);
    dbus::MessageWriter writer(&method);

    auto path = GetInodeTable().GetDevicePath(node);
    writer.AppendString(path);
    VLOG(1) << "create flags " << OpenFlagsToString(request->flags());
    writer.AppendInt32(request->flags());

    auto create_response =
        base::BindOnce(&FuseBoxClient::CreateResponse, base::Unretained(this),
                       std::move(request), node->ino);
    CallFuseBoxServerMethod(&method, std::move(create_response));
  }

  void CreateResponse(std::unique_ptr<CreateRequest> request,
                      ino_t ino,
                      dbus::Response* response) {
    VLOG(1) << "create-resp " << ino;

    if (request->IsInterrupted()) {
      GetInodeTable().Forget(ino);
      return;
    }

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response, "create")) {
      GetInodeTable().Forget(ino);
      request->ReplyError(error);
      return;
    }

    fuse_entry_param entry = {0};
    entry.ino = static_cast<fuse_ino_t>(ino);
    entry.attr = GetServerStat(ino, &reader);

    request->SetEntry(entry);
    Open(std::move(request), ino);
  }

  int32_t AttachStorage(const std::string& name) override {
    return AttachStorage(name, 0);
  }

  int32_t AttachStorage(const std::string& name, ino_t ino) {
    VLOG(1) << "attach-storage " << name;

    Device device = GetInodeTable().MakeFromName(name);
    Node* node = GetInodeTable().AttachDevice(FUSE_ROOT_ID, device, ino);
    if (!node)
      return errno;

    struct stat stat = MakeTimeStat(S_IFDIR | 0770);
    stat = MakeStat(node->ino, stat, device.mode == "ro");
    device_dir_entry_[device.name] = {node->ino, device.name, stat.st_mode};
    GetInodeTable().SetStat(node->ino, stat);

    if (fuse_->debug)
      ShowStat(stat, device.name);
    return 0;
  }

  int32_t DetachStorage(const std::string& name) override {
    VLOG(1) << "detach-storage " << name;

    Device device = GetInodeTable().MakeFromName(name);
    auto it = device_dir_entry_.find(device.name);
    if (it == device_dir_entry_.end())
      return ENOENT;

    ino_t device_ino = it->second.ino;
    if (!GetInodeTable().DetachDevice(device_ino))
      return errno;

    device_dir_entry_.erase(it);
    return 0;
  }

 private:
  // Client D-Bus object.
  brillo::dbus_utils::DBusObject dbus_object_;

  // Server D-Bus proxy.
  scoped_refptr<dbus::ObjectProxy> dbus_proxy_;

  // D-Bus.
  scoped_refptr<dbus::Bus> bus_;

  // Map device name to device DirEntry.
  std::map<std::string, DirEntry> device_dir_entry_;

  // Fuse mount: not owned.
  FuseMount* fuse_ = nullptr;

  // Fuse user-space frontend.
  std::unique_ptr<FuseFrontend> fuse_frontend_;

  // Active readdir requests.
  std::map<uint64_t, std::unique_ptr<DirEntryResponse>> readdir_;
};

class FuseBoxDaemon : public brillo::DBusServiceDaemon {
 public:
  explicit FuseBoxDaemon(FuseMount* fuse)
      : DBusServiceDaemon(kFuseBoxReverseServiceName), fuse_(fuse) {}
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

int Run(char** mountpoint, fuse_chan* chan, int foreground) {
  LOG(INFO) << "fusebox " << *mountpoint << " [" << getpid() << "]";

  FuseMount fuse = FuseMount(mountpoint, chan);

  auto* commandline_options = base::CommandLine::ForCurrentProcess();
  fuse.opts = commandline_options->GetSwitchValueASCII("ll");
  fuse.name = commandline_options->GetSwitchValueASCII("storage");
  fuse.debug = commandline_options->HasSwitch("debug");
  fuse.fake = commandline_options->HasSwitch("fake");

  if (!foreground)
    LOG(INFO) << "fusebox fuse_daemonizing";
  fuse_daemonize(foreground);

  auto daemon = FuseBoxDaemon(&fuse);
  return daemon.Run();
}

}  // namespace fusebox

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  fuse_args args = FUSE_ARGS_INIT(argc, argv);
  char* mountpoint = nullptr;

  int foreground = 0;
  if (fuse_parse_cmdline(&args, &mountpoint, nullptr, &foreground) == -1) {
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

  int exit_code = fusebox::Run(&mountpoint, chan, foreground);

  if (!mountpoint) {  // Kernel removed the FUSE mountpoint: umount(8).
    exit_code = EX_OK;
  } else {
    fuse_unmount(mountpoint, nullptr);
  }

  return exit_code;
}
