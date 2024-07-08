## How to use the image?

Note: This instruction is subject to change. Currently v0-1 images are tested on redrix/brya & lazor/trogdor, v2 image is only tested on lazor/trogdor.

Prerequisites: A DUT running at least 127 with crostini is installed (for the DLC), borealis is not installed.

On DUT:
`vmc create --size 15G baguette` minimum size is 2G, the files in the image currently takes almost 1GB, and I added a few hundred MBs of empty space when making the image.

On workstation:
`scp /path/to/baugette_rootfs.image.zst root@<DUT IP>:/home/chronos/`

On DUT As root:
`zstd -d -c /home/chronos/baguette_rootfs.img.zst | dd of=/run/daemon-store/crosvm/<USER HOME ID>/YmFndWV0dGU=.img conv=notrunc status=progress`

`cp /run/imageloader/termina-dlc/package/root/vm_kernel /home/chronos/user/MyFiles/Downloads/vm_kernel`

On DUT:
`vmc start --tools-dlc termina-dlc --no-start-lxd --vm-type baguette --kernel /home/chronos/user/MyFiles/Downloads/vm_kernel --kernel-param "root=/dev/vdb rw net.ifnames=0 systemd.log_color=0" baguette`

What to look for:

- You will be welcomed by a shell.
- (Ignore the manual env settings underneath if you are running a v2+ image)
- If you install `mesa-utils` and run `DISPLAY=:0 glxgears` (or vkgears if you like that better), the gears should show up on your display.
- If you run `df -h`, you should see your entire VM disk space has been occupied by the rootfs.
- If you install `featherpad`, you should see the app icon on your chrome app selector (but clicking it will lead to a window asking you to install borealis).
- If you install `qtwayland5` too, `export XDG_RUNTIME_DIR=/run/user/1000/` and `QT_QPA_PLATFORM=wayland featherpad` will show you featherpad running on wayland.

Knonw issue:

- Sommelier might not have been started before profile scripts were run, causing first entry of shell not having display-related envs set

Not implemented:

- Username setting via host

**Happy Testing!**
