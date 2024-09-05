// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use std::str::FromStr;
use system_api::vm_memory_management::MglruGeneration;
use system_api::vm_memory_management::MglruMemcg;
use system_api::vm_memory_management::MglruNode;
use system_api::vm_memory_management::MglruStats;

// The raw MGLRU stats consist of multiple memcgs, nodes, and generations, formatted as follows:

// memcg     0
// node     0
// 695      40523      18334        4175
// 696      35101      35592       22242
// 697      10961      32552       12081
// 698       3419      21460        4438

pub fn parse_mglru_stats(raw_stats: &str, page_size: usize) -> Result<MglruStats> {
    let mut stats: MglruStats = MglruStats::new();
    let mut line_iter = raw_stats.lines().peekable();

    while let Ok(memcg) = parse_memcg(&mut line_iter, page_size) {
        stats.cgs.push(memcg);
    }

    if stats.cgs.len() == 0 {
        bail!("No memcg found");
    }

    if line_iter.peek() != None {
        bail!("Does not reach to the end of the stats");
    }

    return Ok(stats);
}

pub fn parse_memcg<'a, I: std::iter::Iterator<Item = &'a str>>(
    line_iter: &mut std::iter::Peekable<I>,
    page_size: usize,
) -> Result<MglruMemcg> {
    let Some(line) = line_iter.peek() else {
        bail!("No line is found");
    };
    let mut iter = line.split_whitespace();

    match iter.next() {
        None => bail!("Not a memcg line"),
        Some(word) => {
            if word != "memcg" {
                bail!("Not a memcg line");
            }
        }
    }

    let Ok(memcg_id) = u32::from_str(iter.next().context("Memcg id is not found")?) else {
        bail!("Memcg id is invalid");
    };

    let mut memcg: MglruMemcg = Default::default();
    memcg.id = memcg_id;
    line_iter.next();

    while let Ok(parsed_node) = parse_node(line_iter, page_size) {
        memcg.nodes.push(parsed_node);
    }

    if memcg.nodes.len() == 0 {
        bail!("Node is not found");
    }

    Ok(memcg)
}

pub fn parse_node<'a, I: std::iter::Iterator<Item = &'a str>>(
    line_iter: &mut std::iter::Peekable<I>,
    page_size: usize,
) -> Result<MglruNode> {
    let Some(line) = line_iter.peek() else {
        bail!("No line is found");
    };
    let mut iter = line.split_whitespace();
    match iter.next() {
        None => bail!("Not a node line"),
        Some(word) => {
            if word != "node" {
                bail!("Not a node line");
            }
        }
    }

    let Ok(node_id) = u32::from_str(iter.next().context("Node id is not found")?) else {
        bail!("Node id is invalid");
    };
    let mut parsed_node: MglruNode = Default::default();
    parsed_node.id = node_id;

    line_iter.next();

    loop {
        let Some(line) = line_iter.peek() else {
            break;
        };
        let Ok(parsed_gen) = parse_gen(line, page_size) else {
            break;
        };
        parsed_node.generations.push(parsed_gen);
        line_iter.next();
    }
    if parsed_node.generations.len() == 0 {
        bail!("First generation in node is not found");
    }
    Ok(parsed_node)
}

pub fn parse_gen(line: &str, page_size: usize) -> Result<MglruGeneration> {
    let mut iter = line.split_whitespace();

    // A generation is composed of a single line of text with 4 integer values:
    // <generation number> <age timestamp> <anonymous pages> <file pages>
    const NUM_GEN_VALS: usize = 4;

    let mut values = [0_u32; NUM_GEN_VALS];
    for i in 0..NUM_GEN_VALS {
        let Some(value_in_str) = iter.next() else {
            bail!("Does not have enough values for a generation");
        };
        let Ok(value_i64) = i64::from_str(value_in_str) else {
            bail!("Failed to parse a number");
        };

        // If the nr_pages are of negative values, convert it to 0.
        values[i] = match value_i64 {
            v if v < 0 => 0,
            v if v > u32::MAX as i64 => u32::MAX,
            v => v as u32,
        };
    }

    let mut parsed_gen: MglruGeneration = Default::default();
    parsed_gen.sequence_num = values[0];
    parsed_gen.timestamp_msec = values[1];
    const KB: u32 = 1024;
    parsed_gen.anon_kb = values[2] * (page_size as u32 / KB);
    parsed_gen.file_kb = values[3] * (page_size as u32 / KB);

    return Ok(parsed_gen);
}

#[cfg(test)]
mod tests {
    use crate::mglru::parse_mglru_stats;
    use system_api::vm_memory_management::MglruGeneration;
    use system_api::vm_memory_management::MglruMemcg;
    use system_api::vm_memory_management::MglruNode;
    use system_api::vm_memory_management::MglruStats;

