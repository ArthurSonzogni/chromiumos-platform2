// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/fuse_path_inodes.h"

#include <deque>

#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/contains.h>
#include <base/logging.h>
#include <base/strings/string_piece.h>

namespace {

std::string GetParentChildName(const char* path) {
  base::StringPiece name(path ? path : ".");

  if (name.empty() || name == "/")
    return path;
  if (name == "." || name == "..")
    return {};
  if (name.find('/') != base::StringPiece::npos)
    return {};

  return std::string("/").append(name.data(), name.size());
}

using Node = fusebox::Node;

inline Node* NodeError(int error) {
  errno = error;
  return nullptr;
}

Node* CreateNode(ino_t parent, const std::string& child, ino_t ino) {
  Node* node = new Node();
  CHECK(node);
  node->device = 0;
  node->parent = parent;
  node->ino = ino;
  node->name = child;
  node->refcount = 1;
  return node;
}

}  // namespace

namespace fusebox {

InodeTable::InodeTable() : ino_(0), stat_cache_(1024) {
  root_node_ = InsertNode(CreateNode(0, "/", CreateIno()));
}

ino_t InodeTable::CreateIno() {
  ino_t ino = ++ino_;
  CHECK(ino) << "inodes wrapped";
  return ino;
}

Node* InodeTable::Create(ino_t parent, const char* name) {
  std::string child = GetParentChildName(name);
  if (child.empty() || !parent)
    return NodeError(EINVAL);

  auto p = parent_map_.find(std::to_string(parent).append(child));
  if (p != parent_map_.end())
    return NodeError(EEXIST);

  return InsertNode(CreateNode(parent, child, CreateIno()));
}

Node* InodeTable::Lookup(ino_t ino, uint64_t ref) {
  auto n = node_map_.find(ino);
  if (n == node_map_.end())
    return NodeError(ENOENT);

  Node* node = n->second.get();
  node->refcount += ref;
  return node;
}

Node* InodeTable::Lookup(ino_t parent, const char* name, uint64_t ref) {
  std::string child = GetParentChildName(name);
  if (child.empty())
    return NodeError(EINVAL);

  auto p = parent_map_.find(std::to_string(parent).append(child));
  if (p == parent_map_.end())
    return NodeError(ENOENT);

  Node* node = p->second;
  node->refcount += ref;
  return node;
}

Node* InodeTable::Ensure(ino_t parent, const char* name, uint64_t ref) {
  std::string child = GetParentChildName(name);
  if (child.empty() || !parent)
    return NodeError(EINVAL);

  auto p = parent_map_.find(std::to_string(parent).append(child));
  if (p != parent_map_.end()) {
    p->second->refcount += ref;
    return p->second;
  }

  Node* node = InsertNode(CreateNode(parent, child, CreateIno()));
  node->refcount += ref;
  return node;
}

Node* InodeTable::Move(Node* node, ino_t parent, const char* name) {
  CHECK_NE(node, root_node_);

  if (node_map_.find(parent) == node_map_.end())
    return NodeError(EINVAL);

  std::string child = GetParentChildName(name);
  if (child.empty() || !node || node->ino == parent)
    return NodeError(EINVAL);

  auto p = parent_map_.find(std::to_string(parent).append(child));
  if (p != parent_map_.end())
    return NodeError(EEXIST);

  RemoveNode(node);
  node->parent = parent;
  node->name = child;
  return InsertNode(node);
}

bool InodeTable::Forget(ino_t ino, uint64_t nlookup) {
  if (!ino || ino == root_node_->ino)
    return false;  // Ignore root node.

  Node* node = Lookup(ino);
  if (!node)
    return true;

  if (nlookup < node->refcount) {
    node->refcount -= nlookup;
    return false;
  }

  delete RemoveNode(node);
  ForgetStat(ino);
  return true;
}

std::string InodeTable::GetName(ino_t ino) {
  if (Node* node = Lookup(ino))
    return node->name;
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
  for (const auto& name : names)
    path.append(name);
  if (path.empty())
    path.push_back('/');

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
  if (it == stat_cache_.end())
    return false;

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
  if (it != stat_cache_.end())
    stat_cache_.Erase(it);
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

}  // namespace fusebox
