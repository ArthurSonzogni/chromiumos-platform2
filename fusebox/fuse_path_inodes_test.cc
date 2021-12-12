// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/fuse_path_inodes.h"

#include <gtest/gtest.h>

namespace fusebox {

TEST(FusePathInodesTest, RootNode) {
  InodeTable inodes;

  // The root node always exists.
  Node* node = inodes.Lookup(1);
  EXPECT_TRUE(node);
  EXPECT_EQ(0, node->device);
  EXPECT_EQ(1, node->ino);
  EXPECT_EQ(1, node->refcount);

  // Root node can be found by parent child lookup.
  Node* root = inodes.Lookup(0, "/");
  EXPECT_EQ(node, root);

  // Root has a name and full path name.
  EXPECT_EQ("/", inodes.GetName(root->ino));
  EXPECT_EQ("/", inodes.GetPath(root));

  // Root node cannot be forgotten.
  EXPECT_FALSE(inodes.Forget(node->ino));

  // Root node cannot be recreated.
  errno = 0;
  EXPECT_FALSE(inodes.Create(0, "/"));
  EXPECT_EQ(EINVAL, errno);

  // Create a child of the root node.
  Node* child = inodes.Create(root->ino, "foo");
  EXPECT_TRUE(child);

  // Root node cannot be overwritten.
  errno = 0;
  EXPECT_FALSE(inodes.Move(child, 0, "/"));
  EXPECT_EQ(EINVAL, errno);

  // Root node cannot be moved.
  EXPECT_DEATH(inodes.Move(root, 2, "bar"), "");
}

TEST(FusePathInodesTest, RootNodeParent) {
  InodeTable inodes;

  // The root node parent ino is ino 0.
  Node* root = inodes.Lookup(1);
  EXPECT_TRUE(root);
  EXPECT_EQ(0, root->parent);

  // Root node parent is not present in the node table.
  errno = 0;
  Node* root_parent = inodes.Lookup(0);
  EXPECT_EQ(errno, ENOENT);
  EXPECT_FALSE(root_parent);

  // Root node parent has no name.
  EXPECT_TRUE(inodes.GetName(0).empty());

  // Root node parent cannot be forgotten.
  EXPECT_FALSE(inodes.Forget(0));

  // It only has one child: the root node.
  errno = 0;
  EXPECT_FALSE(inodes.Create(0, "child"));
  EXPECT_EQ(EINVAL, errno);
  errno = 0;
  EXPECT_FALSE(inodes.Ensure(0, "child"));
  EXPECT_EQ(EINVAL, errno);

  // Create a child of the root node.
  Node* child = inodes.Create(1, "foo");
  EXPECT_TRUE(child);

  // Cannot move a child to the root parent.
  errno = 0;
  EXPECT_FALSE(inodes.Move(child, 0, "foo"));
  EXPECT_EQ(EINVAL, errno);
}

TEST(FusePathInodesTest, LookupNodes) {
  InodeTable inodes;

  errno = 0;
  Node* root_parent = inodes.Lookup(0);
  EXPECT_FALSE(root_parent);
  EXPECT_EQ(ENOENT, errno);

  Node* root = inodes.Lookup(1);
  EXPECT_TRUE(root);
  EXPECT_EQ(0, root->parent);

  errno = 0;
  EXPECT_FALSE(inodes.Lookup(2));
  EXPECT_EQ(ENOENT, errno);
  EXPECT_TRUE(inodes.GetName(2).empty());

  errno = 0;
  EXPECT_FALSE(inodes.Lookup(1, "/foo"));
  EXPECT_EQ(EINVAL, errno);

  errno = 0;
  EXPECT_FALSE(inodes.Lookup(1, "foo"));
  EXPECT_EQ(ENOENT, errno);
}

TEST(FusePathInodesTest, NodeNames) {
  InodeTable inodes;

  errno = 0;
  EXPECT_FALSE(inodes.Create(1, nullptr));
  EXPECT_EQ(EINVAL, errno);

  static const char* kInvalidNames[] = {
      "",    ".",    "..",   "/.",   "/..",   "./",    "..//",
      "//",  "//.",  "//..", "/foo", "//bar", "foo/",  "bar//",
      "/a/", "//b/", "c/.",  "c/..", "d/e",   "f/./g", "/../i",
  };

  for (const char* name : kInvalidNames) {
    errno = 0;
    EXPECT_FALSE(inodes.Create(1, name));
    EXPECT_EQ(EINVAL, errno);
    errno = 0;
    EXPECT_FALSE(inodes.Lookup(1, name));
    EXPECT_EQ(EINVAL, errno);
    errno = 0;
    EXPECT_FALSE(inodes.Ensure(1, name));
    EXPECT_EQ(EINVAL, errno);
  }

  for (const char* valid : {"foo", "bar", "baz"}) {
    errno = 0;
    EXPECT_TRUE(inodes.Create(1, valid));
    EXPECT_EQ(0, errno);
    EXPECT_TRUE(inodes.Lookup(1, valid));
    EXPECT_EQ(0, errno);
    EXPECT_TRUE(inodes.Ensure(1, valid));
    EXPECT_EQ(0, errno);
  }
}

TEST(FusePathInodesTest, ChildNode) {
  InodeTable inodes;

  // Create a child of the root node.
  Node* node = inodes.Create(1, "foo");
  EXPECT_TRUE(node);
  EXPECT_EQ(2, node->ino);
  EXPECT_EQ(1, node->parent);
  EXPECT_EQ(1, node->refcount);

  // Node can be found by ino lookup.
  EXPECT_EQ(node, inodes.Lookup(node->ino));

  // Node can be found by parent child lookup.
  EXPECT_EQ(node, inodes.Lookup(1, "foo"));

  // Node has a name and a full path name.
  EXPECT_EQ("/foo", inodes.GetName(node->ino));
  EXPECT_EQ("/foo", inodes.GetPath(node));

  // Node cannot be recreated.
  errno = 0;
  EXPECT_FALSE(inodes.Create(1, "foo"));
  EXPECT_EQ(EEXIST, errno);
}

TEST(FusePathInodesTest, ChildNodeForget) {
  InodeTable inodes;

  // Create a child of the root node.
  Node* node = inodes.Create(1, "foo");
  EXPECT_TRUE(node);
  EXPECT_EQ(2, node->ino);
  EXPECT_EQ(1, node->parent);
  EXPECT_EQ(1, node->refcount);

  // Nodes have a refcount.
  node->refcount = 2;
  Node* lookup = inodes.Lookup(2);
  EXPECT_EQ(node, lookup);
  EXPECT_EQ(2, node->refcount);

  // Forget reduces the node refcount by 1.
  const ino_t ino = node->ino;
  EXPECT_FALSE(inodes.Forget(ino));
  EXPECT_EQ(1, node->refcount);

  // And removes the node at refcount 0.
  EXPECT_TRUE(inodes.Forget(ino));
  errno = 0;
  EXPECT_FALSE(inodes.Lookup(ino));
  EXPECT_EQ(ENOENT, errno);
  EXPECT_TRUE(inodes.GetName(ino).empty());
}

TEST(FusePathInodesTest, ChildNodeChild) {
  InodeTable inodes;

  // Create a child of the root node.
  Node* node = inodes.Create(1, "foo");
  EXPECT_TRUE(node);
  EXPECT_EQ(2, node->ino);
  EXPECT_EQ(1, node->parent);
  EXPECT_EQ(1, node->refcount);

  // Create a child of the "foo" node.
  Node* child = inodes.Create(2, "bar");
  EXPECT_TRUE(child);
  EXPECT_EQ(3, child->ino);
  EXPECT_EQ(2, child->parent);
  EXPECT_EQ(1, child->refcount);

  // Child node has a name and a full path name.
  EXPECT_EQ("/bar", inodes.GetName(child->ino));
  EXPECT_EQ("/foo/bar", inodes.GetPath(child));

  // Child node can be found by ino lookup.
  EXPECT_EQ(child, inodes.Lookup(child->ino));

  // Child node can be found by parent child lookup.
  EXPECT_EQ(child, inodes.Lookup(2, "bar"));

  // Child node cannot be recreated.
  errno = 0;
  EXPECT_FALSE(inodes.Create(2, "bar"));
  EXPECT_EQ(EEXIST, errno);

  // Child node cannot be overwritten.
  errno = 0;
  EXPECT_FALSE(inodes.Move(node, 2, "bar"));
  EXPECT_EQ(EINVAL, errno);
}

TEST(FusePathInodesTest, ChildNodeMove) {
  InodeTable inodes;

  // Create a child of the root node.
  Node* node = inodes.Create(1, "foo");
  EXPECT_TRUE(node);
  EXPECT_EQ(1, node->parent);
  EXPECT_EQ(2, node->ino);

  // Create a child of the "foo" node.
  Node* child = inodes.Create(2, "bar");
  EXPECT_TRUE(child);
  EXPECT_EQ(2, child->parent);
  EXPECT_EQ(3, child->ino);

  // Create a child of the "bar" node.
  Node* child_child = inodes.Create(3, "baz");
  EXPECT_TRUE(child_child);
  EXPECT_EQ(3, child_child->parent);
  EXPECT_EQ(4, child_child->ino);

  // Child node has a name and a full path name.
  EXPECT_EQ("/bar", inodes.GetName(child->ino));
  EXPECT_EQ("/foo/bar", inodes.GetPath(child));

  // Same for the child of the child node names.
  EXPECT_EQ("/baz", inodes.GetName(child_child->ino));
  EXPECT_EQ("/foo/bar/baz", inodes.GetPath(child_child));

  // Child node cannot be moved to an existing node.
  errno = 0;
  Node* exist = inodes.Move(child, 1, "foo");
  EXPECT_FALSE(exist);
  EXPECT_EQ(EEXIST, errno);

  // Child node can be moved to a new child node.
  EXPECT_EQ(2, child->parent);
  EXPECT_EQ(3, child->ino);
  Node* move = inodes.Move(child, 1, "move");
  EXPECT_EQ(child, move);
  EXPECT_EQ(1, child->parent);
  EXPECT_EQ(3, child->ino);

  // And also be renamed while being moved.
  EXPECT_EQ("/move", inodes.GetName(child->ino));
  EXPECT_EQ("/move", inodes.GetPath(child));

  // Child of the child moves with its parent.
  EXPECT_EQ("/baz", inodes.GetName(child_child->ino));
  EXPECT_EQ("/move/baz", inodes.GetPath(child_child));

  // And its parent and ino should not change.
  EXPECT_EQ(3, child_child->parent);
  EXPECT_EQ(4, child_child->ino);
}

TEST(FusePathInodesTest, ChildNodeRename) {
  InodeTable inodes;

  // Create a child of the root node.
  Node* node = inodes.Create(1, "foo");
  EXPECT_TRUE(node);
  EXPECT_EQ("/foo", inodes.GetName(node->ino));
  EXPECT_EQ("/foo", inodes.GetPath(node));

  // Child nodes can be renamed.
  Node* move = inodes.Move(node, node->parent, "bar");
  EXPECT_EQ(node, move);
  EXPECT_EQ("/bar", inodes.GetName(node->ino));
  EXPECT_EQ("/bar", inodes.GetPath(node));

  // Nodes cannot self-parent because inodes must be unique.
  Node* parent_self = inodes.Move(node, node->ino, "baz");
  EXPECT_FALSE(parent_self);
}

TEST(FusePathInodesTest, ChildNodeEnsure) {
  InodeTable inodes;

  // Ensure can be used to create child nodes.
  Node* foo = inodes.Ensure(1, "foo");
  EXPECT_TRUE(foo);
  EXPECT_EQ(2, foo->ino);
  EXPECT_EQ(1, foo->parent);
  EXPECT_EQ(1, foo->refcount);

  // Ensure and Lookup should return that node.
  EXPECT_EQ(foo, inodes.Ensure(1, "foo"));
  EXPECT_EQ(1, foo->refcount);
  EXPECT_EQ(foo, inodes.Lookup(1, "foo"));
  EXPECT_EQ(1, foo->refcount);

  // Ensure and Lookup can change the node refcount.
  EXPECT_EQ(foo, inodes.Ensure(1, "foo", 2));
  EXPECT_EQ(3, foo->refcount);
  EXPECT_EQ(foo, inodes.Lookup(1, "foo", 2));
  EXPECT_EQ(5, foo->refcount);

  // Create a child of the "foo" node.
  Node* bar = inodes.Ensure(2, "bar", 1);
  EXPECT_TRUE(bar);
  EXPECT_EQ(3, bar->ino);
  EXPECT_EQ(2, bar->parent);
  EXPECT_EQ(2, bar->refcount);

  // Ensure and Lookup should return that node.
  EXPECT_EQ(bar, inodes.Lookup(2, "bar"));
  EXPECT_EQ(2, bar->refcount);
  EXPECT_EQ(bar, inodes.Ensure(2, "bar"));
  EXPECT_EQ(2, bar->refcount);

  // Ensure and Lookup can change the node refcount.
  EXPECT_EQ(bar, inodes.Lookup(2, "bar", 3));
  EXPECT_EQ(5, bar->refcount);
  EXPECT_EQ(bar, inodes.Ensure(2, "bar", 3));
  EXPECT_EQ(8, bar->refcount);
}

TEST(FusePathInodesTest, NodeStatCache) {
  InodeTable inodes;

  // Nodes initially have no cached stat.
  const ino_t ino = inodes.Lookup(1)->ino;
  struct stat stat = {0};
  EXPECT_FALSE(inodes.GetStat(ino, &stat));

  // Cache a stat on the node.
  struct stat stbuf = {0};
  const mode_t mode = S_IFDIR | 0755;
  stbuf.st_mode = mode;
  stbuf.st_nlink = 2;
  inodes.SetStat(ino, stbuf);

  // Get the cached node stat.
  EXPECT_TRUE(inodes.GetStat(ino, &stat));
  EXPECT_EQ(0, stat.st_dev);
  EXPECT_EQ(1, stat.st_ino);
  EXPECT_EQ(mode, stat.st_mode);
  EXPECT_EQ(0, stat.st_size);
  EXPECT_EQ(2, stat.st_nlink);
}

TEST(FusePathInodesTest, NodeStatCacheTimeout) {
  InodeTable inodes;

  Node* node = inodes.Lookup(1);
  EXPECT_TRUE(node);

  // Cache a stat on the node.
  const mode_t mode = S_IFREG | 0755;
  struct stat stbuf = {0};
  stbuf.st_mode = mode;
  stbuf.st_uid = 2;
  stbuf.st_gid = 3;
  inodes.SetStat(node->ino, stbuf, 5.0);

  // Get the cached node stat.
  struct stat stat = {0};
  EXPECT_TRUE(inodes.GetStat(node->ino, &stat));
  EXPECT_EQ(mode, stat.st_mode);
  EXPECT_EQ(1, stat.st_ino);
  EXPECT_EQ(2, stat.st_uid);
  EXPECT_EQ(3, stat.st_gid);

  // Cache stat: use time < 0 to make time move forward.
  inodes.SetStat(node->ino, stbuf, -5.0);

  // Get the stat: should fail due to cache stat timeout.
  EXPECT_FALSE(inodes.GetStat(node->ino, &stat));
}

TEST(FusePathInodesTest, NodeStatCacheForget) {
  InodeTable inodes;

  Node* node = inodes.Lookup(1);
  EXPECT_TRUE(node);

  // Cache a stat on the node.
  const mode_t mode = S_IFREG | 0755;
  struct stat stbuf = {0};
  stbuf.st_dev = 2;
  stbuf.st_mode = mode;
  inodes.SetStat(node->ino, stbuf);

  // Get the cached node stat.
  struct stat stat = {0};
  EXPECT_TRUE(inodes.GetStat(node->ino, &stat));
  EXPECT_EQ(1, stat.st_ino);
  EXPECT_EQ(2, stat.st_dev);
  EXPECT_EQ(mode, stat.st_mode);

  // Forget the cached node stat.
  inodes.ForgetStat(node->ino);
  EXPECT_FALSE(inodes.GetStat(node->ino, &stat));
}

}  // namespace fusebox
