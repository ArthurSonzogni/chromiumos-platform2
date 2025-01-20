// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <iostream>
#include <optional>
#include <utility>

#include <base/check.h>
#include <base/command_line.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <brillo/syslog_logging.h>
#include <chromeos/mojo/service_constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo_service_manager/lib/connect.h>

#include "odml/cros_safety/safety_service_manager.h"
#include "odml/cros_safety/safety_service_manager_impl.h"
#include "odml/mojom/big_buffer.mojom.h"
#include "odml/mojom/cros_safety.mojom.h"

namespace {

constexpr const char kText[] = "text";
constexpr const char kImage[] = "image";
constexpr const char kRuleset[] = "ruleset";

class FilePath;

}  // namespace

std::optional<cros_safety::mojom::SafetyRuleset> GetRulesetFromCommandLine(
    base::CommandLine* cl) {
  if (!cl->HasSwitch(kRuleset)) {
    LOG(INFO) << "Ruleset arg not provided, using default ruleset.";
    return std::nullopt;
  }
  std::string ruleset = cl->GetSwitchValueASCII(kRuleset);
  std::transform(ruleset.begin(), ruleset.end(), ruleset.begin(), ::tolower);
  LOG(INFO) << "using safety ruleset: " << ruleset;
  if (ruleset == "generic") {
    return cros_safety::mojom::SafetyRuleset::kGeneric;
  } else if (ruleset == "coral") {
    return cros_safety::mojom::SafetyRuleset::kCoral;
  } else if (ruleset == "mantis") {
    return cros_safety::mojom::SafetyRuleset::kMantis;
  } else if (ruleset == "mantis-input-image") {
    return cros_safety::mojom::SafetyRuleset::kMantisInputImage;
  } else if (ruleset == "mantis-output-image") {
    return cros_safety::mojom::SafetyRuleset::kMantisOutputImage;
  } else if (ruleset == "mantis-generated-region") {
    return cros_safety::mojom::SafetyRuleset::kMantisGeneratedRegion;
  }
  LOG(ERROR) << "Unrecognized safety ruleset: " << ruleset;
  return std::nullopt;
}

void OnClassifyComplete(base::RunLoop* run_loop,
                        cros_safety::mojom::SafetyClassifierVerdict result) {
  std::cout << result << std::endl;
  run_loop->Quit();
}

void FilterImageWithCloudClassifier(
    base::CommandLine* cl,
    raw_ref<cros_safety::SafetyServiceManager> safety_service_manager) {
  std::optional<std::string> text;
  if (cl->HasSwitch(kText)) {
    text = cl->GetSwitchValueNative(kText);
  }

  base::FilePath image_path = cl->GetSwitchValuePath(kImage);
  CHECK(!image_path.empty() && base::PathExists(image_path));

  std::optional<std::vector<uint8_t>> image_bytes =
      base::ReadFileToBytes(image_path);
  CHECK(image_bytes.has_value() && image_bytes->size() > 0);

  LOG(INFO) << "Run cloud session ClassifyImageSafety";
  base::RunLoop run_loop;

  // Call ClassifyImageSafety with default ruleset kMantis.
  safety_service_manager->ClassifyImageSafety(
      GetRulesetFromCommandLine(cl).value_or(
          cros_safety::mojom::SafetyRuleset::kMantis),
      text, mojo_base::mojom::BigBuffer::NewBytes(image_bytes.value()),
      base::BindOnce(&OnClassifyComplete, &run_loop));
  run_loop.Run();
}

void FilterTextWithOnDeviceClassifier(
    base::CommandLine* cl,
    raw_ref<cros_safety::SafetyServiceManager> safety_service_manager) {
  std::string text = cl->GetSwitchValueNative(kText);
  CHECK(!text.empty());

  LOG(INFO) << "Run on-device session ClassifyTextSafety";
  base::RunLoop run_loop;

  // Call ClassifyTextSafety with ruleset kCoral.
  safety_service_manager->ClassifyTextSafety(
      GetRulesetFromCommandLine(cl).value_or(
          cros_safety::mojom::SafetyRuleset::kCoral),
      text, base::BindOnce(&OnClassifyComplete, &run_loop));
  run_loop.Run();
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

  cros_safety::SafetyServiceManagerImpl safety_service_manager(service_manager);

  if (cl->HasSwitch(kImage)) {
    // Filter image with cloud classifier
    FilterImageWithCloudClassifier(cl, raw_ref(safety_service_manager));
  } else {
    // Filter text using on-device classifier
    FilterTextWithOnDeviceClassifier(cl, raw_ref(safety_service_manager));
  }
}
