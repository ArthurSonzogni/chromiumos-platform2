// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// sample usage:
// coral_console --input="(minecraft,a),(minecraft,a),(minecraft,a), \
//      (minecraft,a),(japan travel,b),(usa travel,b),(japan travel,b), \
//      (japan travel,b)" --output_file=/tmp/out.txt

#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <optional>
#include <regex>  // NOLINT(build/c++11)
#include <string>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/command_line.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <base/uuid.h>
#include <brillo/syslog_logging.h>
#include <chromeos/mojo/service_constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/connect.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "odml/mojom/coral_service.mojom-forward.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/url.mojom.h"

namespace {

constexpr const char kInput[] = "input";
constexpr const char kSuppressionContext[] = "suppression_context";
constexpr const char kOutputFile[] = "output_file";
constexpr const char kSkipSafetyCheck[] = "skip_safety_check";
constexpr int kMinItemsInGroup = 4;
constexpr int kMaxItemsInGroup = 25;
constexpr int kMaxGroupsToGenerate = 2;

constexpr base::TimeDelta kRemoteRequestTimeout = base::Seconds(10);

class FilePath;

}  // namespace

std::string GroupEntitiesToString(const coral::mojom::GroupPtr& group) {
  std::string out;
  for (const auto& entity : group->entities) {
    if (entity->is_tab()) {
      out += ("(" + entity->get_tab()->title + "," +
              entity->get_tab()->url->url + ")");
    } else {  // entity.is_app()
      out += ("(" + entity->get_app()->title + ",)");
    }
  }
  return out;
}

std::vector<coral::mojom::EntityPtr> ParseEntitiesFromInput(std::string input) {
  LOG(INFO) << "ParseTabsFromInput: " << input;
  std::vector<coral::mojom::EntityPtr> result;
  std::regex pattern("\\(([^,]+),([^)]*)\\)");
  std::smatch match;

  auto it = std::sregex_iterator(input.begin(), input.end(), pattern);
  auto end = std::sregex_iterator();
  for (; it != end; ++it) {
    match = *it;
    LOG(INFO) << "title: " << match[1].str() << ", url: " << match[2].str();
    if (match[2].str().length() > 0) {
      coral::mojom::Tab data;
      data.title = match[1].str();
      data.url = url::mojom::Url::New(match[2].str());
      result.push_back(coral::mojom::Entity::NewTab(data.Clone()));
    } else {
      coral::mojom::App data;
      data.title = match[1].str();
      result.push_back(coral::mojom::Entity::NewApp(data.Clone()));
    }
  }

  return result;
}

void WriteGroupResponseToFile(
    const coral::mojom::GroupResponsePtr& group_response,
    const base::FilePath& file) {
  std::string out;
  const std::vector<coral::mojom::GroupPtr>& groups = group_response->groups;
  for (const auto& group : groups) {
    out += GroupEntitiesToString(group);
    out += ("$$$" + group->title.value_or("[NO TITLE]") + "\n");
  }
  base::WriteFile(file, out);
}

