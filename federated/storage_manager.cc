// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/storage_manager.h"

#include <cstddef>
#include <memory>
#include <optional>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "federated/federated_metadata.h"
#include "federated/session_manager_proxy.h"
#include "federated/utils.h"

#if USE_LOCAL_FEDERATED_SERVER
#include <vector>
#include "federated/mojom/example.mojom.h"
#endif

namespace federated {
#if USE_LOCAL_FEDERATED_SERVER
// When we are testing against a local federated server, we want to populate
// the test server with generic test data
namespace {
using ::chromeos::federated::mojom::Example;
using ::chromeos::federated::mojom::ExamplePtr;
using ::chromeos::federated::mojom::Features;
using ::chromeos::federated::mojom::FloatList;
using ::chromeos::federated::mojom::Int64List;
using ::chromeos::federated::mojom::StringList;
using ::chromeos::federated::mojom::ValueList;
using ::chromeos::federated::mojom::ValueListPtr;

ValueListPtr CreateStringList(const std::vector<std::string>& values) {
  ValueListPtr value_list = ValueList::New();
  value_list->set_string_list(StringList::New());
  value_list->get_string_list()->value = values;
  return value_list;
}

ExamplePtr CreateExamplePtr(const std::string& query) {
  ExamplePtr example = Example::New();
  example->features = Features::New();
  auto& feature_map = example->features->feature;
  feature_map["query"] = CreateStringList({query});

  return example;
}

}  // namespace
#endif

StorageManager::StorageManager() = default;
StorageManager::~StorageManager() = default;

void StorageManager::InitializeSessionManagerProxy(dbus::Bus* const bus) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(session_manager_proxy_, nullptr)
      << "session_manager_proxy is already initialized!";
  DCHECK_NE(bus, nullptr);
  session_manager_proxy_ = std::make_unique<SessionManagerProxy>(
      std::make_unique<org::chromium::SessionManagerInterfaceProxy>(bus));

  session_manager_proxy_->AddObserver(this);
  // If session already started, connect to database.
  if (session_manager_proxy_->RetrieveSessionState() == kSessionStartedState) {
    ConnectToDatabaseIfNecessary();
  }
}

bool StorageManager::OnExampleReceived(const std::string& client_name,
                                       const std::string& serialized_example) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (example_database_ == nullptr || !example_database_->IsOpen()) {
    VLOG(1) << "No database connection";
    return false;
  }

  ExampleRecord example_record;
  example_record.serialized_example = serialized_example;
  example_record.timestamp = base::Time::Now();

  return example_database_->InsertExample(client_name, example_record);
}

std::optional<ExampleDatabase::Iterator> StorageManager::GetExampleIterator(
    const std::string& client_name) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (example_database_ == nullptr || !example_database_->IsOpen()) {
    VLOG(1) << "No database connection";
    return std::nullopt;
  }

  if (example_database_->ExampleCount(client_name) < kMinExampleCount) {
    DVLOG(1) << "Client '" << client_name << " "
             << "doesn't meet the minimum example count requirement";
    return std::nullopt;
  }

  return example_database_->GetIterator(client_name);
}

void StorageManager::OnSessionStarted() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectToDatabaseIfNecessary();
}

void StorageManager::OnSessionStopped() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  example_database_.reset();
}

void StorageManager::ConnectToDatabaseIfNecessary() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string new_sanitized_username =
      session_manager_proxy_->GetSanitizedUsername();
  if (new_sanitized_username.empty()) {
    VLOG(1) << "Sanitized_username is empty, disconnect the database";
    example_database_.reset();
    return;
  }

  if (example_database_ != nullptr && example_database_->IsOpen() &&
      new_sanitized_username == sanitized_username_) {
    VLOG(1) << "Database for user " << sanitized_username_
            << " is already connected, nothing changed";
    return;
  }

  sanitized_username_ = new_sanitized_username;
  const auto db_path = GetDatabasePath(sanitized_username_);
  example_database_ = std::make_unique<ExampleDatabase>(db_path);

  if (!example_database_->Init(GetClientNames())) {
    LOG(ERROR) << "Failed to connect to database for user "
               << sanitized_username_;
    example_database_.reset();
  } else if (!example_database_->CheckIntegrity()) {
    LOG(ERROR) << "Failed to verify the database integrity for user "
               << sanitized_username_ << ", delete the existing db file";
    if (!base::DeleteFile(db_path)) {
      LOG(ERROR) << "Failed to delete corrupted db file " << db_path.value();
    }
    example_database_.reset();
  } else {
#if USE_LOCAL_FEDERATED_SERVER
    DVLOG(1) << "Successfully connect to database, inserts examples for test.";
    std::vector<std::string> queries = {"hey", "hey", "hey", "wow", "wow",
                                        "yay", "yay", "yay", "yay", "aha"};
    std::for_each(queries.begin(), queries.end(), [this](auto& query) {
      OnExampleReceived("analytics_test_population",
                        ConvertToTensorFlowExampleProto(CreateExamplePtr(query))
                            .SerializeAsString());
    });
#endif
  }
}

StorageManager* StorageManager::GetInstance() {
  static base::NoDestructor<StorageManager> storage_manager;
  return storage_manager.get();
}
}  // namespace federated
