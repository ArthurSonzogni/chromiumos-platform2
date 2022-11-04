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
#include <base/strings/strcat.h>
#include <base/strings/string_piece.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_proxy.h>

#include "fusebox/built_in.h"
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

namespace {
void HandleDBusSignalConnected(const std::string& interface,
                               const std::string& signal,
                               bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to D-Bus signal " << interface << "."
               << signal;
  }
}
}  // namespace

class FuseBoxClient : public FileSystem {
 public:
  FuseBoxClient(scoped_refptr<dbus::Bus> bus, FuseMount* fuse)
      : fuse_(fuse), weak_ptr_factory_(this) {}
  FuseBoxClient(const FuseBoxClient&) = delete;
  FuseBoxClient& operator=(const FuseBoxClient&) = delete;
  virtual ~FuseBoxClient() = default;

  void OnDBusDaemonInit(scoped_refptr<dbus::Bus> bus) {
    const auto path = dbus::ObjectPath(kFuseBoxServicePath);
    dbus_proxy_ = bus->GetObjectProxy(kFuseBoxServiceName, path);

    dbus_proxy_->ConnectToSignal(
        kFuseBoxServiceInterface, kStorageAttachedSignal,
        base::BindRepeating(&FuseBoxClient::OnStorageAttached,
                            weak_ptr_factory_.GetWeakPtr()),
        base::BindOnce(&HandleDBusSignalConnected));
    dbus_proxy_->ConnectToSignal(
        kFuseBoxServiceInterface, kStorageDetachedSignal,
        base::BindRepeating(&FuseBoxClient::OnStorageDetached,
                            weak_ptr_factory_.GetWeakPtr()),
        base::BindOnce(&HandleDBusSignalConnected));

    dbus::MethodCall method(kFuseBoxServiceInterface, kListStoragesMethod);
    dbus::MessageWriter writer(&method);
    ListStoragesRequestProto request_proto;
    writer.AppendProtoAsArrayOfBytes(request_proto);
    CallFuseBoxServerMethod(&method,
                            base::BindOnce(&FuseBoxClient::ListStoragesResponse,
                                           weak_ptr_factory_.GetWeakPtr()));
  }

  void ListStoragesResponse(dbus::Response* response) {
    dbus::MessageReader reader(response);
    ListStoragesResponseProto response_proto;
    if (!reader.PopArrayOfBytesAsProto(&response_proto)) {
      return;
    }
    for (const auto& subdir : response_proto.storages()) {
      DoAttachStorage(subdir, 0);
    }
  }

  int StartFuseSession(base::OnceClosure stop_callback) {
    CHECK(stop_callback);

    fuse_frontend_.reset(new FuseFrontend(fuse_));
    FileSystem* fs = fuse_->fake ? CreateFakeFileSystem() : this;
    if (!fuse_frontend_->CreateFuseSession(fs, FileSystem::FuseOps()))
      return EX_SOFTWARE;

    dbus_proxy_->SetNameOwnerChangedCallback(base::BindRepeating(
        &FuseBoxClient::ServiceOwnerChanged, weak_ptr_factory_.GetWeakPtr()));
    fuse_frontend_->StartFuseSession(std::move(stop_callback));
    return EX_OK;
  }

