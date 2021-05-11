// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/storage_manager_impl.h"

#include <cstddef>
#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/strings/stringprintf.h>

#include "federated/federated_metadata.h"
#include "federated/session_manager_proxy.h"
#include "federated/utils.h"

namespace federated {

void StorageManagerImpl::InitializeSessionManagerProxy(dbus::Bus* bus) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!session_manager_proxy_)
      << "session_manager_proxy is already initialized!";
  DCHECK(bus);
  session_manager_proxy_ = std::make_unique<SessionManagerProxy>(
      std::make_unique<org::chromium::SessionManagerInterfaceProxy>(bus));

  session_manager_proxy_->AddObserver(this);
  // If session already started, connect to database.
  if (session_manager_proxy_->RetrieveSessionState() == kSessionStartedState) {
    ConnectToDatabaseIfNecessary();
  }
}

bool StorageManagerImpl::OnExampleReceived(
    const std::string& client_name, const std::string& serialized_example) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!example_database_ || !example_database_->IsOpen()) {
    VLOG(1) << "No database connection.";
    return false;
  }

  ExampleRecord example_record;
  example_record.client_name = client_name;
  example_record.serialized_example = serialized_example;
  example_record.timestamp = base::Time::Now();

  return example_database_->InsertExample(example_record);
}

bool StorageManagerImpl::PrepareStreamingForClient(
    const std::string& client_name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!example_database_ || !example_database_->IsOpen()) {
    LOG(ERROR) << "No database connection.";
    return false;
  }
  last_seen_example_id_ = 0;
  streaming_client_name_ = client_name;
  return example_database_->PrepareStreamingForClient(
      client_name, kMaxStreamingExampleCount);
}

bool StorageManagerImpl::GetNextExample(std::string* example,
                                        bool* end_of_iterator) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (!example_database_ || !example_database_->IsOpen()) {
    VLOG(1) << "No database connection.";
    return false;
  }
  *end_of_iterator = false;
  auto maybe_example_record = example_database_->GetNextStreamedRecord();
  if (maybe_example_record == base::nullopt) {
    *end_of_iterator = true;
  } else {
    last_seen_example_id_ = maybe_example_record.value().id;
    *example = maybe_example_record.value().serialized_example;
  }

  return true;
}

bool StorageManagerImpl::CloseStreaming(bool clean_examples) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!example_database_ || !example_database_->IsOpen()) {
    VLOG(1) << "No database connection!";
    return true;
  }

  example_database_->CloseStreaming();

  return !clean_examples ||
         example_database_->DeleteExamplesWithSmallerIdForClient(
             streaming_client_name_, last_seen_example_id_);
}

void StorageManagerImpl::OnSessionStarted() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectToDatabaseIfNecessary();
}

void StorageManagerImpl::OnSessionStopped() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  example_database_.reset();
}

void StorageManagerImpl::ConnectToDatabaseIfNecessary() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::string new_sanitized_username =
      session_manager_proxy_->GetSanitizedUsername();
  if (new_sanitized_username.empty()) {
    VLOG(1) << "Sanitized_username is empty, disconnect the database.";
    example_database_.reset();
    return;
  }

  if (example_database_ && example_database_->IsOpen() &&
      new_sanitized_username == sanitized_username_) {
    VLOG(1) << "Database for user " << sanitized_username_
            << " is already connected, nothing changed.";
    return;
  }

  sanitized_username_ = new_sanitized_username;
  auto db_path = GetDatabasePath(sanitized_username_);
  example_database_.reset(new ExampleDatabase(db_path, GetClientNames()));

  if (!example_database_->Init()) {
    LOG(ERROR) << "Failed to connect to database for user "
               << sanitized_username_;
    example_database_.reset();
  } else if (!example_database_->CheckIntegrity()) {
    LOG(ERROR) << "Failed to verify the database integrity for user "
               << sanitized_username_ << ", delete the existing db file.";
    if (!base::DeleteFile(db_path)) {
      LOG(ERROR) << "Failed to delete corrupted db file " << db_path.value();
    }
    example_database_.reset();
  }
}

StorageManager* StorageManager::GetInstance() {
  static base::NoDestructor<StorageManagerImpl> storage_manager;
  return storage_manager.get();
}
}  // namespace federated
