// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/fuse_path_inodes.h"

#include <string_view>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/contains.h>
#include <base/logging.h>
#include <base/strings/string_split.h>

namespace {

using Node = fusebox::Node;

Node* CreateNode(ino_t parent, const std::string& child, ino_t ino) {
  Node* node = new Node();
  CHECK(node);
  node->device = 0;
  node->parent = parent;
  node->ino = ino;
  node->name = child;
  return node;
}

// Returns "/" prepended to |name|, which matches the form of the Node.name
// field. For example, a "foo.bar" input produces a "/foo.bar" output.
//
// It returns an empty string on failure, which occurs if path already contains
// a "/" or if path is one of: NULL, "", "." or "..".
std::string GetChildNodeName(const char* name) {
  std::string_view entry(name ? name : "");

  // Verify entry name is POSIX conformant: return "" if not.
  if (entry == "." || entry == "..") {
    return {};  // path traversals not allowed
  }
  if (entry.find('/') != std::string_view::npos) {
    return {};  // path components not allowed
  }
  if (entry.empty()) {
    return {};  // trivial cases "" or nullptr
  }

  return std::string("/").append(entry.data(), entry.size());
}

inline Node* NodeError(int error) {
  errno = error;
  return nullptr;
}

}  // namespace

namespace fusebox {

InodeTable::InodeTable() : stat_cache_(1024) {
  root_node_ = InsertNode(CreateNode(0, "/", FUSE_ROOT_ID));
}

static_assert(sizeof(fuse_ino_t) <= sizeof(ino_t),
              "fuse_ino_t size should not exceed the system ino_t size");

ino_t InodeTable::CreateIno() {
  fuse_ino_t ino = ino_++;
  CHECK(ino) << "inodes wrapped";
  return ino;
}

Node* InodeTable::Create(ino_t parent, const char* name, ino_t ino) {
  std::string child = GetChildNodeName(name);
  if (child.empty() || !parent) {
    return NodeError(EINVAL);
  }

  auto parent_it = node_map_.find(parent);
  if (parent_it == node_map_.end()) {
    return NodeError(EINVAL);
  }

  auto p = parent_map_.find(std::to_string(parent).append(child));
  if (p != parent_map_.end()) {
    return NodeError(EEXIST);
  }

  Node* node = InsertNode(CreateNode(parent, child, ino ? ino : CreateIno()));
  node->device = parent_it->second->device;
  return node;
}

Node* InodeTable::Lookup(ino_t ino) {
  auto n = node_map_.find(ino);
  if (n == node_map_.end()) {
    return NodeError(ENOENT);
  }

  return n->second.get();
}

Node* InodeTable::Lookup(ino_t parent, const char* name) {
  std::string child = GetChildNodeName(name);
  if (child.empty()) {
    return NodeError(EINVAL);
  }

  auto p = parent_map_.find(std::to_string(parent).append(child));
  if (p == parent_map_.end()) {
    return NodeError(ENOENT);
  }

  return p->second;
}

Node* InodeTable::Ensure(ino_t parent, const char* name, ino_t ino) {
  std::string child = GetChildNodeName(name);
  if (child.empty() || !parent) {
    return NodeError(EINVAL);
  }

  auto parent_it = node_map_.find(parent);
  if (parent_it == node_map_.end()) {
    return NodeError(EINVAL);
  }

  auto p = parent_map_.find(std::to_string(parent).append(child));
  if (p != parent_map_.end()) {
    return p->second;
  }

  Node* node = InsertNode(CreateNode(parent, child, ino ? ino : CreateIno()));
  node->device = parent_it->second->device;
  return node;
}

Node* InodeTable::Move(Node* node, ino_t parent, const char* name) {
  CHECK_NE(node, root_node_);

  auto parent_it = node_map_.find(parent);
  if (parent_it == node_map_.end()) {
    return NodeError(EINVAL);
  }

  std::string child = GetChildNodeName(name);
  if (child.empty() || !node || node->ino == parent) {
    return NodeError(EINVAL);
  }

  if (parent_it->second->device != node->device) {
    return NodeError(ENOTSUP);  // cross-device move
  }

  if ((node->parent == parent) && (node->name == child)) {
    return node;
  }

  auto p = parent_map_.find(std::to_string(parent).append(child));
  if (p != parent_map_.end()) {
    RemoveAndDeleteNodeAndDescendents(p->second);
  }

  RemoveNode(node);
  node->parent = parent;
  node->name = child;
  return InsertNode(node);
}

bool InodeTable::Forget(ino_t ino) {
  if (!ino || ino == root_node_->ino) {
    return false;  // Ignore root node.
  }

  Node* node = Lookup(ino);
  if (!node) {
    return false;
  }

  delete RemoveNode(node);
  ForgetStat(ino);
  return true;
}

std::string InodeTable::GetName(ino_t ino) {
  if (Node* node = Lookup(ino)) {
    return node->name;
  }
  return {};
}

std::string InodeTable::GetPath(Node* node) {
  DCHECK(node);

  std::deque<std::string> names;
  while (node && node->parent) {
    names.push_front(node->name);
    node = Lookup(node->parent);
  }

  std::string path;
  for (const auto& name : names) {
    path.append(name);
  }
  if (path.empty()) {
    path.push_back('/');
  }

  return path;
}

void InodeTable::SetStat(ino_t ino, struct stat stat, double timeout) {
  DCHECK(ino);
  stat.st_ino = ino;

  struct Stat item;
  item.time = timeout ? std::time(nullptr) + time_t(timeout) : time_t(0);
  item.stat = stat;

  stat_cache_.Put(stat.st_ino, item);
}

bool InodeTable::GetStat(ino_t ino, struct stat* stat) {
  DCHECK(stat);

  auto it = stat_cache_.Get(ino);
  if (it == stat_cache_.end()) {
    return false;
  }

  const auto& item = it->second;
  if (item.time && item.time < std::time(nullptr)) {
    stat_cache_.Erase(it);  // stat time out
    return false;
  }

  *stat = item.stat;
  return true;
}

void InodeTable::ForgetStat(ino_t ino) {
  auto it = stat_cache_.Peek(ino);
  if (it != stat_cache_.end()) {
    stat_cache_.Erase(it);
  }
}

Node* InodeTable::InsertNode(Node* node) {
  DCHECK(node);
  DCHECK(node->ino);

  CHECK_NE(node->parent, node->ino);
  parent_map_[std::to_string(node->parent).append(node->name)] = node;
  CHECK(!base::Contains(node_map_, node->ino));
  node_map_[node->ino].reset(node);

  return node;
}

Node* InodeTable::RemoveNode(Node* node) {
  DCHECK(node);

  parent_map_.erase(std::to_string(node->parent).append(node->name));
  auto n = node_map_.find(node->ino);
  CHECK(n != node_map_.end());
  n->second.release();
  node_map_.erase(n);

  return node;
}

void InodeTable::RemoveAndDeleteNodeAndDescendents(Node* node) {
  CHECK_NE(node, root_node_);

  // TODO(nigeltao): this could be more algorithmically efficient if we
  // redesigned our data structures so that a Node pointed to its children
  // instead of to its parent.

  std::vector<Node*> condemned_nodes;
  for (const auto& it : node_map_) {
    Node* n = it.second.get();
    if ((n == node) || HasDescendent(node, n)) {
      condemned_nodes.push_back(n);
    }
  }

  for (Node* n : condemned_nodes) {
    ForgetStat(n->ino);
    delete RemoveNode(n);
  }
}

bool InodeTable::HasDescendent(Node* ancestor, Node* n) {
  while (n != root_node_) {
    const auto& iter = node_map_.find(n->parent);
    if (iter == node_map_.end()) {
      break;
    }
    n = iter->second.get();
    if (n == ancestor) {
      return true;
    }
  }
  return false;
}

Device InodeTable::MakeFromName(const std::string& name) const {
  Device device;

  std::vector<std::string> parts =
      base::SplitString(name, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  if (parts.size() >= 1) {
    device.name = parts.at(0);
  }
  if (parts.size() >= 2) {
    device.path = parts.at(1);
  } else {
    device.path = device.name;
  }
  device.mode = "rw";
  if (parts.size() >= 3) {
    device.mode = parts.at(2);
  }

  return device;
}

dev_t InodeTable::CreateDev() {
  dev_t dev = ++dev_;
  CHECK(dev) << "devices wrapped";
  return dev;
}

Node* InodeTable::AttachDevice(ino_t parent, struct Device& device, ino_t ino) {
  Node* node = nullptr;

  if (parent == root_node_->ino) {
    node = Create(root_node_->ino, device.name.c_str(), ino);
  } else if (!parent) {
    node = root_node_;
  } else {  // Device node must attach to the root node.
    return NodeError(EINVAL);
  }

  if (!node) {
    DCHECK_NE(0, errno);
    return nullptr;
  }

  device.device = 0;
  if (node != root_node_) {
    device.device = node->device = CreateDev();
  }
  device.ino = node->ino;

  device_map_[device.device] = device;
  return node;
}

bool InodeTable::DetachDevice(ino_t ino) {
  dev_t device = 0;
  if (Node* node = Lookup(ino)) {
    device = node->device;
  }

  auto it = device_map_.find(device);
  if (!device || it == device_map_.end()) {
    return errno = EINVAL, false;
  }

  for (Node* node : GetDeviceNodes(device)) {
    Forget(node->ino);
  }

  device_map_.erase(it);
  return true;
}

std::deque<Node*> InodeTable::GetDeviceNodes(dev_t device) const {
  std::deque<Node*> nodes;

  for (const auto& it : node_map_) {
    Node* node = it.second.get();
    if (device == node->device) {
      nodes.push_front(node);
    }
  }

  return nodes;
}

std::string InodeTable::GetDevicePath(Node* node) {
  Device device = GetDevice(node);

  // Remove the device.name from the path.
  std::string path = GetPath(node);
  if (device.device && !device.name.empty()) {
    path = path.substr(1 + device.name.size());
  }

  // Add the device.path prefix if needed.
  if (path != "/") {
    return path.insert(0, device.path);
  }
  if (!device.path.empty()) {
    return device.path;
  }

  return path;
}

Device InodeTable::GetDevice(Node* node) const {
  DCHECK(node);

  auto it = device_map_.find(node->device);
  if (it != device_map_.end()) {
    return it->second;
  }

  return {};
}

}  // namespace fusebox
