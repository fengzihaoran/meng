# ZenFS utility makefile (use local rocksdb source tree + local librocksdb)

TARGET = zenfs

CC ?= gcc
CXX ?= g++
OBJCOPY ?= objcopy

# ---- Local RocksDB paths ----
ROCKSDB_DIR    ?= /home/femu/rocksdb
ROCKSDB_INCDIR ?= $(ROCKSDB_DIR)/include
# Parent dir so <rocksdb/plugin/...> resolves to $(ROCKSDB_DIR)/plugin/...
ROCKSDB_PARENT ?= $(dir $(ROCKSDB_DIR))
ROCKSDB_LIBDIR ?= $(ROCKSDB_DIR)

# ---- Compile flags ----
CXXFLAGS := -std=c++17 -faligned-new -fno-builtin-memcmp -march=native \
            -DOS_LINUX -DROCKSDB_PLATFORM_POSIX -DROCKSDB_LIB_IO_POSIX \
            -I$(ROCKSDB_INCDIR) \
            -I$(ROCKSDB_PARENT)

# ---- Link flags ----
LIBS := -L$(ROCKSDB_LIBDIR) -lrocksdb \
        -ldl -lpthread -lrt -lsnappy -lgflags -lz -lbz2 -llz4 -lzstd -lnuma -luring \
        -lzbd

# runtime prefer local librocksdb
LDFLAGS += -Wl,-rpath,$(ROCKSDB_LIBDIR)

CXXFLAGS += $(EXTRA_CXXFLAGS)
LDFLAGS  += $(EXTRA_LDFLAGS)

all: $(TARGET) $(TARGET).dbg

$(TARGET).dbg: $(TARGET)
	@$(OBJCOPY) --only-keep-debug $(TARGET) $(TARGET).dbg
	@$(OBJCOPY) --strip-all $(TARGET)

debug: $(TARGET)

$(TARGET): $(TARGET).cc
	$(CXX) $(CXXFLAGS) -g -o $(TARGET) $< $(LIBS) $(LDFLAGS)

clean:
	$(RM) $(TARGET) $(TARGET).dbg