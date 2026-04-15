sudo /home/femu/rocksdb/db_bench --benchmarks=fillseq --num=1000000 --value_size=100 --fs_uri=zenfs://dev:nvme0n1 --use_existing_db=0

sudo /home/femu/rocksdb/db_bench --benchmarks=fillrandom,overwrite --num=2000000 --value_size=1024 --fs_uri=zenfs://dev:nvme0n1 --use_existing_db=0

sudo ./db_bench --benchmarks=fillrandom,overwrite,overwrite --num=1000000 --value_size=1024 --fs_uri=zenfs://dev:nvme0n1 --use_existing_db=0

sudo ./db_bench \
  --benchmarks=fillrandom,overwrite \
  --num=100000 \
  --value_size=1024 \
  --fs_uri=zenfs://dev:nvme0n1 \
  --use_existing_db=0