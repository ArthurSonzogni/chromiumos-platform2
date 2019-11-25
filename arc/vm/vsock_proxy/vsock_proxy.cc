// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/vsock_proxy/vsock_proxy.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <tuple>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/logging.h>

#include "arc/vm/vsock_proxy/file_descriptor_util.h"
#include "arc/vm/vsock_proxy/file_stream.h"
#include "arc/vm/vsock_proxy/pipe_stream.h"
#include "arc/vm/vsock_proxy/socket_stream.h"

namespace arc {
namespace {

std::unique_ptr<StreamBase> CreateStream(
    base::ScopedFD fd, arc_proxy::FileDescriptor::Type fd_type) {
  switch (fd_type) {
    case arc_proxy::FileDescriptor::SOCKET:
      return std::make_unique<SocketStream>(std::move(fd));
    case arc_proxy::FileDescriptor::FIFO_READ:
    case arc_proxy::FileDescriptor::FIFO_WRITE:
      return std::make_unique<PipeStream>(std::move(fd));
    case arc_proxy::FileDescriptor::REGULAR_FILE:
      return std::make_unique<FileStream>(std::move(fd));
    case arc_proxy::FileDescriptor::DMABUF:
      return nullptr;
    default:
      LOG(ERROR) << "Unknown FileDescriptor::Type: " << fd_type;
      return nullptr;
  }
}

}  // namespace

VSockProxy::VSockProxy(Delegate* delegate, base::ScopedFD vsock)
    : delegate_(delegate),
      vsock_(std::move(vsock)),
      next_handle_(delegate_->GetType() == Type::SERVER ? 1 : -1),
      next_cookie_(delegate_->GetType() == Type::SERVER ? 1 : -1) {
  // Note: this needs to be initialized after mWeakFactory, which is
  // declared after mVSockController in order to destroy it first.
  vsock_controller_ = base::FileDescriptorWatcher::WatchReadable(
      vsock_.Get(), base::BindRepeating(&VSockProxy::OnVSockReadReady,
                                        weak_factory_.GetWeakPtr()));
}

VSockProxy::~VSockProxy() {
  Stop();
}

int64_t VSockProxy::RegisterFileDescriptor(
    base::ScopedFD fd,
    arc_proxy::FileDescriptor::Type fd_type,
    int64_t handle) {
  if (!fd.is_valid()) {
    LOG(ERROR) << "Registering invalid fd.";
    return 0;
  }

  const int raw_fd = fd.get();
  if (handle == 0) {
    // TODO(hidehiko): Ensure handle is unique in case of overflow.
    if (delegate_->GetType() == Type::SERVER)
      handle = next_handle_++;
    else
      handle = next_handle_--;
  }

  auto stream = CreateStream(std::move(fd), fd_type);
  std::unique_ptr<base::FileDescriptorWatcher::Controller> controller;
  if (fd_type != arc_proxy::FileDescriptor::REGULAR_FILE &&
      fd_type != arc_proxy::FileDescriptor::DMABUF) {
    controller = base::FileDescriptorWatcher::WatchReadable(
        raw_fd, base::BindRepeating(&VSockProxy::OnLocalFileDesciptorReadReady,
                                    weak_factory_.GetWeakPtr(), handle));
  }
  fd_map_.emplace(handle,
                  FileDescriptorInfo{std::move(stream), std::move(controller)});

  // TODO(hidehiko): Info looks too verbose. Reduce it when we are ready.
  LOG(INFO) << "New FD is created: raw_fd=" << raw_fd << ", handle=" << handle;
  return handle;
}

void VSockProxy::Connect(const base::FilePath& path, ConnectCallback callback) {
  const int64_t cookie = GenerateCookie();

  arc_proxy::VSockMessage message;
  auto* request = message.mutable_connect_request();
  request->set_cookie(cookie);
  request->set_path(path.value());
  pending_connect_.emplace(cookie, std::move(callback));
  if (!vsock_.Write(message))
    Stop();
}

void VSockProxy::Pread(int64_t handle,
                       uint64_t count,
                       uint64_t offset,
                       PreadCallback callback) {
  const int64_t cookie = GenerateCookie();

  arc_proxy::VSockMessage message;
  auto* request = message.mutable_pread_request();
  request->set_cookie(cookie);
  request->set_handle(handle);
  request->set_count(count);
  request->set_offset(offset);
  pending_pread_.emplace(cookie, std::move(callback));
  if (!vsock_.Write(message))
    Stop();
}

void VSockProxy::Fstat(int64_t handle, FstatCallback callback) {
  const int64_t cookie = GenerateCookie();

  arc_proxy::VSockMessage message;
  auto* request = message.mutable_fstat_request();
  request->set_cookie(cookie);
  request->set_handle(handle);
  pending_fstat_.emplace(cookie, std::move(callback));
  if (!vsock_.Write(message))
    Stop();
}

void VSockProxy::Close(int64_t handle) {
  arc_proxy::VSockMessage message;
  message.mutable_close()->set_handle(handle);
  if (!vsock_.Write(message))
    Stop();
}

void VSockProxy::OnVSockReadReady() {
  arc_proxy::VSockMessage message;
  if (!vsock_.Read(&message) || !HandleMessage(&message))
    Stop();
}

bool VSockProxy::HandleMessage(arc_proxy::VSockMessage* message) {
  switch (message->command_case()) {
    case arc_proxy::VSockMessage::kClose:
      return OnClose(message->mutable_close());
    case arc_proxy::VSockMessage::kData:
      return OnData(message->mutable_data());
    case arc_proxy::VSockMessage::kConnectRequest:
      return OnConnectRequest(message->mutable_connect_request());
    case arc_proxy::VSockMessage::kConnectResponse:
      return OnConnectResponse(message->mutable_connect_response());
    case arc_proxy::VSockMessage::kPreadRequest:
      return OnPreadRequest(message->mutable_pread_request());
    case arc_proxy::VSockMessage::kPreadResponse:
      return OnPreadResponse(message->mutable_pread_response());
    case arc_proxy::VSockMessage::kFstatRequest:
      return OnFstatRequest(message->mutable_fstat_request());
    case arc_proxy::VSockMessage::kFstatResponse:
      return OnFstatResponse(message->mutable_fstat_response());
    default:
      LOG(ERROR) << "Unknown message type: " << message->command_case();
      return false;
  }
}

void VSockProxy::Stop() {
  if (!vsock_controller_)  // Do nothing if already stopped.
    return;

  // Run all pending callbacks.
  for (auto& x : pending_fstat_) {
    FstatCallback& callback = x.second;
    std::move(callback).Run(ECONNREFUSED, 0);
  }
  for (auto& x : pending_pread_) {
    PreadCallback& callback = x.second;
    std::move(callback).Run(ECONNREFUSED, std::string());
  }
  for (auto& x : pending_connect_) {
    ConnectCallback& callback = x.second;
    std::move(callback).Run(ECONNREFUSED, 0);
  }
  // Clear registered file descriptors.
  fd_map_.clear();
  // Stop watching the vsock.
  vsock_controller_.reset();

  delegate_->OnStopped();
}

bool VSockProxy::OnClose(arc_proxy::Close* close) {
  LOG(INFO) << "Closing: " << close->handle();
  auto it = fd_map_.find(close->handle());
  if (it == fd_map_.end()) {
    LOG(ERROR) << "Couldn't find handle: handle=" << close->handle();
    return false;
  }
  fd_map_.erase(it);
  return true;
}

bool VSockProxy::OnData(arc_proxy::Data* data) {
  auto it = fd_map_.find(data->handle());
  if (it == fd_map_.end()) {
    LOG(ERROR) << "Couldn't find handle: handle=" << data->handle();
    return false;
  }

  // First, create file descriptors for the received message.
  std::vector<base::ScopedFD> transferred_fds;
  transferred_fds.reserve(data->transferred_fd().size());
  for (const auto& transferred_fd : data->transferred_fd()) {
    base::ScopedFD local_fd;
    base::ScopedFD remote_fd;
    switch (transferred_fd.type()) {
      case arc_proxy::FileDescriptor::FIFO_READ: {
        auto created = CreatePipe();
        if (!created)
          return false;
        std::tie(remote_fd, local_fd) = std::move(*created);
        break;
      }
      case arc_proxy::FileDescriptor::FIFO_WRITE: {
        auto created = CreatePipe();
        if (!created)
          return false;
        std::tie(local_fd, remote_fd) = std::move(*created);
        break;
      }
      case arc_proxy::FileDescriptor::SOCKET: {
        auto created = CreateSocketPair();
        if (!created)
          return false;
        std::tie(local_fd, remote_fd) = std::move(*created);
        break;
      }
      default: {
        remote_fd = delegate_->ConvertProtoToFileDescriptor(transferred_fd);
        if (!remote_fd.is_valid())
          return false;
      }
    }

    // |local_fd| is set iff the descriptor's read readiness needs to be
    // watched, so register it.
    if (local_fd.is_valid()) {
      RegisterFileDescriptor(std::move(local_fd), transferred_fd.type(),
                             transferred_fd.handle());
    }
    transferred_fds.emplace_back(std::move(remote_fd));
  }

  // TODO(hashimoto): Redesign stream interface for better write error handling.
  // SocketStream::Write() doesn't return error as it's async.
  if (!it->second.stream->Write(std::move(*data->mutable_blob()),
                                std::move(transferred_fds))) {
    fd_map_.erase(it);
    Close(data->handle());
  }
  return true;
}

bool VSockProxy::OnConnectRequest(arc_proxy::ConnectRequest* request) {
  LOG(INFO) << "Connecting to " << request->path();
  arc_proxy::VSockMessage reply;
  auto* response = reply.mutable_connect_response();

  // Currently, this actually uses only on ArcBridgeService's initial
  // connection establishment, and the request comes from the guest to the host
  // including the |path|.
  // TODO(hidehiko): Consider whitelist the path which is allowed to access.
  auto result = ConnectUnixDomainSocket(base::FilePath(request->path()));
  response->set_cookie(request->cookie());
  response->set_error_code(result.first);
  if (result.first == 0) {
    response->set_handle(RegisterFileDescriptor(
        std::move(result.second), arc_proxy::FileDescriptor::SOCKET,
        0 /* generate handle */));
  }
  return vsock_.Write(reply);
}

bool VSockProxy::OnConnectResponse(arc_proxy::ConnectResponse* response) {
  auto it = pending_connect_.find(response->cookie());
  if (it == pending_connect_.end()) {
    LOG(ERROR) << "Unexpected connect response: cookie=" << response->cookie();
    return false;
  }

  auto callback = std::move(it->second);
  pending_connect_.erase(it);
  std::move(callback).Run(response->error_code(), response->handle());
  return true;
}

bool VSockProxy::OnPreadRequest(arc_proxy::PreadRequest* request) {
  arc_proxy::VSockMessage reply;
  auto* response = reply.mutable_pread_response();
  response->set_cookie(request->cookie());

  OnPreadRequestInternal(request, response);

  return vsock_.Write(reply);
}

void VSockProxy::OnPreadRequestInternal(arc_proxy::PreadRequest* request,
                                        arc_proxy::PreadResponse* response) {
  auto it = fd_map_.find(request->handle());
  if (it == fd_map_.end()) {
    LOG(ERROR) << "Couldn't find handle: handle=" << request->handle();
    response->set_error_code(EBADF);
    return;
  }

  if (!it->second.stream->Pread(request->count(), request->offset(),
                                response)) {
    response->set_error_code(EINVAL);
    return;
  }
}

bool VSockProxy::OnPreadResponse(arc_proxy::PreadResponse* response) {
  auto it = pending_pread_.find(response->cookie());
  if (it == pending_pread_.end()) {
    LOG(ERROR) << "Unexpected pread response: cookie=" << response->cookie();
    return false;
  }

  auto callback = std::move(it->second);
  pending_pread_.erase(it);
  std::move(callback).Run(response->error_code(),
                          std::move(*response->mutable_blob()));
  return true;
}

bool VSockProxy::OnFstatRequest(arc_proxy::FstatRequest* request) {
  arc_proxy::VSockMessage reply;
  auto* response = reply.mutable_fstat_response();
  response->set_cookie(request->cookie());

  OnFstatRequestInternal(request, response);

  return vsock_.Write(reply);
}

void VSockProxy::OnFstatRequestInternal(arc_proxy::FstatRequest* request,
                                        arc_proxy::FstatResponse* response) {
  auto it = fd_map_.find(request->handle());
  if (it == fd_map_.end()) {
    LOG(ERROR) << "Couldn't find handle: handle=" << request->handle();
    response->set_error_code(EBADF);
    return;
  }

  if (!it->second.stream->Fstat(response)) {
    // According to man, it seems like stat family needs to be supported for
    // all file descriptor types, so there's no good errno is defined to reject
    // the request. Thus, use EOPNOTSUPP, meaning the fstat is not supported.
    response->set_error_code(EOPNOTSUPP);
    return;
  }
}

bool VSockProxy::OnFstatResponse(arc_proxy::FstatResponse* response) {
  auto it = pending_fstat_.find(response->cookie());
  if (it == pending_fstat_.end()) {
    LOG(ERROR) << "Unexpected pread response: cookie=" << response->cookie();
    return false;
  }

  auto callback = std::move(it->second);
  pending_fstat_.erase(it);
  std::move(callback).Run(response->error_code(), response->size());
  return true;
}

void VSockProxy::OnLocalFileDesciptorReadReady(int64_t handle) {
  auto it = fd_map_.find(handle);
  if (it == fd_map_.end()) {
    LOG(ERROR) << "Unknown FD gets read ready: handle=" << handle;
    return;
  }

  auto read_result = it->second.stream->Read();
  arc_proxy::VSockMessage message;
  if (read_result.error_code != 0) {
    LOG(ERROR) << "Failed to read from file descriptor. handle=" << handle;
    // Notify the other side to close.
    message.mutable_close();
  } else if (read_result.blob.empty() && read_result.fds.empty()) {
    // Read empty message, i.e. reached EOF.
    message.mutable_close();
  } else if (!ConvertDataToVSockMessage(std::move(read_result.blob),
                                        std::move(read_result.fds), &message)) {
    // Failed to convert read result into proto.
    message.Clear();
    message.mutable_close();
  }

  if (message.has_close()) {
    // In case of EOF on the other side of the |fd|, |fd| needs to be closed.
    // Otherwise it will be kept read-ready and this callback will be
    // repeatedly called.
    LOG(INFO) << "Closing: handle=" << handle;
    message.mutable_close()->set_handle(handle);
    // Close the corresponding fd, too.
    fd_map_.erase(it);
  } else {
    DCHECK(message.has_data());
    message.mutable_data()->set_handle(handle);
  }
  if (!vsock_.Write(message))
    Stop();
}

bool VSockProxy::ConvertDataToVSockMessage(std::string blob,
                                           std::vector<base::ScopedFD> fds,
                                           arc_proxy::VSockMessage* message) {
  DCHECK(!blob.empty() || !fds.empty());

  // Build returning message.
  auto* data = message->mutable_data();
  *data->mutable_blob() = std::move(blob);
  for (auto& fd : fds) {
    auto* transferred_fd = data->add_transferred_fd();

    struct stat st;
    if (fstat(fd.get(), &st) == -1) {
      PLOG(ERROR) << "Failed to fstat";
      return false;
    }

    if (S_ISFIFO(st.st_mode)) {
      int flags = fcntl(fd.get(), F_GETFL, 0);
      if (flags < 0) {
        PLOG(ERROR) << "Failed to find file status flags";
        return false;
      }
      switch (flags & O_ACCMODE) {
        case O_RDONLY:
          transferred_fd->set_type(arc_proxy::FileDescriptor::FIFO_READ);
          break;
        case O_WRONLY:
          transferred_fd->set_type(arc_proxy::FileDescriptor::FIFO_WRITE);
          break;
        default:
          LOG(ERROR) << "Unsupported access mode: " << (flags & O_ACCMODE);
          return false;
      }
    } else if (S_ISSOCK(st.st_mode)) {
      transferred_fd->set_type(arc_proxy::FileDescriptor::SOCKET);
    } else if (S_ISREG(st.st_mode)) {
      transferred_fd->set_type(arc_proxy::FileDescriptor::REGULAR_FILE);
    } else if (!delegate_->ConvertFileDescriptorToProto(fd.get(),
                                                        transferred_fd)) {
      return false;
    }
    transferred_fd->set_handle(RegisterFileDescriptor(
        std::move(fd), transferred_fd->type(), 0 /* generate handle */));
  }
  return true;
}

int64_t VSockProxy::GenerateCookie() {
  // TODO(hidehiko): Ensure cookie is unique in case of overflow.
  return delegate_->GetType() == Type::SERVER ? next_cookie_++ : next_cookie_--;
}

}  // namespace arc