  void ServiceOwnerChanged(const std::string&, const std::string& owner) {
    if (owner.empty()) {
      PLOG(ERROR) << "service owner changed";
      fuse_frontend_->StopFuseSession(errno);
    }
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

    CHECK_EQ(0, DoAttachStorage("built_in", INO_BUILT_IN));
    BuiltInEnsureNodes(GetInodeTable());

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

    auto stat_response = base::BindOnce(&FuseBoxClient::StatResponse,
                                        weak_ptr_factory_.GetWeakPtr(),
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

    if (parent <= FUSE_ROOT_ID) {
      RootLookup(std::move(request), name);
      return;
    } else if (parent == INO_BUILT_IN) {
      BuiltInLookup(std::move(request), name);
      return;
    }

    Node* parent_node = GetInodeTable().Lookup(parent);
    if (!parent_node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "lookup parent";
      return;
    }

    dbus::MethodCall method(kFuseBoxServiceInterface, kStatMethod);
    dbus::MessageWriter writer(&method);

    std::string path = GetInodeTable().GetDevicePath(parent_node);
    writer.AppendString(path.append("/").append(name));

    auto lookup_response = base::BindOnce(
        &FuseBoxClient::LookupResponse, weak_ptr_factory_.GetWeakPtr(),
        std::move(request), parent, std::string(name));
    CallFuseBoxServerMethod(&method, std::move(lookup_response));
  }

  void RootLookup(std::unique_ptr<EntryRequest> request, std::string name) {
    VLOG(1) << "root-lookup" << FUSE_ROOT_ID << "/" << name;

    // Look for a device directory that we were previously told about (by
    // DoAttachStorage, typically via the OnStorageAttached D-Bus signal).
    auto it = device_dir_entry_.find(name);
    if (it != device_dir_entry_.end()) {
      DoRootLookup(std::move(request), it->second.ino);
      return;
    }

    // If we didn't find one, it's probably ENOENT, but there's also the
    // unlikely possibility that there was a race (since Chrome and FuseBox are
    // separate processes and D-Bus IPC can also bounce through the kernel)
    // where we get the FUSE request before the corresponding OnStorageAttached
    // D-Bus signal. We therefore ask the Chrome process (via a D-Bus method
    // call) whether the subdir exists (and reply ENOENT if it doesn't).
    dbus::MethodCall method(kFuseBoxServiceInterface, kStatMethod);
    dbus::MessageWriter writer(&method);
    writer.AppendString(name);

    auto stat_response = base::BindOnce(&FuseBoxClient::RootLookupResponse,
                                        weak_ptr_factory_.GetWeakPtr(),
                                        std::move(request), name);
    CallFuseBoxServerMethod(&method, std::move(stat_response));
  }

  void RootLookupResponse(std::unique_ptr<EntryRequest> request,
                          std::string name,
                          dbus::Response* response) {
    VLOG(1) << "rootlookup-resp " << name;

    if (request->IsInterrupted())
      return;

    dbus::MessageReader reader(response);
    if (int error = GetResponseErrno(&reader, response, "rootlookup")) {
      errno = request->ReplyError(error);
      PLOG(ERROR) << "rootlookup";
      return;
    }

    DoAttachStorage(name, 0);

    auto it = device_dir_entry_.find(name);
    if (it != device_dir_entry_.end()) {
      DoRootLookup(std::move(request), it->second.ino);
      return;
    }
    errno = request->ReplyError(ENOENT);
    PLOG(ERROR) << "rootlookup";
  }

  void DoRootLookup(std::unique_ptr<EntryRequest> request, ino_t ino) {
    fuse_entry_param entry = {0};
    entry.ino = static_cast<fuse_ino_t>(ino);
    CHECK(GetInodeTable().GetStat(ino, &entry.attr));
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
      errno = request->ReplyError(errno ? errno : EACCES);
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

    auto truncate_response = base::BindOnce(&FuseBoxClient::TruncateResponse,
                                            weak_ptr_factory_.GetWeakPtr(),
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

    auto dir_entry_response = std::make_unique<DirEntryResponse>(ino, handle);
    dir_entry_response->Append(std::move(request));

    if (node->ino <= FUSE_ROOT_ID) {
      RootReadDir(off, std::move(dir_entry_response));
      return;
    } else if (node->ino == INO_BUILT_IN) {
      BuiltInReadDir(off, std::move(dir_entry_response));
      return;
    }
    CallReadDir2(ino, GetInodeTable().GetDevicePath(node), 0, 0,
                 std::move(dir_entry_response));
  }

  void RootReadDir(off_t off, std::unique_ptr<DirEntryResponse> response) {
    VLOG(1) << "root-readdir fh " << response->handle() << " off " << off;

    std::vector<DirEntry> entries;
    for (const auto& item : device_dir_entry_)
      entries.push_back(item.second);

    response->Append(std::move(entries), true);
  }

  void CallReadDir2(ino_t parent_ino,
                    std::string parent_path,
                    uint64_t cookie,
                    int32_t cancel_error_code,
                    std::unique_ptr<DirEntryResponse> dir_entry_response) {
    ReadDir2RequestProto request_proto;
    request_proto.set_file_system_url(parent_path);
    request_proto.set_cookie(cookie);
    request_proto.set_cancel_error_code(cancel_error_code);

    dbus::MethodCall method(kFuseBoxServiceInterface, kReadDir2Method);
    dbus::MessageWriter writer(&method);
    writer.AppendProtoAsArrayOfBytes(request_proto);

    auto readdir2_response = base::BindOnce(
        &FuseBoxClient::ReadDir2Response, weak_ptr_factory_.GetWeakPtr(),
        parent_path, parent_ino, std::move(dir_entry_response));
    CallFuseBoxServerMethod(&method, std::move(readdir2_response));
  }

  void ReadDir2Response(std::string parent_path,
                        ino_t parent_ino,
                        std::unique_ptr<DirEntryResponse> dir_entry_response,
                        dbus::Response* response) {
    VLOG(1) << "readdir2-resp";

    dbus::MessageReader reader(response);
    ReadDir2ResponseProto response_proto;
    if (!reader.PopArrayOfBytesAsProto(&response_proto)) {
      dir_entry_response->Append(EINVAL);
      return;
    }
    int32_t posix_error_code = response_proto.has_posix_error_code()
                                   ? response_proto.posix_error_code()
                                   : 0;
    if (posix_error_code != 0) {
      dir_entry_response->Append(posix_error_code);
      return;
    }
    uint64_t cookie = response_proto.has_cookie() ? response_proto.cookie() : 0;

    std::vector<fusebox::DirEntry> entries;
    for (const auto& item : response_proto.entries()) {
      const char* name = item.name().c_str();
      if (Node* node = GetInodeTable().Ensure(parent_ino, name)) {
        entries.push_back(
            {node->ino, item.name(), MakeStatModeBits(item.mode_bits())});
      } else {
        dir_entry_response->Append(errno);
        PLOG(ERROR) << "readdir2-resp";
        if (cookie != 0) {
          CallReadDir2(parent_ino, std::move(parent_path), cookie, errno,
                       std::move(dir_entry_response));
        }
        return;
      }
    }
    dir_entry_response->Append(std::move(entries), cookie == 0);

    if (cookie != 0) {
      CallReadDir2(parent_ino, std::move(parent_path), cookie, 0,
                   std::move(dir_entry_response));
    }
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
    Node* parent_node = GetInodeTable().Lookup(parent);
    if (!parent_node || (parent < FIRST_UNRESERVED_INO)) {
      errno = request->ReplyError(errno ? errno : EACCES);
      PLOG(ERROR) << "mkdir";
      return;
    }

    Node* node = GetInodeTable().Create(parent, name);
    if (!node) {
      request->ReplyError(errno);
      PLOG(ERROR) << "mkdir child";
      return;
    }

    MkDirRequestProto request_proto;
    request_proto.set_file_system_url(GetInodeTable().GetDevicePath(node));

    dbus::MethodCall method(kFuseBoxServiceInterface, kMkDirMethod);
    dbus::MessageWriter writer(&method);
    writer.AppendProtoAsArrayOfBytes(request_proto);

    auto mkdir_response = base::BindOnce(&FuseBoxClient::MkDirResponse,
                                         weak_ptr_factory_.GetWeakPtr(),
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
    MkDirResponseProto response_proto;
    if (!reader.PopArrayOfBytesAsProto(&response_proto)) {
      GetInodeTable().Forget(ino);
      request->ReplyError(EINVAL);
      return;
    }
    int32_t posix_error_code = response_proto.has_posix_error_code()
                                   ? response_proto.posix_error_code()
                                   : 0;
    if (posix_error_code != 0) {
      GetInodeTable().Forget(ino);
      request->ReplyError(posix_error_code);
      return;
    }

    fuse_entry_param entry = {0};
    entry.ino = static_cast<fuse_ino_t>(ino);
    if (response_proto.has_stat()) {
      entry.attr = MakeStatFromProto(ino, response_proto.stat());
    }
    entry.attr_timeout = kEntryTimeoutSeconds;
    entry.entry_timeout = kEntryTimeoutSeconds;

    request->ReplyEntry(entry);
  }

  void RmDir(std::unique_ptr<OkRequest> request,
             ino_t parent,
             const char* name) override {
    VLOG(1) << "rmdir " << parent << "/" << name;

    if (request->IsInterrupted())
      return;

    errno = 0;
    Node* parent_node = GetInodeTable().Lookup(parent);
    if (!parent_node || (parent < FIRST_UNRESERVED_INO)) {
      errno = request->ReplyError(errno ? errno : EACCES);
      PLOG(ERROR) << "rmdir";
      return;
    }

    Node* node = GetInodeTable().Lookup(parent, name);
    ino_t ino = node ? node->ino : 0;

    RmDirRequestProto request_proto;
    request_proto.set_file_system_url(
        base::StrCat({GetInodeTable().GetDevicePath(parent_node), "/", name}));

    dbus::MethodCall method(kFuseBoxServiceInterface, kRmDirMethod);
    dbus::MessageWriter writer(&method);
    writer.AppendProtoAsArrayOfBytes(request_proto);

    auto rmdir_response =
        base::BindOnce(&FuseBoxClient::RmDirResponse,
                       weak_ptr_factory_.GetWeakPtr(), std::move(request), ino);
    CallFuseBoxServerMethod(&method, std::move(rmdir_response));
  }

  void RmDirResponse(std::unique_ptr<OkRequest> request,
                     ino_t ino,
                     dbus::Response* response) {
    VLOG(1) << "rmdir-resp " << ino;

    if (request->IsInterrupted()) {
      return;
    }

    dbus::MessageReader reader(response);
    RmDirResponseProto response_proto;
    if (!reader.PopArrayOfBytesAsProto(&response_proto)) {
      request->ReplyError(EINVAL);
      return;
    }
    int32_t posix_error_code = response_proto.has_posix_error_code()
                                   ? response_proto.posix_error_code()
                                   : 0;
    if (posix_error_code != 0) {
      request->ReplyError(posix_error_code);
      return;
    }

    if (ino) {
      GetInodeTable().Forget(ino);
    }
    request->ReplyOk();
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
      errno = request->ReplyError(errno ? errno : EACCES);
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

    auto open_response = base::BindOnce(
        &FuseBoxClient::OpenResponse, weak_ptr_factory_.GetWeakPtr(),
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
      BuiltInRead(dbus_proxy_, std::move(request), ino, size, off);
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

    auto read_response = base::BindOnce(&FuseBoxClient::ReadResponse,
                                        weak_ptr_factory_.GetWeakPtr(),
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
      errno = request->ReplyError(errno ? errno : EACCES);
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

    auto write_response = base::BindOnce(&FuseBoxClient::WriteResponse,
                                         weak_ptr_factory_.GetWeakPtr(),
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
        base::BindOnce(&FuseBoxClient::ReleaseResponse,
                       weak_ptr_factory_.GetWeakPtr(), std::move(request), ino);
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
      errno = request->ReplyError(errno ? errno : EACCES);
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

    auto create_response = base::BindOnce(&FuseBoxClient::CreateResponse,
                                          weak_ptr_factory_.GetWeakPtr(),
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

  void OnStorageAttached(dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    std::string subdir;
    if (!reader.PopString(&subdir)) {
      return;
    }
    DoAttachStorage(subdir, 0);
  }

  int32_t DoAttachStorage(const std::string& name, ino_t ino) {
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

  void OnStorageDetached(dbus::Signal* signal) {
    dbus::MessageReader reader(signal);
    std::string subdir;
    if (!reader.PopString(&subdir)) {
      return;
    }

    VLOG(1) << "detach-storage " << subdir;

    auto it = device_dir_entry_.find(subdir);
    if (it == device_dir_entry_.end())
      return;
    GetInodeTable().DetachDevice(it->second.ino);
    device_dir_entry_.erase(it);
  }

 private:
  // Server D-Bus proxy.
  scoped_refptr<dbus::ObjectProxy> dbus_proxy_;

  // Map device name to device DirEntry.
  std::map<std::string, DirEntry> device_dir_entry_;

  // Fuse mount: not owned.
  FuseMount* fuse_ = nullptr;

  // Fuse user-space frontend.
  std::unique_ptr<FuseFrontend> fuse_frontend_;

  base::WeakPtrFactory<FuseBoxClient> weak_ptr_factory_;
};

class FuseBoxDaemon : public brillo::DBusDaemon {
 public:
  explicit FuseBoxDaemon(FuseMount* fuse)
      : fuse_(fuse), weak_ptr_factory_(this) {}
  FuseBoxDaemon(const FuseBoxDaemon&) = delete;
  FuseBoxDaemon& operator=(const FuseBoxDaemon&) = delete;
  ~FuseBoxDaemon() = default;

 protected:
  // brillo::DBusDaemon overrides.

  int OnInit() override {
    int ret = DBusDaemon::OnInit();
    if (ret != EX_OK)
      return ret;

    bus_->AssertOnDBusThread();

    client_ = std::make_unique<FuseBoxClient>(bus_, fuse_);
    client_->OnDBusDaemonInit(bus_);
    return EX_OK;
  }

  int OnEventLoopStarted() override {
    bus_->AssertOnDBusThread();

    int ret = brillo::DBusDaemon::OnEventLoopStarted();
    if (ret != EX_OK)
      return ret;

    auto quit = base::BindOnce(&Daemon::Quit, weak_ptr_factory_.GetWeakPtr());
    return client_->StartFuseSession(std::move(quit));
  }

  void OnShutdown(int* exit_code) override {
    bus_->AssertOnDBusThread();

    DBusDaemon::OnShutdown(exit_code);
    client_.reset();
  }

 private:
  // Fuse mount: not owned.
  FuseMount* fuse_ = nullptr;

  // Fuse user-space client.
  std::unique_ptr<FuseBoxClient> client_;

  base::WeakPtrFactory<FuseBoxDaemon> weak_ptr_factory_;
};

int Run(char** mountpoint, fuse_chan* chan, int foreground) {
  LOG(INFO) << "fusebox " << *mountpoint << " [" << getpid() << "]";

  FuseMount fuse = FuseMount(mountpoint, chan);

  auto* commandline_options = base::CommandLine::ForCurrentProcess();
  fuse.opts = commandline_options->GetSwitchValueASCII("ll");
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
