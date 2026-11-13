#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "dss/chunker.hpp"

namespace dss {

namespace fs = std::filesystem;

struct FileEntry {
    std::string path;                 // relative to the backed-up root
    std::uint64_t size = 0;
    std::vector<std::string> chunks;  // SHA-256 hex digests, in order
};

struct Snapshot {
    std::string id;
    std::vector<FileEntry> files;
};

struct BackupStats {
    std::size_t files = 0;
    std::size_t chunks_total = 0;
    std::size_t chunks_new = 0;     // chunks actually written to disk
    std::uint64_t bytes_total = 0;
    std::uint64_t bytes_new = 0;
};

// Content-addressed chunk store with versioned snapshot manifests.
//
// Layout under the repository root:
//   chunks/<first 2 hex chars>/<sha256>   one file per unique chunk
//   snapshots/<id>.manifest               text manifest per snapshot
//
// Every write goes to a temp file, is fsync'd, then atomically renamed into
// place, so a crash mid-backup never leaves a half-written chunk or manifest.
class Store {
public:
    explicit Store(fs::path root, ChunkerConfig cfg = {});

    BackupStats backup(const fs::path& source_dir, const std::string& snapshot_id);
    void restore(const std::string& snapshot_id, const fs::path& target_dir) const;
    Snapshot load_snapshot(const std::string& snapshot_id) const;
    std::vector<std::string> list_snapshots() const;

    bool has_chunk(const std::string& digest) const;
    std::vector<std::uint8_t> read_chunk(const std::string& digest) const;

private:
    fs::path chunk_path(const std::string& digest) const;
    bool put_chunk(const std::string& digest, const std::uint8_t* data, std::size_t len);
    void write_manifest(const Snapshot& snap);

    fs::path root_;
    Chunker chunker_;
};

// Writes `data` to `dst` crash-safely: temp file + fsync + rename + fsync(dir).
void atomic_write(const fs::path& dst, const std::uint8_t* data, std::size_t len);

}  // namespace dss
