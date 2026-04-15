sudo rm -f /tmp/zenfs_pe_state
sudo rm -f /tmp/zenfs_pe_state
mkdir -p /tmp/zenfs-aux
sudo ./plugin/zenfs/util/zenfs mkfs --zbd=nvme0n1 --aux_path=/tmp/zenfs-aux --force