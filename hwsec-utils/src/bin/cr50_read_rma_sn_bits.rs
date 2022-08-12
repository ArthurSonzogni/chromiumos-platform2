// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hwsec_utils::context::RealContext;
use hwsec_utils::cr50::cr50_read_rma_sn_bits;

fn main() {
    let mut real_ctx = RealContext::new();
    let result = cr50_read_rma_sn_bits(&mut real_ctx);
    match result {
        Ok(rma_sn_bits) => {
            let mut ret = String::new();
            // append sn_data_version
            for byte in rma_sn_bits.sn_data_version {
                ret.push_str(&format!("{:02x}", byte));
            }
            ret.push(':');

            // append rma_status
            ret.push_str(&format!("{:02x}", rma_sn_bits.rma_status));
            ret.push(':');

            // append sn_bits
            for byte in rma_sn_bits.sn_bits {
                ret.push_str(&format!("{:02x}", byte));
            }

            // append standalone_rma_sn_bits if applicable
            if let Some(standalone_rma_sn_bits) = rma_sn_bits.standalone_rma_sn_bits {
                ret.push(' ');
                for byte in standalone_rma_sn_bits {
                    ret.push_str(&format!("{:02x}", byte));
                }
            }

            // display
            println!("{}", ret);
        }
        Err(e) => eprintln!("{}", e),
    }
}
