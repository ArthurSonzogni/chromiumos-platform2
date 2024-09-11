// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_EMBEDDING_EMBEDDING_DATABASE_H_
#define ODML_CORAL_EMBEDDING_EMBEDDING_DATABASE_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/files/file_path.h>
#include <base/time/time.h>

#include "coral/proto_bindings/embedding.pb.h"

namespace coral {

using Embedding = std::vector<float>;

class EmbeddingDatabase;

class EmbeddingDatabaseFactory {
 public:
  // Creates a EmbeddingDatabase instance with the given parameters.
  std::unique_ptr<EmbeddingDatabase> Create(const base::FilePath& file_path,
                                            base::TimeDelta ttl) const;
};

// A file-backed embedding database.
class EmbeddingDatabase {
 public:
  friend class EmbeddingDatabaseFactory;

  ~EmbeddingDatabase();
  EmbeddingDatabase(const EmbeddingDatabase&) = delete;
  EmbeddingDatabase& operator=(const EmbeddingDatabase&) = delete;

  // Writes (key, embedding) to the in-memory mapping. No sync yet.
  void Put(std::string key, Embedding embedding);

  // Reads (key, embedding) from the in-memory mapping.
  // Returns std::nullopt if the key doesn't exist.
  std::optional<Embedding> Get(const std::string& key);

  // Syncs the in-memory mapping to the file. Stale records are removed both in
  // memory and file.
  // TODO(b/361429567): call this periodically.
  bool Sync();

 private:
  // Backed by file |file_path|.
  // Records older than |ttl| are removed when (and only when) loading and
  // syncing. |ttl| with value 0 means no TTL.
  EmbeddingDatabase(const base::FilePath& file_path, base::TimeDelta ttl);

  // Returns true if a record is stale.
  bool IsRecordExpired(base::Time now, const EmbeddingRecord& record) const;

  bool dirty_;
  const base::FilePath file_path_;
  const base::TimeDelta ttl_;
  std::unordered_map<std::string, EmbeddingRecord> embeddings_map_;
};

}  // namespace coral

#endif  // ODML_CORAL_EMBEDDING_EMBEDDING_DATABASE_H_
