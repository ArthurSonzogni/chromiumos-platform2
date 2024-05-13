This utility allows a directory of binary files to be selectively bsdiff-encoded, and reconstructed on the fly.

The first consumer of this tool is ChromeOS cellular team, where modem firmware is stored in binary formats.

Patch storage can provide benefits in several scenarios:
* Shipping several FW versions for the same modem (MR1 + MR2, or several carrier custpacks)
* Shipping two identical modems (FM101 + RW101 or EM060 + LCK54) that are different in name only.
* Shipping two similar modems (FM101 + EM060) should have some overlap from the shared underlying modem

bsdiff allows efficient patches to be generated between two *similar* binary files. To try and identify files that are similar and address the above use-cases, the following greedy algorithm will process all files present to determine which must be shipped in a full copy, and which can use existing full copies to produce a diff.

1. As it is unintuitive and arbitrary to take diffs of files with very different sizes, a preprocessing step gathers the files into clusters, where files whose sizes are within 20% of each other will be clustered together. Each cluster can then be processed independently with the next steps.

2. All modem firmware is put into a compressed squashfs before being installed on the ChromeOS device. For each file we process, we first compress it to get a baseline size. If the following patch generation attempts don't result in sizes smaller than compression, we won't use a patch.

2. First priority, we attempt patch generation between files with matching filename and depth. Clearly, the following file pairs are for the same purpose:

```
`fm101/19500.0000.00.01.01.52/NON-HLOS.ubi`
`fm101/19500.0000.00.01.02.73/NON-HLOS.ubi`

`fm101/19500.0000.00.01.01.52/NON-HLOS.ubi`
`rw101/19512.0000.00.11.02.01/NON-HLOS.ubi`
```

3. Second priority, we attempt patch generation between files with matching extensions.

```
`fm350/OP_OTA_000.042.img`
`fm350/OP_OTA_000.043.img`
```

4. Finally, we attempt patch generation between the remaining files in the cluster, which have similar file size. As mentioned above, if none of the patches beat compression, we use compression.