    fn create_generation(
        sequence_num: u32,
        timestamp_msec: u32,
        anon_kb: u32,
        file_kb: u32,
    ) -> MglruGeneration {
        let mut new_generation = MglruGeneration::new();
        new_generation.sequence_num = sequence_num;
        new_generation.timestamp_msec = timestamp_msec;
        new_generation.anon_kb = anon_kb;
        new_generation.file_kb = file_kb;
        return new_generation;
    }

    fn create_node(id: u32) -> MglruNode {
        let mut new_node = MglruNode::new();
        new_node.id = id;
        return new_node;
    }

    fn create_memcg(id: u32) -> MglruMemcg {
        let mut new_memcg = MglruMemcg::new();
        new_memcg.id = id;
        return new_memcg;
    }

    #[test]
    fn simple_input() {
        let input = r"memcg     1
        node     2
               3      4      5        6
";
        let expected_generation = create_generation(3, 4, 5, 6);
        let mut expected_node = create_node(2);
        expected_node.generations.push(expected_generation);
        let mut expected_memcg = create_memcg(1);
        expected_memcg.nodes.push(expected_node);
        let mut expected_stats: MglruStats = MglruStats::new();
        expected_stats.cgs.push(expected_memcg);

        assert_eq!(
            parse_mglru_stats(input, 1024)
                .expect("Should not return an Error with the given input"),
            expected_stats
        );
    }

    #[test]
    fn page_size_conversion() {
        let input = r"memcg     1
        node     2
               3      4      5        6
";
        let expected_generation = create_generation(3, 4, 20, 24);
        let mut expected_node = create_node(2);
        expected_node.generations.push(expected_generation);
        let mut expected_memcg = create_memcg(1);
        expected_memcg.nodes.push(expected_node);
        let mut expected_stats: MglruStats = MglruStats::new();
        expected_stats.cgs.push(expected_memcg);

        assert_eq!(
            parse_mglru_stats(input, 4096)
                .expect("Should not return an Error with the given input"),
            expected_stats
        );
    }

    #[test]
    fn simple_input_without_the_last_line() {
        let input = r"memcg     1
        node     2
               3      4      5        6       ";
        let expected_generation = create_generation(3, 4, 5, 6);
        let mut expected_node = create_node(2);
        expected_node.generations.push(expected_generation);
        let mut expected_memcg = create_memcg(1);
        expected_memcg.nodes.push(expected_node);
        let mut expected_stats: MglruStats = MglruStats::new();
        expected_stats.cgs.push(expected_memcg);

        assert_eq!(
            parse_mglru_stats(input, 1024)
                .expect("Should not return an Error with the given input"),
            expected_stats
        );
    }

    #[test]
    fn empty() {
        let input = r"";
        assert!(parse_mglru_stats(input, 1024).is_err())
    }

    #[test]
    fn parse_memcg_missing_cg_id_fails() {
        let input = r"memcg
            node     0
                   695      40523      18334        4175
";
        assert!(parse_mglru_stats(input, 1024).is_err())
    }

    #[test]
    fn wrong_token_the_second_cg() {
        let input = r"memcg 1
            node     0
                   695      40523      18334        4175
                   696      40523      18334        4175
                   697      40523      18334        4175
            memcg
            node     0
                   695      40523      18334        4175
                   696      40523      18334        4175
                   697      40523      18334        4175
";
        assert!(parse_mglru_stats(input, 1024).is_err())
    }

    #[test]
    fn wrong_token_node() {
        let input = r"memcg     0
        Pnode     0
               695      40523      18334        4175
";
        assert!(parse_mglru_stats(input, 1024).is_err())
    }

    #[test]
    fn missing_cg_header() {
        let input = r"node     0
        695      40523      18334        4175
";
        assert!(parse_mglru_stats(input, 1024).is_err())
    }

    #[test]
    fn missing_node_header() {
        let input = r"memcg     0
        695      40523      18334        4175
";
        assert!(parse_mglru_stats(input, 1024).is_err())
    }

    #[test]
    fn missing_generation() {
        let input = r"memcg     0
        node     0
";
        assert!(parse_mglru_stats(input, 1024).is_err())
    }

    #[test]
    fn too_big_generation() {
        let input = r"memcg     0
        node     1
               695      40523      18334        4175 55
               696      40523      18334        4175
";
        let mut expected_generation = create_generation(695, 40523, 18334, 4175);
        let mut expected_node = create_node(1);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(696, 40523, 18334, 4175);
        expected_node.generations.push(expected_generation);
        let mut expected_memcg = create_memcg(0);
        expected_memcg.nodes.push(expected_node);
        let mut expected_stats: MglruStats = MglruStats::new();
        expected_stats.cgs.push(expected_memcg);

        assert_eq!(
            parse_mglru_stats(input, 1024)
                .expect("Should not return an Error with the given input"),
            expected_stats
        );
    }

    #[test]
    fn too_small_generation() {
        let input = r"memcg     0
        node     0
               695      40523      18334
               695      40523      18334        4175
";

        assert!(parse_mglru_stats(input, 1024).is_err())
    }

