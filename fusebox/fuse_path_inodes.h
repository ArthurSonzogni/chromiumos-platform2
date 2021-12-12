// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUSEBOX_FUSE_PATH_INODES_H_
#define FUSEBOX_FUSE_PATH_INODES_H_

#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <unordered_map>

#include <base/containers/mru_cache.h>

namespace fusebox {

struct Node {
  dev_t device;       // Device
  ino_t parent;       // Parent ino
  ino_t ino;          // Inode ino
  std::string name;   // Entry name
  uint64_t refcount;  // Ref count
};

class InodeTable {
 public:
  InodeTable();
  InodeTable(const InodeTable&) = delete;
  InodeTable& operator=(const InodeTable&) = delete;
  ~InodeTable() = default;

  // Creates a new child node |name| of |parent| ino, and inserts it into the
  // node table. Returns the new node or null on failure.
  Node* Create(ino_t parent, const char* name);

  // Lookup the node table by |ino| and return its node. The node refcount is
  // increased by |ref|. Returns null on failure.
  Node* Lookup(ino_t ino, uint64_t ref = 0);

  // Lookup the table by |parent| and child node |name|, and return the node.
  // The node refcount is increased by |ref|. Returns null on failure.
  Node* Lookup(ino_t parent, const char* name, uint64_t ref = 0);

  // Same as Lookup(parent, name, ref) but creates if the |parent| child node
  // |name| if it does not exist. Returns null on failure.
  Node* Ensure(ino_t parent, const char* name, uint64_t ref = 0);

  // Move a table |node| to be the child node |name| of |parent| ino. Returns
  // the node or null on failure.
  Node* Move(Node* node, ino_t parent, const char* name);

  // Forget |nlookup| refcounts to an |ino|. Deletes the node if its refcount
  // becomes 0. Returns true if the node was deleted.
  bool Forget(ino_t ino, uint64_t nlookup = 1);

  // Returns the node |name| field, or empty string if the node was not found
  // in the node table.
  std::string GetName(ino_t ino);

  // Returns the node full path name. Note: |node| must be in the node table.
  std::string GetPath(Node* node);

  // Cache a stat for the node.
  void SetStat(ino_t ino, struct stat stat, double timeout = 0);

  // Get the cached stat for the node. Returns true on success.
  bool GetStat(ino_t ino, struct stat* stat);

  // Forget the cached stat for the node.
  void ForgetStat(ino_t ino);

 private:
  // Returns a new ino_t number.
  ino_t CreateIno();

  // Inserts |node| into the node table.
  Node* InsertNode(Node* node);

  // Removes |node| from the node table.
  Node* RemoveNode(Node* node);

  // Node |stat_cache_| entry type.
  struct Stat {
    struct stat stat;
    time_t time;
  };

 private:
  // ino_t allocator.
  ino_t ino_;

  // Map ino to node.
  std::unordered_map<ino_t, std::unique_ptr<Node>> node_map_;

  // Map parent-ino/child-name to node.
  std::unordered_map<std::string, Node*> parent_map_;

  // Node stat cache.
  base::HashingMRUCache<ino_t, struct Stat> stat_cache_;

  // Root node.
  Node* root_node_ = nullptr;
};

}  // namespace fusebox

#endif  // FUSEBOX_FUSE_PATH_INODES_H_