void HandleGroupResult(base::RunLoop* run_loop,
                       std::optional<const base::FilePath> output_file,
                       const base::TimeTicks& request_time,
                       coral::mojom::GroupResultPtr result) {
  if (result->is_error()) {
    LOG(FATAL) << "Coral group request failed with CoralError code: "
               << static_cast<int>(result->get_error());
  }
  coral::mojom::GroupResponsePtr group_response =
      std::move(result->get_response());

  // Print human-friendly response in stdout.
  LOG(INFO) << "Coral group request succeeded with "
            << group_response->groups.size() << " groups in "
            << (base::TimeTicks::Now() - request_time).InMilliseconds()
            << " ms.";
  for (size_t i = 0; i < group_response->groups.size(); i++) {
    LOG(INFO) << "Group " << base::NumberToString(i + 1) << " has title `"
              << group_response->groups[i]->title.value_or("[NO TITLE]")
              << "` and " << group_response->groups[i]->entities.size()
              << " entities: ";
    LOG(INFO) << GroupEntitiesToString(group_response->groups[i]);
  }

  // Write output to file if `--output_file` arg provided
  if (output_file.has_value()) {
    WriteGroupResponseToFile(group_response, output_file.value());
  }
  run_loop->Quit();
}

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("thread_pool");

  base::SingleThreadTaskExecutor io_task_executor(base::MessagePumpType::IO);
  mojo::core::Init();

  mojo::core::ScopedIPCSupport ipc_support(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  mojo::Remote<coral::mojom::CoralService> coral_service;
  mojo::Remote<coral::mojom::CoralProcessor> coral_processor;

  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager;

  auto service_manager_remote =
      chromeos::mojo_service_manager::ConnectToMojoServiceManager();

  if (!service_manager_remote) {
    LOG(ERROR) << "Failed to connect to Mojo Service Manager";
    return -1;
  }

  service_manager.Bind(std::move(service_manager_remote));
  service_manager.set_disconnect_with_reason_handler(
      base::BindOnce([](uint32_t error, const std::string& message) {
        LOG(FATAL) << "Disconnected from mojo service manager (the mojo "
                      "broker process). Error: "
                   << error << ", message: " << message
                   << ". Shutdown and wait for respawn.";
      }));

  service_manager->Request(
      /*service_name=*/chromeos::mojo_services::kCrosCoralService,
      /*timeout=*/kRemoteRequestTimeout,
      coral_service.BindNewPipeAndPassReceiver().PassPipe());
  coral_service.set_disconnect_with_reason_handler(
      base::BindOnce([](uint32_t error, const std::string& reason) {
        LOG(FATAL) << "Coral service disconnected, error: " << error
                   << ", reason: " << reason;
      }));
  CHECK(coral_service && coral_service.is_bound() &&
        coral_service.is_connected())
      << "Cannot receive CoralService from mojo service manager";

  // Currently it is not possible to obtain ML Service outside Chrome. This
  // means the coral_console can only be run after Chrome initializes the
  // CoralProcessor for us.
  coral_service->Initialize(mojo::NullRemote(),
                            coral_processor.BindNewPipeAndPassReceiver());
  coral_processor.set_disconnect_with_reason_handler(
      base::BindOnce([](uint32_t error, const std::string& reason) {
        LOG(FATAL) << "Coral service disconnected, error: " << error
                   << ", reason: " << reason;
      }));
  CHECK(coral_processor && coral_processor.is_bound() &&
        coral_processor.is_connected())
      << "Cannot initialize CoralProcessor";

  auto group_request = coral::mojom::GroupRequest::New();

  group_request->embedding_options = coral::mojom::EmbeddingOptions::New();
  group_request->embedding_options->check_safety_filter =
      !cl->HasSwitch(kSkipSafetyCheck);
  group_request->clustering_options = coral::mojom::ClusteringOptions::New();
  group_request->clustering_options->min_items_in_cluster = kMinItemsInGroup;
  group_request->clustering_options->max_items_in_cluster = kMaxItemsInGroup;
  group_request->clustering_options->max_clusters = kMaxGroupsToGenerate;
  group_request->title_generation_options =
      coral::mojom::TitleGenerationOptions::New();

  CHECK(cl->HasSwitch(kInput));
  group_request->entities =
      ParseEntitiesFromInput(cl->GetSwitchValueNative(kInput));
  if (cl->HasSwitch(kSuppressionContext)) {
    group_request->suppression_context =
        ParseEntitiesFromInput(cl->GetSwitchValueNative(kSuppressionContext));
  }

  mojo::PendingRemote<coral::mojom::TitleObserver> observer;

  std::optional<base::FilePath> output_path;
  if (cl->HasSwitch(kOutputFile)) {
    output_path = cl->GetSwitchValuePath(kOutputFile);
  }
  base::RunLoop run_loop;
  coral_processor->Group(std::move(group_request), std::move(observer),
                         base::BindOnce(&HandleGroupResult, &run_loop,
                                        output_path, base::TimeTicks::Now()));
  run_loop.Run();
}
