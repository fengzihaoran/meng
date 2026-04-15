sudo sh -c "echo mq-deadline > /sys/block/nvme0n1/queue/scheduler"

sudo rm -rf /home/femu/rocksdb/plugin/zenfs/log

mkdir -p /home/femu/rocksdb/plugin/zenfs/log

sudo /home/femu/rocksdb/plugin/zenfs/util/zenfs mkfs --zbd=nvme0n1 --aux-path=/home/femu/rocksdb/plugin/zenfs/log --force

sudo chown -R femu:femu /home/femu/rocksdb/plugin/zenfs/log