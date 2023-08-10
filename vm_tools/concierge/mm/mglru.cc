// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/mglru.h"

#include <utility>
#include <vector>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include <vm_memory_management/vm_memory_management.pb.h>

#include "vm_tools/concierge/byte_unit.h"

using vm_tools::vm_memory_management::MglruGeneration;
using vm_tools::vm_memory_management::MglruMemcg;
using vm_tools::vm_memory_management::MglruNode;

namespace vm_tools::concierge::mglru {
namespace {

// Parses a single generation from the input line and returns it if successful.
// On failure a std::nullopt is returned.
std::optional<MglruGeneration> ParseGeneration(const base::StringPiece line) {
  std::vector<base::StringPiece> tokens =
      base::SplitStringPiece(line, base::kWhitespaceASCII,
                             base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // sequence_num, timestamp_msec, anon_pages, file_pages.
  constexpr size_t kNumGenVals = 4;
  uint32_t vals[kNumGenVals];

  // A generation should have at least 4 distinct numbers.
  if (tokens.size() < kNumGenVals) {
    return std::nullopt;
  }

  for (size_t i = 0; i < kNumGenVals; i++) {
    if (!base::StringToUint(tokens[i], &vals[i])) {
      return std::nullopt;
    }
  }

  MglruGeneration gen;

  gen.set_sequence_num(vals[0]);
  gen.set_timestamp_msec(vals[1]);
  // Note: The values are set as pages here even though the final units will be
  // KiB. The conversion is handled at the end of parsing in
  // ConvertStatsToKbUnits.
  gen.set_anon_kb(vals[2]);
  gen.set_file_kb(vals[3]);

  return gen;
}

// Parses a single node from the input lines and returns it if successful. The
// line index is expected to be at the starting position for the node, otherwise
// this function will fail. On failure a std::nullopt is returned and the index
// is reset to the original position.
std::optional<MglruNode> ParseNode(const std::vector<base::StringPiece>& lines,
                                   size_t& index) {
  size_t starting_position = index;

  // A node always starts with the node identifier token.
  if (index >= lines.size()) {
    return std::nullopt;
  }

  std::vector<base::StringPiece> tokens =
      base::SplitStringPiece(lines.at(index++), base::kWhitespaceASCII,
                             base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // The first line of the node is 'node' followed by the node's id.
  uint32_t node_id;
  if (tokens.size() < 2 || tokens[0] != "node" ||
      !base::StringToUint(tokens[1], &node_id)) {
    index = starting_position;
    return std::nullopt;
  }

  MglruNode new_node;
  new_node.set_id(node_id);

  // Next is one or more generations, each of which consists of a single line of
  // text. Parse generations until we encounter a failure. The first failure
  // indicates the end of the list of generations.
  std::optional<MglruGeneration> parsed_gen;
  while (index < lines.size() &&
         (parsed_gen = ParseGeneration(lines.at(index++)))) {
    new_node.mutable_generations()->Add(std::move(*parsed_gen));
  }

  // If the last generation failed, decrement the line index since the last line
  // was not a valid generation.
  if (!parsed_gen) {
    index--;
  }

  // If no generations were parsed, there is an error.
  if (new_node.generations_size() == 0) {
    index = starting_position;
    return std::nullopt;
  }

  return new_node;
}

// Parses a single memcg from the input lines and returns it if successful. The
// line index is expected to be at the starting position for the memcg,
// otherwise this function will fail. On failure the line index is reset to its
// original position and a std::nullopt is returned.
std::optional<MglruMemcg> ParseMemcg(
    const std::vector<base::StringPiece>& lines, size_t& index) {
  size_t starting_index = index;

  if (index >= lines.size()) {
    return std::nullopt;
  }

  std::vector<base::StringPiece> tokens =
      base::SplitStringPiece(lines.at(index++), base::kWhitespaceASCII,
                             base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // The first line of a memcg is always 'memcg' followed by the id.
  uint32_t id;
  if (tokens.size() < 2 || tokens[0] != "memcg" ||
      !base::StringToUint(tokens[1], &id)) {
    index = starting_index;
    return std::nullopt;
  }

  MglruMemcg new_memcg;
  new_memcg.set_id(id);

  // After the id is a list of one or more nodes. Parse nodes until failure. The
  // first failure indicates the end of the list of nodes.
  std::optional<MglruNode> parsed_node;
  while ((parsed_node = ParseNode(lines, index))) {
    new_memcg.mutable_nodes()->Add(std::move(*parsed_node));
  }

  // If no nodes were parsed, then there was an error.
  if (new_memcg.nodes_size() == 0) {
    index = starting_index;
    return std::nullopt;
  }

  return new_memcg;
}

void ConvertStatsToKbUnits(MglruStats& stats, const size_t page_size) {
  // Convert the page sizes from the file to KiB sizes.
  const size_t page_k = (page_size / KiB(1));

  for (size_t i = 0; i < stats.cgs_size(); i++) {
    MglruMemcg* cg = stats.mutable_cgs(i);
    for (size_t j = 0; j < cg->nodes_size(); j++) {
      MglruNode* node = cg->mutable_nodes(j);
      for (size_t k = 0; k < node->generations_size(); k++) {
        MglruGeneration* gen = node->mutable_generations(k);
        gen->set_anon_kb(gen->anon_kb() * page_k);
        gen->set_file_kb(gen->file_kb() * page_k);
      }
    }
  }
}

}  // namespace

std::optional<MglruStats> ParseStatsFromString(
    const base::StringPiece stats_string, const size_t page_size) {
  std::vector<base::StringPiece> lines = base::SplitStringPiece(
      stats_string, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  MglruStats parsed_stats;

  size_t index = 0;

  // The MGLRU stats file is a list of one or more memcgs. Parse until failure.
  // The first failure indicates the end of the list of memcgs.
  std::optional<MglruMemcg> parsed_memcg;
  while ((parsed_memcg = ParseMemcg(lines, index))) {
    parsed_stats.mutable_cgs()->Add(std::move(*parsed_memcg));
  }

  // If the parsing did not consume the entire input file, or if no memcgs were
  // parsed, then something went wrong.
  if (parsed_stats.cgs_size() == 0 || index != lines.size()) {
    return std::nullopt;
  }

  ConvertStatsToKbUnits(parsed_stats, page_size);

  return parsed_stats;
}

std::string StatsToString(const MglruStats& stats, const size_t page_size) {
  std::stringstream output;

  size_t page_k = (page_size / KiB(1));

  for (size_t i = 0; i < stats.cgs_size(); i++) {
    const MglruMemcg& cg = stats.cgs(i);
    output << "memcg  " << cg.id() << '\n';

    for (size_t j = 0; j < cg.nodes_size(); j++) {
      const MglruNode& node = cg.nodes(j);
      output << "  node  " << node.id() << '\n';
      for (size_t k = 0; k < node.generations_size(); k++) {
        const MglruGeneration& gen = node.generations(k);
        output << "    " << gen.sequence_num() << "  " << gen.timestamp_msec()
               << "  " << gen.anon_kb() / page_k << "  "
               << gen.file_kb() / page_k << '\n';
      }
    }
  }

  return output.str();
}

}  // namespace vm_tools::concierge::mglru
