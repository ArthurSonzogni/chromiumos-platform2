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
#include <grpcpp/grpcpp.h>
#include <vm_protos/proto_bindings/container_guest.grpc.pb.h>
#include <chromeos/constants/vm_tools.h>

using std::string;

namespace vm_tools {
namespace cicerone {
namespace {

// How long to wait before timing out on regular RPCs.
constexpr int64_t kDefaultTimeoutSeconds = 60;

}  // namespace

Container::Container(const std::string& name,
                     const std::string& token,
                     base::WeakPtr<VirtualMachine> vm)
    : name_(name), token_(token), vm_(vm) {}

// Sets the container's IPv4 address.
void Container::set_ipv4_address(uint32_t ipv4_address) {
  ipv4_address_ = ipv4_address;
}

void Container::set_drivefs_mount_path(std::string drivefs_mount_path) {
  drivefs_mount_path_ = drivefs_mount_path;
}

void Container::set_homedir(const std::string& homedir) {
  homedir_ = homedir;
}

void Container::set_listening_tcp4_ports(std::vector<uint16_t> ports) {
  listening_tcp4_ports_ = std::move(ports);
}

void Container::ConnectToGarcon(const std::string& addr) {
  garcon_stub_ = std::make_unique<vm_tools::container::Garcon::Stub>(
      grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));
}

bool Container::LaunchContainerApplication(
    const std::string& desktop_file_id,
    std::vector<std::string> files,
    vm_tools::container::LaunchApplicationRequest::DisplayScaling
        display_scaling,
    std::string* out_error) {
  CHECK(out_error);
  vm_tools::container::LaunchApplicationRequest container_request;
  vm_tools::container::LaunchApplicationResponse container_response;
  container_request.set_desktop_file_id(desktop_file_id);
  std::copy(std::make_move_iterator(files.begin()),
            std::make_move_iterator(files.end()),
            google::protobuf::RepeatedFieldBackInserter(
                container_request.mutable_files()));
  container_request.set_display_scaling(display_scaling);

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

bool Container::ConnectChunnel(uint32_t chunneld_port,
                               uint32_t tcp4_port,
                               std::string* out_error) {
  vm_tools::container::ConnectChunnelRequest container_request;
  vm_tools::container::ConnectChunnelResponse container_response;
  container_request.set_chunneld_port(chunneld_port);
  container_request.set_target_tcp4_port(tcp4_port);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = garcon_stub_->ConnectChunnel(&ctx, container_request,
                                                     &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to connect chunnel in container " << name_ << ": "
               << status.error_message() << " code: " << status.error_code();
    out_error->assign("gRPC failure connecting chunnel in container: " +
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
                                    const std::string& package_name,
                                    LinuxPackageInfo* out_pkg_info,
                                    std::string* out_error) {
  CHECK(out_pkg_info);
  vm_tools::container::LinuxPackageInfoRequest container_request;
  vm_tools::container::LinuxPackageInfoResponse container_response;
  container_request.set_file_path(file_path);
  container_request.set_package_name(package_name);

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

vm_tools::container::InstallLinuxPackageResponse::Status
Container::InstallLinuxPackage(const std::string& file_path,
                               const std::string& package_id,
                               const std::string& command_uuid,
                               std::string* out_error) {
  vm_tools::container::InstallLinuxPackageRequest container_request;
  vm_tools::container::InstallLinuxPackageResponse container_response;
  container_request.set_file_path(file_path);
  container_request.set_package_id(package_id);
  container_request.set_command_uuid(command_uuid);

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

vm_tools::container::UninstallPackageOwningFileResponse::Status
Container::UninstallPackageOwningFile(const std::string& desktop_file_id,
                                      std::string* out_error) {
  vm_tools::container::UninstallPackageOwningFileRequest container_request;
  vm_tools::container::UninstallPackageOwningFileResponse container_response;
  container_request.set_desktop_file_id(desktop_file_id);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = garcon_stub_->UninstallPackageOwningFile(
      &ctx, container_request, &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to uninstall package in container " << name_ << ": "
               << status.error_message() << " code: " << status.error_code();
    out_error->assign("gRPC failure uninstalling package in container: " +
                      status.error_message());
    return vm_tools::container::UninstallPackageOwningFileResponse::FAILED;
  }
  out_error->assign(container_response.failure_reason());
  return container_response.status();
}

vm_tools::container::ApplyAnsiblePlaybookResponse::Status
Container::ApplyAnsiblePlaybook(const std::string& playbook,
                                std::string* out_error) {
  vm_tools::container::ApplyAnsiblePlaybookRequest container_request;
  vm_tools::container::ApplyAnsiblePlaybookResponse container_response;
  container_request.set_playbook(playbook);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = garcon_stub_->ApplyAnsiblePlaybook(
      &ctx, container_request, &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to apply Ansible playbook to container " << name_
               << ": " << status.error_message()
               << " code: " << status.error_code();
    out_error->assign("gRPC failure applying Ansible playbook to container: " +
                      status.error_message());
    return vm_tools::container::ApplyAnsiblePlaybookResponse::FAILED;
  }
  out_error->assign(container_response.failure_reason());
  return container_response.status();
}

vm_tools::container::ConfigureForArcSideloadResponse::Status
Container::ConfigureForArcSideload(std::string* out_error) {
  vm_tools::container::ConfigureForArcSideloadRequest container_request;
  vm_tools::container::ConfigureForArcSideloadResponse container_response;

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = garcon_stub_->ConfigureForArcSideload(
      &ctx, container_request, &container_response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to configure for arc sideloading: "
               << status.error_message() << " code: " << status.error_code();
    out_error->assign("gRPC failure configuring container for arc sideload: " +
                      status.error_message());
    return vm_tools::container::ConfigureForArcSideloadResponse::FAILED;
  }
  out_error->assign(container_response.failure_reason());
  return container_response.status();
}

}  // namespace cicerone
}  // namespace vm_tools
