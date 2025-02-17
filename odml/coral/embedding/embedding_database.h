// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_EMBEDDING_EMBEDDING_DATABASE_H_
#define ODML_CORAL_EMBEDDING_EMBEDDING_DATABASE_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <metrics/cumulative_metrics.h>

#include "odml/coral/common.h"
#include "odml/coral/metrics.h"

namespace coral {

class EmbeddingDatabaseInterface;

class EmbeddingDatabaseFactory {
 public:
  virtual ~EmbeddingDatabaseFactory() = default;
  // Creates a EmbeddingDatabaseInterface instance with the given parameters.
  virtual std::unique_ptr<EmbeddingDatabaseInterface> Create(
      raw_ref<CoralMetrics> metrics,
      const base::FilePath& file_path,
      base::TimeDelta ttl) const;
};

// Interface to a file-backed embedding database.
class EmbeddingDatabaseInterface {
 public:
  virtual ~EmbeddingDatabaseInterface() = default;

  // Writes (key, embedding_entry) to the in-memory mapping. No sync yet.
  virtual void Put(std::string key, EmbeddingEntry embedding) = 0;

  // Reads embedding_entry from the in-memory mapping if the key exists in
  // database. The returned entry will have empty `embedding` and empty
  // `safety_verdict` if the key doesn't exist.
  virtual EmbeddingEntry Get(const std::string& key) = 0;

  // Syncs the in-memory mapping to the file. Stale records are removed both in
  // memory and file.
  virtual bool Sync() = 0;
};

// A file-backed embedding database.
class EmbeddingDatabase : public EmbeddingDatabaseInterface {
 public:
  static std::unique_ptr<EmbeddingDatabase> Create(
      raw_ref<CoralMetrics> metrics,
      const base::FilePath& file_path,
      base::TimeDelta ttl);

  ~EmbeddingDatabase() override;
  EmbeddingDatabase(const EmbeddingDatabase&) = delete;
  EmbeddingDatabase& operator=(const EmbeddingDatabase&) = delete;

  // EmbeddingDatabaseInterface overrides.
  void Put(std::string key, EmbeddingEntry embedding) override;
  EmbeddingEntry Get(const std::string& key) override;
  bool Sync() override;

 private:
  struct EmbeddingEntryWithTimestamp {
    EmbeddingEntry entry;
    base::Time updated_time_ms;
  };

  // Backed by file |file_path|.
  // Records older than |ttl| are removed when (and only when) loading and
  // syncing. |ttl| with value 0 means no TTL.
  EmbeddingDatabase(raw_ref<CoralMetrics> metrics,
                    const base::FilePath& file_path,
                    base::TimeDelta ttl);

  // Returns true if a record is stale.
  bool IsRecordExpired(base::Time now,
                       const EmbeddingEntryWithTimestamp& record) const;

  // Loads the database from |file_path_|.
  bool LoadFromFile();

  // If the embeddings map contains too many entries, prune some of them
  // according to last updated time.
  void MaybePruneEntries();

  void ReportDailyMetrics(
      chromeos_metrics::CumulativeMetrics* const cumulative_metrics);

  const raw_ref<CoralMetrics> metrics_;

  bool dirty_;
  const base::FilePath file_path_;
  const base::TimeDelta ttl_;
  std::unordered_map<std::string, EmbeddingEntryWithTimestamp> embeddings_map_;
  // Each entry (updated_time, key) corresponds to an entry in
  // `embeddings_map_`. The 2 containers should be updated together and always
  // stay consistent. This is sorted by updated_time so we can efficiently find
  // the oldest entries to prune when we need to.
  // The default behavior of pair comparison is lexicographical, so it will be
  // first sorted by updated_time, then by the key string.
  std::set<std::pair<base::Time, std::string>> updated_time_of_keys_;

  std::unique_ptr<chromeos_metrics::CumulativeMetrics> daily_metrics_;

  base::WeakPtrFactory<EmbeddingDatabase> weak_factory_{this};
};

}  // namespace coral

#endif  // ODML_CORAL_EMBEDDING_EMBEDDING_DATABASE_H_
