# thinpool_migrator

Thinpool_migrator is a special purpose utility designed to migrate an ext4
stateful partition to an LVM layout where the entire partition resides inside a
thinpool. The thinpool migrator achieves this by resizing the filesystem down
by ~2% of disk size and generating thinpool metadata to make the current
filesystem appear as a thinly provisioned logical volume.

Thinpool migrator is inspired by g2p/lvmify (https://github.com/g2p/lvmify)
that is now a part of g2p/blocks (https://github.com/g2p/blocks) with a slightly
more convoluted step of putting a thinpool underneath the logical volume.
