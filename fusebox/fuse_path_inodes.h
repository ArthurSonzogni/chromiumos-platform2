// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUSEBOX_FUSE_PATH_INODES_H_
#define FUSEBOX_FUSE_PATH_INODES_H_

#include <fuse_lowlevel.h>

#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

#include <base/containers/lru_cache.h>

namespace fusebox {

enum {
  // Skip FUSE_ROOT_ID = 1.
  INO_BUILT_IN = 2,
  INO_BUILT_IN_FUSE_STATUS = 3,
  INO_BUILT_IN_PING = 4,
  // This final number is arbitrary, other than being larger than the above.
  FIRST_UNRESERVED_INO = 100,
};

struct Node {
  dev_t device;      // Device number
  ino_t parent;      // Parent ino
  ino_t ino;         // Inode ino
  std::string name;  // Entry name with "/" prefix
};

struct Device {
  std::string name;  // Device name
  std::string path;  // Path prefix
  std::string mode;  // Device mode
  dev_t device = 0;  // Device number
  ino_t ino = 0;     // Inode ino
};

class InodeTable {
 public:
  InodeTable();
  InodeTable(const InodeTable&) = delete;
  InodeTable& operator=(const InodeTable&) = delete;
  ~InodeTable() = default;

  // Creates a new child node |name| of |parent| ino, and inserts it into the
  // node table. Returns the new node or null on failure.
  //
  // The new node will have the given |ino|, or an automatically incremented
  // global counter if |ino| is zero.
  Node* Create(ino_t parent, const char* name, ino_t ino = 0);

  // Lookup the node table by |ino| and return its node. Returns null on
  // failure.
  Node* Lookup(ino_t ino);

  // Lookup the table by |parent| and child node |name|, and return the node.
  // Returns null on failure.
  Node* Lookup(ino_t parent, const char* name);

  // Lookup(parent, name) that creates the |parent| child node |name| if it
  // does not exist. Returns null on failure.
  //
  // The new node will have the given |ino|, or an automatically incremented
  // global counter if |ino| is zero.
  Node* Ensure(ino_t parent, const char* name, ino_t ino = 0);

  // Move a table |node| to be the child node |name| of |parent| ino. This
  // overwrites any existing node at that parent/name combination, even if it
  // was a directory, consistent with FUSE or "man 2 renameat2" semantics where
  // the RENAME_NOREPLACE flag is off by default.
  //
  // Returns |node| on success. It can fail if the |parent| cannot be found, if
  // attempting to reparent |node| to itself (causing a direct loop) or if the
  // |name| is invalid.
  //
  // Indirect loops, where a node is its own ancestor (but not its own parent),
  // are currently not prohibited, but that may change in the future.
  //
  // On failure, nullptr is returned and, as a side effect, errno is set.
  Node* Move(Node* node, ino_t parent, const char* name);

  // Deletes the node for |ino|. Returns true if the node was deleted.
  bool Forget(ino_t ino);

  // Returns the node |name| field, or empty string if the node was not found
  // in the node table.
  std::string GetName(ino_t ino);

  // Returns the node full path name: |node| must be in the node table.
  std::string GetPath(Node* node);

  // Returns a Device created from |name|, which has fields for Device struct
  // {name, path, mode, ...} members.
  Device MakeFromName(const std::string& name) const;

  // Attach |device| to the node table as a child of the |parent| node, named
  // |device.name|. Returns the new node or null on failure.
  //
  // The new node will have the given |ino|, or an automatically incremented
  // global counter if |ino| is zero.
  Node* AttachDevice(ino_t parent, Device& device, ino_t ino = 0);

  // Detach and remove device |ino| and its child nodes from the node table.
  // Returns true on success.
  bool DetachDevice(ino_t ino);

  // Returns the full device path name: |node| must be in the node table.
  std::string GetDevicePath(Node* node);

  // Returns the node device: |node| must be in the node table.
  Device GetDevice(Node* node) const;

  // Cache a stat for the node.
  void SetStat(ino_t ino, struct stat stat, double timeout = 0);

  // Get the cached stat for the node. Returns true on success.
  bool GetStat(ino_t ino, struct stat* stat);

  // Forget the cached stat for the node.
  void ForgetStat(ino_t ino);

 private:
  // Creates a new ino number.
  ino_t CreateIno();

  // Inserts |node| into the node table.
  Node* InsertNode(Node* node);

  // Removes |node| from the node table.
  Node* RemoveNode(Node* node);

  // Removes and deletes n from the node table for every Node n that is either
  // |node| itself or a descendent of |node|.
  void RemoveAndDeleteNodeAndDescendents(Node* node);

  // Returns whether |n| is a descendent of |ancestor|. It returns false if the
  // two nodes are equal.
  bool HasDescendent(Node* ancestor, Node* n);

  // Creates a new device number.
  dev_t CreateDev();

  // Returns all |device| number nodes.
  std::deque<Node*> GetDeviceNodes(dev_t device) const;

  // Node |stat_cache_| entry type.
  struct Stat {
    struct stat stat;
    time_t time;
  };

 private:
  // ino number creator.
  fuse_ino_t ino_ = FIRST_UNRESERVED_INO;

  // dev_t number creator.
  dev_t dev_ = 0;

  // Map ino to node.
  std::unordered_map<ino_t, std::unique_ptr<Node>> node_map_;

  // Map parent-ino/child-name (e.g., 342/child.txt) to node.
  std::unordered_map<std::string, Node*> parent_map_;

  // Map device number to device.
  std::unordered_map<dev_t, struct Device> device_map_;

  // Node stat cache.
  base::HashingLRUCache<ino_t, struct Stat> stat_cache_;

  // Root node.
  Node* root_node_ = nullptr;
};

}  // namespace fusebox

#endif  // FUSEBOX_FUSE_PATH_INODES_H_
