// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/test/test_timeouts.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/dbus/dbus_object_test_helpers.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/bus.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <dbus/dbus.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <gmock/gmock.h>
#include <google/protobuf/stubs/logging.h>
#include <libhwsec/factory/fuzzed_factory.h>
#include <tpm_manager/client/mock_tpm_manager_utility.h>

#include "cryptohome/fake_platform.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/fuzzers/fuzzed_proto_generator.h"
#include "cryptohome/mock_uss_experiment_config_fetcher.h"
#include "cryptohome/platform.h"
#include "cryptohome/service_userdataauth.h"
#include "cryptohome/storage/cryptohome_vault_factory.h"
#include "cryptohome/storage/encrypted_container/backing_device_factory.h"
#include "cryptohome/storage/encrypted_container/encrypted_container_factory.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/keyring/fake_keyring.h"
#include "cryptohome/storage/mock_mount_factory.h"
#include "cryptohome/storage/mount_factory.h"
#include "cryptohome/userdataauth.h"

namespace cryptohome {
namespace {

using ::brillo::Blob;
using ::brillo::BlobFromString;
using ::testing::_;
using ::testing::NiceMock;

// Performs initialization and holds state that's shared across all invocations
// of the fuzzer.
class Environment {
 public:
  Environment() {
    base::CommandLine::Init(0, nullptr);
    TestTimeouts::Initialize();
    // Suppress log spam from the code-under-test.
    logging::SetMinLogLevel(logging::LOGGING_FATAL);
  }

