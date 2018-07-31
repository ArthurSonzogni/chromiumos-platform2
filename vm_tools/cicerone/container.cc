// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/cicerone/container.h"

#include <arpa/inet.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/guid.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/stringprintf.h>
#include <google/protobuf/repeated_field.h>
#include <grpc++/grpc++.h>

#include "container_guest.grpc.pb.h"  // NOLINT(build/include)
#include "vm_tools/common/constants.h"

using std::string;

namespace vm_tools {
namespace cicerone {
namespace {

// How long to wait before timing out on regular RPCs.
constexpr int64_t kDefaultTimeoutSeconds = 2;

}  // namespace

Container::Container(const std::string& name,
                     const std::string& token,
                     base::WeakPtr<VirtualMachine> vm)
    : name_(name), token_(token), vm_(vm) {}

// Sets the container's IPv4 address.
void Container::set_ipv4_address(uint32_t ipv4_address) {
  ipv4_address_ = ipv4_address;
}

void Container::ConnectToGarcon(const std::string& addr) {
  garcon_channel_ =
      grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
  garcon_stub_ =
      std::make_unique<vm_tools::container::Garcon::Stub>(garcon_channel_);
}

bool Container::LaunchContainerApplication(const std::string& desktop_file_id,
                                           std::vector<std::string> files,
                                           std::string* out_error) {
  CHECK(out_error);
  vm_tools::container::LaunchApplicationRequest container_request;
  vm_tools::container::LaunchApplicationResponse container_response;
  container_request.set_desktop_file_id(desktop_file_id);
  std::copy(std::make_move_iterator(files.begin()),
            std::make_move_iterator(files.end()),
            google::protobuf::RepeatedFieldBackInserter(
                container_request.mutable_files()));

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = garcon_stub_->LaunchApplication(&ctx, container_request,
                                                        &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to launch application " << desktop_file_id
               << " in container " << name_ << ": " << status.error_message();
    out_error->assign("gRPC failure launching application: " +
                      status.error_message());
    return false;
  }
  out_error->assign(container_response.failure_reason());
  return container_response.success();
}

bool Container::LaunchVshd(uint32_t port, std::string* out_error) {
  vm_tools::container::LaunchVshdRequest container_request;
  vm_tools::container::LaunchVshdResponse container_response;
  container_request.set_port(port);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      garcon_stub_->LaunchVshd(&ctx, container_request, &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to launch vshd in container " << name_ << ": "
               << status.error_message() << " code: " << status.error_code();
    out_error->assign("gRPC failure launching vshd in container: " +
                      status.error_message());
    return false;
  }
  out_error->assign(container_response.failure_reason());
  return container_response.success();
}

bool Container::GetDebugInformation(std::string* out) {
  vm_tools::container::GetDebugInformationRequest container_request;
  vm_tools::container::GetDebugInformationResponse container_response;

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = garcon_stub_->GetDebugInformation(
      &ctx, container_request, &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to get debug information in container " << name_
               << ": " << status.error_message()
               << " code: " << status.error_code();
    out->assign("gRPC failure to get debug information in container: " +
                status.error_message());
    return false;
  }
  out->assign(container_response.debug_information());
  return true;
}

bool Container::GetContainerAppIcon(std::vector<std::string> desktop_file_ids,
                                    uint32_t icon_size,
                                    uint32_t scale,
                                    std::vector<Icon>* icons) {
  CHECK(icons);

  vm_tools::container::IconRequest container_request;
  vm_tools::container::IconResponse container_response;

  std::copy(std::make_move_iterator(desktop_file_ids.begin()),
            std::make_move_iterator(desktop_file_ids.end()),
            google::protobuf::RepeatedFieldBackInserter(
                container_request.mutable_desktop_file_ids()));
  container_request.set_icon_size(icon_size);
  container_request.set_scale(scale);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      garcon_stub_->GetIcon(&ctx, container_request, &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to get icons in container " << name_ << ": "
               << status.error_message();
    return false;
  }

  for (auto& icon : *container_response.mutable_desktop_icons()) {
    icons->emplace_back(
        Icon{.desktop_file_id = std::move(*icon.mutable_desktop_file_id()),
             .content = std::move(*icon.mutable_icon())});
  }
  return true;
}

bool Container::GetLinuxPackageInfo(const std::string& file_path,
                                    LinuxPackageInfo* out_pkg_info,
                                    std::string* out_error) {
  CHECK(out_pkg_info);
  vm_tools::container::LinuxPackageInfoRequest container_request;
  vm_tools::container::LinuxPackageInfoResponse container_response;
  container_request.set_file_path(file_path);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = garcon_stub_->GetLinuxPackageInfo(
      &ctx, container_request, &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to get Linux package info from container " << name_
               << ": " << status.error_message()
               << " code: " << status.error_code();
    out_error->assign(
        "gRPC failure getting Linux package info from container: " +
        status.error_message());
    return false;
  }
  out_error->assign(container_response.failure_reason());
  out_pkg_info->package_id = std::move(container_response.package_id());
  out_pkg_info->license = std::move(container_response.license());
  out_pkg_info->description = std::move(container_response.description());
  out_pkg_info->project_url = std::move(container_response.project_url());
  out_pkg_info->size = container_response.size();
  out_pkg_info->summary = std::move(container_response.summary());
  return container_response.success();
}

int Container::InstallLinuxPackage(const std::string& file_path,
                                   std::string* out_error) {
  vm_tools::container::InstallLinuxPackageRequest container_request;
  vm_tools::container::InstallLinuxPackageResponse container_response;
  container_request.set_file_path(file_path);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = garcon_stub_->InstallLinuxPackage(
      &ctx, container_request, &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to install Linux package in container " << name_
               << ": " << status.error_message()
               << " code: " << status.error_code();
    out_error->assign("gRPC failure installing Linux package in container: " +
                      status.error_message());
    return vm_tools::container::InstallLinuxPackageResponse::FAILED;
  }
  out_error->assign(container_response.failure_reason());
  return container_response.status();
}

bool Container::IsRunning() {
  auto channel_state = garcon_channel_->GetState(true);
  return channel_state == GRPC_CHANNEL_IDLE ||
         channel_state == GRPC_CHANNEL_CONNECTING ||
         channel_state == GRPC_CHANNEL_READY;
}

}  // namespace cicerone
}  // namespace vm_tools
