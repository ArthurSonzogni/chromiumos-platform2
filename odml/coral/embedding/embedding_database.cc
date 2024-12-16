// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/embedding_database.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>

#include "coral/proto_bindings/embedding.pb.h"
#include "odml/coral/common.h"

namespace coral {

namespace {

// Roughly 3KB per entry, leading to max 3MB for the in-memory/on-device
// database.
constexpr size_t kMaxEntries = 1000;
// Prune around 10% of entries when it exceeds kMaxEntries, so we don't have to
// trigger prune operations that often when the map is nearly full.
constexpr size_t kEntriesToPrune = 100;

}  // namespace

// class EmbeddingDatabaseFactory.
std::unique_ptr<EmbeddingDatabaseInterface> EmbeddingDatabaseFactory::Create(
    raw_ref<CoralMetrics> metrics,
    const base::FilePath& file_path,
    const base::TimeDelta ttl) const {
  return EmbeddingDatabase::Create(metrics, file_path, ttl);
}

// class EmbeddingDatabase.
std::unique_ptr<EmbeddingDatabase> EmbeddingDatabase::Create(
    raw_ref<CoralMetrics> metrics,
    const base::FilePath& file_path,
    const base::TimeDelta ttl) {
  // EmbeddingDatabase() is private, so can not use make_unique.
  auto instance =
      base::WrapUnique(new EmbeddingDatabase(metrics, file_path, ttl));

  if (base::PathExists(file_path)) {
    // Do not return nullptr, since we can try overwriting the file later when
    // Sync().
    if (!instance->LoadFromFile()) {
      LOG(ERROR) << "Failed to load from embedding database.";
    }
  } else if (!base::PathExists(file_path.DirName())) {
    base::File::Error error;
    // If we can't create the parent directory, we can't write to |file_path|
    // later in Sync(). So return nullptr to indicate an error.
    if (!base::CreateDirectoryAndGetError(file_path.DirName(), &error)) {
      LOG(ERROR) << "Unable to create embedding database directory: "
                 << base::File::ErrorToString(error);
      return nullptr;
    } else {
      LOG(INFO) << "Created embedding database directory.";
    }
  }
  return instance;
}

EmbeddingDatabase::EmbeddingDatabase(raw_ref<CoralMetrics> metrics,
                                     const base::FilePath& file_path,
                                     const base::TimeDelta ttl)
    : metrics_(metrics), dirty_(false), file_path_(file_path), ttl_(ttl) {}

EmbeddingDatabase::~EmbeddingDatabase() {
  // Ignore errors.
  Sync();
}

void EmbeddingDatabase::Put(std::string key, EmbeddingEntry entry) {
  auto now = base::Time::Now();
  auto it = embeddings_map_.find(key);
  if (it == embeddings_map_.end()) {
    updated_time_of_keys_.insert({now, key});
    embeddings_map_[std::move(key)] = EmbeddingEntryWithTimestamp{
        .entry = std::move(entry),
        .updated_time_ms = now,
    };
    MaybePruneEntries();
  } else {
    updated_time_of_keys_.erase({it->second.updated_time_ms, key});
    updated_time_of_keys_.insert({now, key});
    it->second = EmbeddingEntryWithTimestamp{
        .entry = std::move(entry),
        .updated_time_ms = now,
    };
  }

  dirty_ = true;
}

EmbeddingEntry EmbeddingDatabase::Get(const std::string& key) {
  auto now = base::Time::Now();
  auto it = embeddings_map_.find(key);
  if (it == embeddings_map_.end()) {
    return EmbeddingEntry();
  }
  updated_time_of_keys_.erase({it->second.updated_time_ms, key});
  updated_time_of_keys_.insert({now, key});
  it->second.updated_time_ms = now;
  dirty_ = true;
  return it->second.entry;
}

bool EmbeddingDatabase::Sync() {
  // Remove stale records.
  base::Time now = base::Time::Now();

  int num_removed = 0;
  std::erase_if(embeddings_map_, [this, &now, &num_removed](const auto& entry) {
    if (IsRecordExpired(now, entry.second)) {
      ++num_removed;
      updated_time_of_keys_.erase({entry.second.updated_time_ms, entry.first});
      return true;
    }
    return false;
  });

  if (!dirty_ && !num_removed) {
    return true;
  }

  LOG(INFO) << "Sync embedding database with now: " << now
            << ", num_removed: " << num_removed
            << ", size: " << embeddings_map_.size();

  EmbeddingRecords records;
  for (const auto& [key, entry] : embeddings_map_) {
    EmbeddingRecord record;
    record.mutable_values()->Assign(entry.entry.embedding.begin(),
                                    entry.entry.embedding.end());
    record.set_updated_time_ms(
        entry.updated_time_ms.InMillisecondsSinceUnixEpoch());
    if (entry.entry.safety_verdict.has_value()) {
      record.set_safety_verdict(entry.entry.safety_verdict.value());
    }
    records.mutable_records()->insert({key, std::move(record)});
  }

  std::string buf;
  if (!records.SerializeToString(&buf)) {
    LOG(ERROR) << "Failed to seralize the to embeddings.";
    return false;
  }
  if (!base::WriteFile(file_path_, buf)) {
    LOG(ERROR) << "Failed to write embeddings to database.";
    return false;
  }
  return true;
}

bool EmbeddingDatabase::IsRecordExpired(
    const base::Time now, const EmbeddingEntryWithTimestamp& record) const {
  // 0 means no ttl.
  return !ttl_.is_zero() && now - record.updated_time_ms > ttl_;
}

bool EmbeddingDatabase::LoadFromFile() {
  std::string buf;
  if (!base::ReadFileToString(file_path_, &buf)) {
    LOG(WARNING) << "Failed to read the embedding database.";
    return false;
  }

  EmbeddingRecords records;
  if (!records.ParseFromString(buf)) {
    LOG(ERROR)
        << "Failed to parse the embedding database. Try removing the file.";
    if (!brillo::DeleteFile(file_path_)) {
      LOG(ERROR) << "Failed to delete the corrupted embedding database file.";
    }
    return false;
  }

  base::Time now = base::Time::Now();
  for (const auto& [key, record] : records.records()) {
    auto updated_time_ms =
        base::Time::FromMillisecondsSinceUnixEpoch(record.updated_time_ms());

    std::optional<bool> violation;
    if (record.has_safety_verdict()) {
      violation = record.safety_verdict();
    }
    embeddings_map_[key] = EmbeddingEntryWithTimestamp{
        .entry = EmbeddingEntry{.embedding = Embedding(record.values().begin(),
                                                       record.values().end()),
                                .safety_verdict = violation},
        .updated_time_ms = updated_time_ms};
    updated_time_of_keys_.insert({updated_time_ms, key});
  }
  MaybePruneEntries();
  LOG(INFO) << "Load from embedding database with now: " << now
            << ", size: " << embeddings_map_.size();
  metrics_->SendEmbeddingDatabaseEntriesCount(embeddings_map_.size());
  return true;
}

void EmbeddingDatabase::MaybePruneEntries() {
  if (embeddings_map_.size() <= kMaxEntries) {
    return;
  }
  // This shouldn't happen, but if it does, we fail gracefully by not doing the
  // pruning.
  if (embeddings_map_.size() != updated_time_of_keys_.size()) {
    LOG(WARNING)
        << "embeddings_map_ isn't consistent with updated_time_of_keys_";
    return;
  }
  static_assert(kEntriesToPrune < kMaxEntries);
  for (int i = 0; i < kEntriesToPrune; i++) {
    embeddings_map_.erase(updated_time_of_keys_.begin()->second);
    updated_time_of_keys_.erase(updated_time_of_keys_.begin());
  }
}

}  // namespace coral