    #[test]
    fn test_multiple() {
        let input = r"memcg     0
        node     0
               695      40523      18334        4175
               696      35101      35592       22242
               697      10961      32552       12081
               698       3419      21460        4438
        node     1
               695      40523      18334        4175
               696      35101      35592       22242
               697      10961      32552       12081
               698       3419      21460        4438
       memcg     1
        node     0
               695      40523      18334        4175
               696      35101      35592       22242
               697      10961      32552       12081
               698       3419      21460        4438       ";

        let mut expected_stats: MglruStats = MglruStats::new();
        let mut expected_memcg = create_memcg(0);
        let mut expected_node = create_node(0);
        let mut expected_generation = create_generation(695, 40523, 18334, 4175);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(696, 35101, 35592, 22242);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(697, 10961, 32552, 12081);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(698, 3419, 21460, 4438);
        expected_node.generations.push(expected_generation);
        expected_memcg.nodes.push(expected_node);

        expected_node = create_node(1);
        expected_generation = create_generation(695, 40523, 18334, 4175);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(696, 35101, 35592, 22242);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(697, 10961, 32552, 12081);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(698, 3419, 21460, 4438);
        expected_node.generations.push(expected_generation);
        expected_memcg.nodes.push(expected_node);

        expected_stats.cgs.push(expected_memcg);

        expected_memcg = create_memcg(1);
        expected_node = create_node(0);
        expected_generation = create_generation(695, 40523, 18334, 4175);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(696, 35101, 35592, 22242);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(697, 10961, 32552, 12081);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(698, 3419, 21460, 4438);
        expected_node.generations.push(expected_generation);
        expected_memcg.nodes.push(expected_node);
        expected_stats.cgs.push(expected_memcg);

        assert_eq!(
            parse_mglru_stats(input, 1024)
                .expect("Should not return an Error with the given input"),
            expected_stats
        );
    }

    #[test]
    // New kernel versions have a trailing '/' after the memcg id
    fn multiple_new_kernel() {
        let input = r"memcg     1 /
        node     0
                 0       1177          0         822
                 1       1177          7           0
                 2       1177          0           0
                 3       1177       1171        5125
";
        let mut expected_stats: MglruStats = MglruStats::new();
        let mut expected_memcg = create_memcg(1);
        let mut expected_node = create_node(0);
        let mut expected_generation = create_generation(0, 1177, 0, 822);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(1, 1177, 7, 0);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(2, 1177, 0, 0);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(3, 1177, 1171, 5125);
        expected_node.generations.push(expected_generation);
        expected_memcg.nodes.push(expected_node);
        expected_stats.cgs.push(expected_memcg);

        assert_eq!(
            parse_mglru_stats(input, 1024)
                .expect("Should not return an Error with the given input"),
            expected_stats
        );
    }

    #[test]
    // Kernel 5.4 versions can have nr_anon_pages/nr_file_pages as -0.
    // Parse it as 0 for this case.
    fn multiple_old_kernel() {
        let input = r"memcg     1 /
        node     0
                 0       1177          -0         822
                 1       1177          7           0
                 2       1177          0           0
                 3       1177       1171        5125
";
        let mut expected_stats: MglruStats = MglruStats::new();
        let mut expected_memcg = create_memcg(1);
        let mut expected_node = create_node(0);
        let mut expected_generation = create_generation(0, 1177, 0, 822);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(1, 1177, 7, 0);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(2, 1177, 0, 0);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(3, 1177, 1171, 5125);
        expected_node.generations.push(expected_generation);
        expected_memcg.nodes.push(expected_node);
        expected_stats.cgs.push(expected_memcg);
        assert_eq!(
            parse_mglru_stats(input, 1024)
                .expect("Should not return an Error with the given input"),
            expected_stats
        );
    }

    #[test]
    // Test when nr_anon_pages/nr_file_pages are of negative values,
    // they will be converted to 0.
    fn test_negative_values() {
        let input = r"memcg     1 /
        node     0
                 0       1177          -5         822
                 1       1177          7          -100
                 2       1177          0           0
                 3       1177       1171        5125
";
        let mut expected_stats: MglruStats = MglruStats::new();
        let mut expected_memcg = create_memcg(1);
        let mut expected_node = create_node(0);
        let mut expected_generation = create_generation(0, 1177, 0, 822);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(1, 1177, 7, 0);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(2, 1177, 0, 0);
        expected_node.generations.push(expected_generation);
        expected_generation = create_generation(3, 1177, 1171, 5125);
        expected_node.generations.push(expected_generation);
        expected_memcg.nodes.push(expected_node);
        expected_stats.cgs.push(expected_memcg);

        assert_eq!(
            parse_mglru_stats(input, 1024)
                .expect("Should not return an Error with the given input"),
            expected_stats
        );
    }
}