  base::test::TaskEnvironment& task_environment() { return task_environment_; }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  // Suppress log spam from protobuf helpers that complain about malformed
  // inputs.
  google::protobuf::LogSilencer log_silencer_;
};

std::unique_ptr<CryptohomeVaultFactory> CreateVaultFactory(
    Platform& platform, FuzzedDataProvider& provider) {
  // Only stub out `Keyring`, because unlike other classes its real
  // implementation does platform operations that don't go through `Platform`.
  auto container_factory = std::make_unique<EncryptedContainerFactory>(
      &platform, std::make_unique<FakeKeyring>(),
      std::make_unique<BackingDeviceFactory>(&platform));
  container_factory->set_allow_fscrypt_v2(provider.ConsumeBool());
  auto vault_factory = std::make_unique<CryptohomeVaultFactory>(
      &platform, std::move(container_factory));
  vault_factory->set_enable_application_containers(provider.ConsumeBool());
  return vault_factory;
}

std::unique_ptr<MountFactory> CreateMountFactory() {
  auto mount_factory = std::make_unique<MockMountFactory>();
  auto* mount_factory_ptr = mount_factory.get();
  // Configure the usage of in-process mount helper, as out-of-process
  // mounting is not fuzzing-compatible.
  EXPECT_CALL(*mount_factory, New(_, _, _, _, _))
      .WillRepeatedly([=](Platform* platform, HomeDirs* homedirs,
                          bool legacy_mount, bool bind_mount_downloads,
                          bool /*use_local_mounter*/) {
        return mount_factory_ptr->NewConcrete(platform, homedirs, legacy_mount,
                                              bind_mount_downloads,
                                              /*use_local_mounter=*/true);
      });
  return mount_factory;
}

std::string GenerateFuzzedDBusMethodName(
    const brillo::dbus_utils::DBusObject& dbus_object,
    const std::string& dbus_interface_name,
    FuzzedDataProvider& provider) {
  // The value to return if the code below fails to generate a valid one. It
  // must satisfy D-Bus restrictions on method names (e.g., be nonempty).
  static constexpr char kFallbackName[] = "foo";
  DCHECK(dbus_validate_member(kFallbackName, /*error=*/nullptr));

  const brillo::dbus_utils::DBusInterface* const dbus_interface =
      dbus_object.FindInterface(dbus_interface_name);
  CHECK(dbus_interface);

  // Generate the method name either by picking one of exported methods or by
  // creating a "random" string.
  const std::vector<std::string> exported_method_names =
      dbus_interface->GetMethodNames();
  // The max value in the range here is used to trigger the random generation.
  const size_t selected_method_index =
      provider.ConsumeIntegralInRange<size_t>(0, exported_method_names.size());
  if (selected_method_index < exported_method_names.size()) {
    return exported_method_names[selected_method_index];
  }

  std::string fuzzed_name = provider.ConsumeRandomLengthString();
  if (!dbus_validate_member(fuzzed_name.c_str(), /*error=*/nullptr)) {
    return kFallbackName;
  }
  return fuzzed_name;
}

std::unique_ptr<dbus::MethodCall> GenerateFuzzedDBusCallMessage(
    const brillo::dbus_utils::DBusObject& dbus_object,
    const std::string& dbus_interface_name,
    const std::string& dbus_method_name,
    const std::vector<Blob>& breadcrumbs,
    FuzzedDataProvider& provider) {
  auto dbus_call =
      std::make_unique<dbus::MethodCall>(dbus_interface_name, dbus_method_name);
  // The serial number can be hardcoded, since we never perform concurrent D-Bus
  // requests in the fuzzer.
  dbus_call->SetSerial(1);

  // Construct "random" arguments for the D-Bus call.
  dbus::MessageWriter dbus_writer(dbus_call.get());
  if (provider.ConsumeBool()) {
    FuzzedProtoGenerator generator(breadcrumbs, provider);
    Blob argument = generator.Generate();
    dbus_writer.AppendArrayOfBytes(argument.data(), argument.size());
  }

  return dbus_call;
}

std::unique_ptr<dbus::Response> RunBlockingDBusCall(
    std::unique_ptr<dbus::MethodCall> method_call_message,
    brillo::dbus_utils::DBusObject& dbus_object) {
  // Obtain the interface object for the name specified in the call.
  brillo::dbus_utils::DBusInterface* const dbus_interface =
      dbus_object.FindInterface(method_call_message->GetInterface());
  CHECK(dbus_interface);
  // Start the call.
  base::test::TestFuture<std::unique_ptr<dbus::Response>> dbus_response_future;
  brillo::dbus_utils::DBusInterfaceTestHelper::HandleMethodCall(
      dbus_interface, method_call_message.get(),
      dbus_response_future.GetCallback());
  // Wait for the reply and return it.
  return dbus_response_future.Take();
}

// Add new interesting blobs to `breadcrumbs` from `dbus_response`, if there's
// any (i.e., a reply field which we should try using in subsequent requests).
void UpdateBreadcrumbs(const std::string& dbus_method_name,
                       std::unique_ptr<dbus::Response> dbus_response,
                       std::vector<Blob>& breadcrumbs) {
  DCHECK(dbus_response);
  dbus::MessageReader reader(dbus_response.get());
  if (dbus_method_name == user_data_auth::kStartAuthSession) {
    user_data_auth::StartAuthSessionReply start_auth_session_reply;
    if (reader.PopArrayOfBytesAsProto(&start_auth_session_reply) &&
        !start_auth_session_reply.auth_session_id().empty()) {
      // Keep as a breadcrumb the AuthSessionId which the code-under-test
      // returned, so that the fuzzer can realistically test multiple D-Bus
      // calls against the same AuthSession (the IDs are random tokens, which
      // Libfuzzer can't "guess" itself).
      breadcrumbs.push_back(
          BlobFromString(start_auth_session_reply.auth_session_id()));
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  FuzzedDataProvider provider(data, size);

  // Prepare `UserDataAuth`'s dependencies.
  FakePlatform platform;
  std::unique_ptr<CryptohomeVaultFactory> vault_factory =
      CreateVaultFactory(platform, provider);
  std::unique_ptr<MountFactory> mount_factory = CreateMountFactory();
  hwsec::FuzzedFactory hwsec_factory(provider);
  NiceMock<tpm_manager::MockTpmManagerUtility> tpm_manager_utility;
  NiceMock<MockUssExperimentConfigFetcher> uss_experiment_config_fetcher;
  auto bus =
      base::MakeRefCounted<NiceMock<dbus::MockBus>>(dbus::Bus::Options());
  auto mount_thread_bus =
      base::MakeRefCounted<NiceMock<dbus::MockBus>>(dbus::Bus::Options());

  // Prepare `UserDataAuth`. Set up a single-thread mode (which is not how the
  // daemon works in production, but allows faster and reproducible fuzzing).
  auto userdataauth = std::make_unique<UserDataAuth>();
  userdataauth->set_mount_task_runner(base::ThreadTaskRunnerHandle::Get());
  userdataauth->set_platform(&platform);
  userdataauth->set_vault_factory_for_testing(vault_factory.get());
  userdataauth->set_mount_factory_for_testing(mount_factory.get());
  userdataauth->set_dbus(bus);
  userdataauth->set_mount_thread_dbus(mount_thread_bus);
  userdataauth->set_hwsec_factory(&hwsec_factory);
  userdataauth->set_tpm_manager_util_(&tpm_manager_utility);
  userdataauth->set_uss_experiment_config_fetcher(
      &uss_experiment_config_fetcher);
  if (!userdataauth->Initialize()) {
    // This should be a rare case (e.g., the mocked system salt writing failed).
    return 0;
  }
  CHECK(userdataauth->PostDBusInitialize());

  // Prepare `UserDataAuthAdaptor`. D-Bus handlers of the code-under-test become
  // registered on the given stub D-Bus object.
  brillo::dbus_utils::DBusObject dbus_object(
      /*object_manager=*/nullptr, /*bus=*/nullptr, /*object_path=*/{});
  UserDataAuthAdaptor userdataauth_adaptor(bus, &dbus_object,
                                           userdataauth.get());
  userdataauth_adaptor.RegisterAsync();

  // Simulate a few D-Bus calls on the stub D-Bus object using "random"
  // parameters.
  // `breadcrumbs` contain blobs which are useful to reuse across multiple calls
  // but which Libfuzzer cannot realistically generate itself.
  std::vector<Blob> breadcrumbs;
  while (provider.remaining_bytes() > 0) {
    const std::string dbus_method_name = GenerateFuzzedDBusMethodName(
        dbus_object, user_data_auth::kUserDataAuthInterface, provider);
    std::unique_ptr<dbus::MethodCall> dbus_call = GenerateFuzzedDBusCallMessage(
        dbus_object, user_data_auth::kUserDataAuthInterface, dbus_method_name,
        breadcrumbs, provider);
    std::unique_ptr<dbus::Response> dbus_response =
        RunBlockingDBusCall(std::move(dbus_call), dbus_object);
    if (dbus_response) {
      UpdateBreadcrumbs(dbus_method_name, std::move(dbus_response),
                        breadcrumbs);
    }
  }

  // TODO(b/258547478): Remove this after `UserDataAuth` and
  // `UserDataAuthAdaptor` lifetime issues are resolved (they post tasks with
  // unretained pointers).
  env.task_environment().RunUntilIdle();

  return 0;
}

}  // namespace cryptohome
