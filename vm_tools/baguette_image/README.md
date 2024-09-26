## How to use the image?

Note: This instruction is subject to change. v4 image is tested on Lazor/Trogdor, but it should also work on x86 devices. Starting with v4 images, btrfs is used for rootfs.

Prerequisites: A DUT running at least R130 with crostini installed (for the DLC).

On DUT:
`vmc create --size 15G baguette` minimum size is 2G, the files in the image currently takes almost 1GB, and I added a few hundred MBs of empty space when making the image.

On workstation:
`scp /path/to/baguette_rootfs.image.zst root@<DUT IP>:/home/chronos/`

On DUT As root:
`zstd -d -c /home/chronos/baguette_rootfs.img.zst | dd of=/run/daemon-store/crosvm/<USER HOME ID>/YmFndWV0dGU=.img conv=notrunc status=progress`

`cp /run/imageloader/termina-dlc/package/root/vm_kernel /home/chronos/user/MyFiles/Downloads/vm_kernel`

On DUT:
```
vmc start --tools-dlc termina-dlc --no-start-lxd --vm-type baguette \
          --kernel /home/chronos/user/MyFiles/Downloads/vm_kernel \
          --kernel-param "root=/dev/vdb rw net.ifnames=0 systemd.log_color=0" \
          --user chronos --user-groups cdrom,dialout,floppy,netdev,sudo,tss,video \
          baguette
```

What to look for:

- You will be welcomed by a shell logged in to the `chronos` user.
- (Ignore the manual env settings underneath if you are running a v2+ image)
- If you install `mesa-utils` and run `DISPLAY=:0 glxgears` (or `vkgears` if you like that better), the gears should show up on your display.
- If you run `df -h`, you should see your entire VM disk space has been occupied by the rootfs.
- If you install `featherpad`, you should see the app icon on your chrome app selector (but clicking it will lead to a window asking you to install borealis).
- If you install `qtwayland5` too, `export XDG_RUNTIME_DIR=/run/user/$(id -u)/` and `QT_QPA_PLATFORM=wayland featherpad` will show you featherpad running on wayland.

Known issue:

- Sommelier might not have been started before profile scripts were run, causing first entry of shell not having display-related envs set

**Happy Testing!**
