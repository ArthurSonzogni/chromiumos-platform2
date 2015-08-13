// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_generator/delta_diff_utils.h"

#include <algorithm>
#include <map>

#include <base/files/file_util.h>
#include <base/format_macros.h>
#include <base/strings/stringprintf.h>

#include "update_engine/bzip.h"
#include "update_engine/omaha_hash_calculator.h"
#include "update_engine/payload_generator/block_mapping.h"
#include "update_engine/payload_generator/delta_diff_generator.h"
#include "update_engine/payload_generator/extent_ranges.h"
#include "update_engine/payload_generator/extent_utils.h"
#include "update_engine/subprocess.h"
#include "update_engine/utils.h"

using std::map;
using std::string;
using std::vector;

namespace chromeos_update_engine {
namespace {

const char* const kBsdiffPath = "bsdiff";

// The maximum destination size allowed for bsdiff. In general, bsdiff should
// work for arbitrary big files, but the payload generation and payload
// application requires a significant amount of RAM. We put a hard-limit of
// 200 MiB that should not affect any released board, but will limit the
// Chrome binary in ASan builders.
const uint64_t kMaxBsdiffDestinationSize = 200 * 1024 * 1024;  // bytes

// Process a range of blocks from |range_start| to |range_end| in the extent at
// position |*idx_p| of |extents|. If |do_remove| is true, this range will be
// removed, which may cause the extent to be trimmed, split or removed entirely.
// The value of |*idx_p| is updated to point to the next extent to be processed.
// Returns true iff the next extent to process is a new or updated one.
bool ProcessExtentBlockRange(vector<Extent>* extents, size_t* idx_p,
                             const bool do_remove, uint64_t range_start,
                             uint64_t range_end) {
  size_t idx = *idx_p;
  uint64_t start_block = (*extents)[idx].start_block();
  uint64_t num_blocks = (*extents)[idx].num_blocks();
  uint64_t range_size = range_end - range_start;

  if (do_remove) {
    if (range_size == num_blocks) {
      // Remove the entire extent.
      extents->erase(extents->begin() + idx);
    } else if (range_end == num_blocks) {
      // Trim the end of the extent.
      (*extents)[idx].set_num_blocks(num_blocks - range_size);
      idx++;
    } else if (range_start == 0) {
      // Trim the head of the extent.
      (*extents)[idx].set_start_block(start_block + range_size);
      (*extents)[idx].set_num_blocks(num_blocks - range_size);
    } else {
      // Trim the middle, splitting the remainder into two parts.
      (*extents)[idx].set_num_blocks(range_start);
      Extent e;
      e.set_start_block(start_block + range_end);
      e.set_num_blocks(num_blocks - range_end);
      idx++;
      extents->insert(extents->begin() + idx, e);
    }
  } else if (range_end == num_blocks) {
    // Done with this extent.
    idx++;
  } else {
    return false;
  }

  *idx_p = idx;
  return true;
}

// Remove identical corresponding block ranges in |src_extents| and
// |dst_extents|. Used for preventing moving of blocks onto themselves during
// MOVE operations. The value of |total_bytes| indicates the actual length of
// content; this may be slightly less than the total size of blocks, in which
// case the last block is only partly occupied with data. Returns the total
// number of bytes removed.
size_t RemoveIdenticalBlockRanges(vector<Extent>* src_extents,
                                  vector<Extent>* dst_extents,
                                  const size_t total_bytes) {
  size_t src_idx = 0;
  size_t dst_idx = 0;
  uint64_t src_offset = 0, dst_offset = 0;
  bool new_src = true, new_dst = true;
  size_t removed_bytes = 0, nonfull_block_bytes;
  bool do_remove = false;
  while (src_idx < src_extents->size() && dst_idx < dst_extents->size()) {
    if (new_src) {
      src_offset = 0;
      new_src = false;
    }
    if (new_dst) {
      dst_offset = 0;
      new_dst = false;
    }

    do_remove = ((*src_extents)[src_idx].start_block() + src_offset ==
                 (*dst_extents)[dst_idx].start_block() + dst_offset);

    uint64_t src_num_blocks = (*src_extents)[src_idx].num_blocks();
    uint64_t dst_num_blocks = (*dst_extents)[dst_idx].num_blocks();
    uint64_t min_num_blocks = std::min(src_num_blocks - src_offset,
                                       dst_num_blocks - dst_offset);
    uint64_t prev_src_offset = src_offset;
    uint64_t prev_dst_offset = dst_offset;
    src_offset += min_num_blocks;
    dst_offset += min_num_blocks;

    new_src = ProcessExtentBlockRange(src_extents, &src_idx, do_remove,
                                      prev_src_offset, src_offset);
    new_dst = ProcessExtentBlockRange(dst_extents, &dst_idx, do_remove,
                                      prev_dst_offset, dst_offset);
    if (do_remove)
      removed_bytes += min_num_blocks * kBlockSize;
  }

  // If we removed the last block and this block is only partly used by file
  // content, deduct the unused portion from the total removed byte count.
  if (do_remove && (nonfull_block_bytes = total_bytes % kBlockSize))
    removed_bytes -= kBlockSize - nonfull_block_bytes;

  return removed_bytes;
}

}  // namespace

namespace diff_utils {

bool DeltaReadPartition(
    vector<AnnotatedOperation>* aops,
    const PartitionConfig& old_part,
    const PartitionConfig& new_part,
    ssize_t hard_chunk_blocks,
    size_t soft_chunk_blocks,
    BlobFileWriter* blob_file,
    bool src_ops_allowed) {
  ExtentRanges old_visited_blocks;
  ExtentRanges new_visited_blocks;

  TEST_AND_RETURN_FALSE(DeltaMovedAndZeroBlocks(
      aops,
      old_part.path,
      new_part.path,
      old_part.fs_interface ? old_part.fs_interface->GetBlockCount() : 0,
      new_part.fs_interface->GetBlockCount(),
      soft_chunk_blocks,
      src_ops_allowed,
      blob_file,
      &old_visited_blocks,
      &new_visited_blocks));

  map<string, vector<Extent>> old_files_map;
  if (old_part.fs_interface) {
    vector<FilesystemInterface::File> old_files;
    old_part.fs_interface->GetFiles(&old_files);
    for (const FilesystemInterface::File& file : old_files)
      old_files_map[file.name] = file.extents;
  }

  TEST_AND_RETURN_FALSE(new_part.fs_interface);
  vector<FilesystemInterface::File> new_files;
  new_part.fs_interface->GetFiles(&new_files);

  // The processing is very straightforward here, we generate operations for
  // every file (and pseudo-file such as the metadata) in the new filesystem
  // based on the file with the same name in the old filesystem, if any.
  // Files with overlapping data blocks (like hardlinks or filesystems with tail
  // packing or compression where the blocks store more than one file) are only
  // generated once in the new image, but are also used only once from the old
  // image due to some simplifications (see below).
  for (const FilesystemInterface::File& new_file : new_files) {
    // Ignore the files in the new filesystem without blocks. Symlinks with
    // data blocks (for example, symlinks bigger than 60 bytes in ext2) are
    // handled as normal files. We also ignore blocks that were already
    // processed by a previous file.
    vector<Extent> new_file_extents = FilterExtentRanges(
        new_file.extents, new_visited_blocks);
    new_visited_blocks.AddExtents(new_file_extents);

    if (new_file_extents.empty())
      continue;

    LOG(INFO) << "Encoding file " << new_file.name << " ("
              << BlocksInExtents(new_file_extents) << " blocks)";

    // We can't visit each dst image inode more than once, as that would
    // duplicate work. Here, we avoid visiting each source image inode
    // more than once. Technically, we could have multiple operations
    // that read the same blocks from the source image for diffing, but
    // we choose not to avoid complexity. Eventually we will move away
    // from using a graph/cycle detection/etc to generate diffs, and at that
    // time, it will be easy (non-complex) to have many operations read
    // from the same source blocks. At that time, this code can die. -adlr
    vector<Extent> old_file_extents = FilterExtentRanges(
        old_files_map[new_file.name], old_visited_blocks);
    old_visited_blocks.AddExtents(old_file_extents);

    TEST_AND_RETURN_FALSE(DeltaReadFile(
        aops,
        old_part.path,
        new_part.path,
        old_file_extents,
        new_file_extents,
        new_file.name,  // operation name
        hard_chunk_blocks,
        blob_file,
        src_ops_allowed));
  }
  // Process all the blocks not included in any file. We provided all the unused
  // blocks in the old partition as available data.
  vector<Extent> new_unvisited = {
      ExtentForRange(0, new_part.size / kBlockSize)};
  new_unvisited = FilterExtentRanges(new_unvisited, new_visited_blocks);
  if (new_unvisited.empty())
    return true;

  vector<Extent> old_unvisited;
  if (old_part.fs_interface) {
    old_unvisited.push_back(ExtentForRange(0, old_part.size / kBlockSize));
    old_unvisited = FilterExtentRanges(old_unvisited, old_visited_blocks);
  }

  LOG(INFO) << "Scanning " << BlocksInExtents(new_unvisited)
            << " unwritten blocks using chunk size of "
            << soft_chunk_blocks << " blocks.";
  // We use the soft_chunk_blocks limit for the <non-file-data> as we don't
  // really know the structure of this data and we should not expect it to have
  // redundancy between partitions.
  TEST_AND_RETURN_FALSE(DeltaReadFile(
      aops,
      old_part.path,
      new_part.path,
      old_unvisited,
      new_unvisited,
      "<non-file-data>",  // operation name
      soft_chunk_blocks,
      blob_file,
      src_ops_allowed));

  return true;
}

bool DeltaMovedAndZeroBlocks(
    vector<AnnotatedOperation>* aops,
    const string& old_part,
    const string& new_part,
    size_t old_num_blocks,
    size_t new_num_blocks,
    ssize_t chunk_blocks,
    bool src_ops_allowed,
    BlobFileWriter* blob_file,
    ExtentRanges* old_visited_blocks,
    ExtentRanges* new_visited_blocks) {
  vector<BlockMapping::BlockId> old_block_ids;
  vector<BlockMapping::BlockId> new_block_ids;
  TEST_AND_RETURN_FALSE(MapPartitionBlocks(old_part,
                                           new_part,
                                           old_num_blocks * kBlockSize,
                                           new_num_blocks * kBlockSize,
                                           kBlockSize,
                                           &old_block_ids,
                                           &new_block_ids));

  // For minor-version=1, we map all the blocks that didn't move, regardless of
  // the contents since they are already copied and no operation is required.
  if (!src_ops_allowed) {
    uint64_t num_blocks = std::min(old_num_blocks, new_num_blocks);
    for (uint64_t block = 0; block < num_blocks; block++) {
      if (old_block_ids[block] == new_block_ids[block] &&
          !old_visited_blocks->ContainsBlock(block) &&
          !new_visited_blocks->ContainsBlock(block)) {
        old_visited_blocks->AddBlock(block);
        new_visited_blocks->AddBlock(block);
      }
    }
  }

  // A mapping from the block_id to the list of block numbers with that block id
  // in the old partition. This is used to lookup where in the old partition
  // is a block from the new partition.
  map<BlockMapping::BlockId, vector<uint64_t>> old_blocks_map;

  for (uint64_t block = old_num_blocks; block-- > 0; ) {
    if (old_block_ids[block] != 0 && !old_visited_blocks->ContainsBlock(block))
      old_blocks_map[old_block_ids[block]].push_back(block);
  }

  // The collection of blocks in the new partition with just zeros. This is a
  // common case for free-space that's also problematic for bsdiff, so we want
  // to optimize it using REPLACE_BZ operations. The blob for a REPLACE_BZ of
  // just zeros is so small that it doesn't make sense to spend the I/O reading
  // zeros from the old partition.
  vector<Extent> new_zeros;

  vector<Extent> old_identical_blocks;
  vector<Extent> new_identical_blocks;

  for (uint64_t block = 0; block < new_num_blocks; block++) {
    // Only produce operations for blocks that were not yet visited.
    if (new_visited_blocks->ContainsBlock(block))
      continue;
    if (new_block_ids[block] == 0) {
      AppendBlockToExtents(&new_zeros, block);
      continue;
    }

    auto old_blocks_map_it = old_blocks_map.find(new_block_ids[block]);
    // Check if the block exists in the old partition at all.
    if (old_blocks_map_it == old_blocks_map.end() ||
        old_blocks_map_it->second.empty())
      continue;

    AppendBlockToExtents(&old_identical_blocks,
                         old_blocks_map_it->second.back());
    AppendBlockToExtents(&new_identical_blocks, block);
    // We can't reuse source blocks in minor version 1 because the cycle
    // breaking algorithm doesn't support that.
    if (!src_ops_allowed)
      old_blocks_map_it->second.pop_back();
  }

  // Produce operations for the zero blocks split per output extent.
  size_t num_ops = aops->size();
  new_visited_blocks->AddExtents(new_zeros);
  for (Extent extent : new_zeros) {
    TEST_AND_RETURN_FALSE(DeltaReadFile(
        aops,
        "",
        new_part,
        vector<Extent>(),  // old_extents
        vector<Extent>{extent},  // new_extents
        "<zeros>",
        chunk_blocks,
        blob_file,
        src_ops_allowed));
  }
  LOG(INFO) << "Produced " << (aops->size() - num_ops) << " operations for "
            << BlocksInExtents(new_zeros) << " zeroed blocks";

  // Produce MOVE/SOURCE_COPY operations for the moved blocks.
  num_ops = aops->size();
  if (chunk_blocks == -1)
    chunk_blocks = new_num_blocks;
  uint64_t used_blocks = 0;
  old_visited_blocks->AddExtents(old_identical_blocks);
  new_visited_blocks->AddExtents(new_identical_blocks);
  for (Extent extent : new_identical_blocks) {
    // We split the operation at the extent boundary or when bigger than
    // chunk_blocks.
    for (uint64_t op_block_offset = 0; op_block_offset < extent.num_blocks();
         op_block_offset += chunk_blocks) {
      aops->emplace_back();
      AnnotatedOperation* aop = &aops->back();
      aop->name = "<identical-blocks>";
      aop->op.set_type(src_ops_allowed ?
                       DeltaArchiveManifest_InstallOperation_Type_SOURCE_COPY :
                       DeltaArchiveManifest_InstallOperation_Type_MOVE);

      uint64_t chunk_num_blocks =
        std::min(extent.num_blocks() - op_block_offset,
                 static_cast<uint64_t>(chunk_blocks));

      // The current operation represents the move/copy operation for the
      // sublist starting at |used_blocks| of length |chunk_num_blocks| where
      // the src and dst are from |old_identical_blocks| and
      // |new_identical_blocks| respectively.
      StoreExtents(
          ExtentsSublist(old_identical_blocks, used_blocks, chunk_num_blocks),
          aop->op.mutable_src_extents());

      Extent* op_dst_extent = aop->op.add_dst_extents();
      op_dst_extent->set_start_block(extent.start_block() + op_block_offset);
      op_dst_extent->set_num_blocks(chunk_num_blocks);
      CHECK(
          vector<Extent>{*op_dst_extent} ==  // NOLINT(whitespace/braces)
          ExtentsSublist(new_identical_blocks, used_blocks, chunk_num_blocks));

      used_blocks += chunk_num_blocks;
    }
  }
  LOG(INFO) << "Produced " << (aops->size() - num_ops) << " operations for "
            << used_blocks << " identical blocks moved";

  return true;
}

bool DeltaReadFile(
    vector<AnnotatedOperation>* aops,
    const string& old_part,
    const string& new_part,
    const vector<Extent>& old_extents,
    const vector<Extent>& new_extents,
    const string& name,
    ssize_t chunk_blocks,
    BlobFileWriter* blob_file,
    bool src_ops_allowed) {
  chromeos::Blob data;
  DeltaArchiveManifest_InstallOperation operation;

  uint64_t total_blocks = BlocksInExtents(new_extents);
  if (chunk_blocks == -1)
    chunk_blocks = total_blocks;

  // If bsdiff breaks again, blacklist the problem file by using:
  //   bsdiff_allowed = (name != "/foo/bar")
  //
  // TODO(dgarrett): chromium-os:15274 connect this test to the command line.
  bool bsdiff_allowed = true;
  if (static_cast<uint64_t>(chunk_blocks) * kBlockSize >
      kMaxBsdiffDestinationSize) {
    bsdiff_allowed = false;
  }

  if (!bsdiff_allowed) {
    LOG(INFO) << "bsdiff blacklisting: " << name;
  }

  for (uint64_t block_offset = 0; block_offset < total_blocks;
      block_offset += chunk_blocks) {
    // Split the old/new file in the same chunks. Note that this could drop
    // some information from the old file used for the new chunk. If the old
    // file is smaller (or even empty when there's no old file) the chunk will
    // also be empty.
    vector<Extent> old_extents_chunk = ExtentsSublist(
        old_extents, block_offset, chunk_blocks);
    vector<Extent> new_extents_chunk = ExtentsSublist(
        new_extents, block_offset, chunk_blocks);
    NormalizeExtents(&old_extents_chunk);
    NormalizeExtents(&new_extents_chunk);

    TEST_AND_RETURN_FALSE(ReadExtentsToDiff(old_part,
                                            new_part,
                                            old_extents_chunk,
                                            new_extents_chunk,
                                            bsdiff_allowed,
                                            &data,
                                            &operation,
                                            src_ops_allowed));

    // Check if the operation writes nothing.
    if (operation.dst_extents_size() == 0) {
      if (operation.type() == DeltaArchiveManifest_InstallOperation_Type_MOVE) {
        LOG(INFO) << "Empty MOVE operation ("
                  << name << "), skipping";
        continue;
      } else {
        LOG(ERROR) << "Empty non-MOVE operation";
        return false;
      }
    }

    // Now, insert into the list of operations.
    AnnotatedOperation aop;
    aop.name = name;
    if (static_cast<uint64_t>(chunk_blocks) < total_blocks) {
      aop.name = base::StringPrintf("%s:%" PRIu64,
                                    name.c_str(), block_offset / chunk_blocks);
    }
    aop.op = operation;

    // Write the data
    if (operation.type() != DeltaArchiveManifest_InstallOperation_Type_MOVE &&
        operation.type() !=
            DeltaArchiveManifest_InstallOperation_Type_SOURCE_COPY) {
      TEST_AND_RETURN_FALSE(aop.SetOperationBlob(&data, blob_file));
    } else {
      TEST_AND_RETURN_FALSE(blob_file->StoreBlob(data) != -1);
    }
    aops->emplace_back(aop);
  }
  return true;
}

bool ReadExtentsToDiff(const string& old_part,
                       const string& new_part,
                       const vector<Extent>& old_extents,
                       const vector<Extent>& new_extents,
                       bool bsdiff_allowed,
                       chromeos::Blob* out_data,
                       DeltaArchiveManifest_InstallOperation* out_op,
                       bool src_ops_allowed) {
  DeltaArchiveManifest_InstallOperation operation;
  // Data blob that will be written to delta file.
  const chromeos::Blob* data_blob = nullptr;

  // We read blocks from old_extents and write blocks to new_extents.
  uint64_t blocks_to_read = BlocksInExtents(old_extents);
  uint64_t blocks_to_write = BlocksInExtents(new_extents);

  // Make copies of the extents so we can modify them.
  vector<Extent> src_extents = old_extents;
  vector<Extent> dst_extents = new_extents;

  // Read in bytes from new data.
  chromeos::Blob new_data;
  TEST_AND_RETURN_FALSE(utils::ReadExtents(new_part,
                                           new_extents,
                                           &new_data,
                                           kBlockSize * blocks_to_write,
                                           kBlockSize));
  TEST_AND_RETURN_FALSE(!new_data.empty());


  // Using a REPLACE is always an option.
  operation.set_type(DeltaArchiveManifest_InstallOperation_Type_REPLACE);
  data_blob = &new_data;

  // Try compressing it with bzip2.
  chromeos::Blob new_data_bz;
  TEST_AND_RETURN_FALSE(BzipCompress(new_data, &new_data_bz));
  CHECK(!new_data_bz.empty());
  if (new_data_bz.size() < data_blob->size()) {
    // A REPLACE_BZ is better.
    operation.set_type(DeltaArchiveManifest_InstallOperation_Type_REPLACE_BZ);
    data_blob = &new_data_bz;
  }

  chromeos::Blob old_data;
  chromeos::Blob empty_blob;
  chromeos::Blob bsdiff_delta;
  if (blocks_to_read > 0) {
    // Read old data.
    TEST_AND_RETURN_FALSE(
        utils::ReadExtents(old_part, src_extents, &old_data,
                           kBlockSize * blocks_to_read, kBlockSize));
    if (old_data == new_data) {
      // No change in data.
      if (src_ops_allowed) {
        operation.set_type(
            DeltaArchiveManifest_InstallOperation_Type_SOURCE_COPY);
      } else {
        operation.set_type(DeltaArchiveManifest_InstallOperation_Type_MOVE);
      }
      data_blob = &empty_blob;
    } else if (bsdiff_allowed) {
      // If the source file is considered bsdiff safe (no bsdiff bugs
      // triggered), see if BSDIFF encoding is smaller.
      base::FilePath old_chunk;
      TEST_AND_RETURN_FALSE(base::CreateTemporaryFile(&old_chunk));
      ScopedPathUnlinker old_unlinker(old_chunk.value());
      TEST_AND_RETURN_FALSE(
          utils::WriteFile(old_chunk.value().c_str(),
                           old_data.data(), old_data.size()));
      base::FilePath new_chunk;
      TEST_AND_RETURN_FALSE(base::CreateTemporaryFile(&new_chunk));
      ScopedPathUnlinker new_unlinker(new_chunk.value());
      TEST_AND_RETURN_FALSE(
          utils::WriteFile(new_chunk.value().c_str(),
                           new_data.data(), new_data.size()));

      TEST_AND_RETURN_FALSE(
          BsdiffFiles(old_chunk.value(), new_chunk.value(), &bsdiff_delta));
      CHECK_GT(bsdiff_delta.size(), static_cast<chromeos::Blob::size_type>(0));
      if (bsdiff_delta.size() < data_blob->size()) {
        if (src_ops_allowed) {
          operation.set_type(
              DeltaArchiveManifest_InstallOperation_Type_SOURCE_BSDIFF);
        } else {
          operation.set_type(DeltaArchiveManifest_InstallOperation_Type_BSDIFF);
        }
        data_blob = &bsdiff_delta;
      }
    }
  }

  size_t removed_bytes = 0;
  // Remove identical src/dst block ranges in MOVE operations.
  if (operation.type() == DeltaArchiveManifest_InstallOperation_Type_MOVE) {
    removed_bytes = RemoveIdenticalBlockRanges(
        &src_extents, &dst_extents, new_data.size());
  }
  // Set legacy src_length and dst_length fields.
  operation.set_src_length(old_data.size() - removed_bytes);
  operation.set_dst_length(new_data.size() - removed_bytes);

  // Embed extents in the operation.
  StoreExtents(src_extents, operation.mutable_src_extents());
  StoreExtents(dst_extents, operation.mutable_dst_extents());

  // Replace operations should not have source extents.
  if (operation.type() == DeltaArchiveManifest_InstallOperation_Type_REPLACE ||
      operation.type() ==
          DeltaArchiveManifest_InstallOperation_Type_REPLACE_BZ) {
    operation.clear_src_extents();
    operation.clear_src_length();
  }

  *out_data = std::move(*data_blob);
  *out_op = operation;

  return true;
}

// Runs the bsdiff tool on two files and returns the resulting delta in
// 'out'. Returns true on success.
bool BsdiffFiles(const string& old_file,
                 const string& new_file,
                 chromeos::Blob* out) {
  const string kPatchFile = "delta.patchXXXXXX";
  string patch_file_path;

  TEST_AND_RETURN_FALSE(
      utils::MakeTempFile(kPatchFile, &patch_file_path, nullptr));

  vector<string> cmd;
  cmd.push_back(kBsdiffPath);
  cmd.push_back(old_file);
  cmd.push_back(new_file);
  cmd.push_back(patch_file_path);

  int rc = 1;
  chromeos::Blob patch_file;
  TEST_AND_RETURN_FALSE(Subprocess::SynchronousExec(cmd, &rc, nullptr));
  TEST_AND_RETURN_FALSE(rc == 0);
  TEST_AND_RETURN_FALSE(utils::ReadFile(patch_file_path, out));
  unlink(patch_file_path.c_str());
  return true;
}

// Returns true if |op| is a no-op operation that doesn't do any useful work
// (e.g., a move operation that copies blocks onto themselves).
bool IsNoopOperation(const DeltaArchiveManifest_InstallOperation& op) {
  return (op.type() == DeltaArchiveManifest_InstallOperation_Type_MOVE &&
          ExpandExtents(op.src_extents()) == ExpandExtents(op.dst_extents()));
}

void FilterNoopOperations(vector<AnnotatedOperation>* ops) {
  ops->erase(
      std::remove_if(
          ops->begin(), ops->end(),
          [](const AnnotatedOperation& aop){return IsNoopOperation(aop.op);}),
      ops->end());
}

bool InitializePartitionInfo(const PartitionConfig& part, PartitionInfo* info) {
  info->set_size(part.size);
  OmahaHashCalculator hasher;
  TEST_AND_RETURN_FALSE(hasher.UpdateFile(part.path, part.size) ==
                        static_cast<off_t>(part.size));
  TEST_AND_RETURN_FALSE(hasher.Finalize());
  const chromeos::Blob& hash = hasher.raw_hash();
  info->set_hash(hash.data(), hash.size());
  LOG(INFO) << part.path << ": size=" << part.size << " hash=" << hasher.hash();
  return true;
}

bool CompareAopsByDestination(AnnotatedOperation first_aop,
                              AnnotatedOperation second_aop) {
  // We want empty operations to be at the end of the payload.
  if (!first_aop.op.dst_extents().size() || !second_aop.op.dst_extents().size())
    return ((!first_aop.op.dst_extents().size()) <
            (!second_aop.op.dst_extents().size()));
  uint32_t first_dst_start = first_aop.op.dst_extents(0).start_block();
  uint32_t second_dst_start = second_aop.op.dst_extents(0).start_block();
  return first_dst_start < second_dst_start;
}

}  // namespace diff_utils

}  // namespace chromeos_update_engine
