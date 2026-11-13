# dedup-snapshot-store

A deduplicating snapshot backup engine in C++17 for Linux.

It splits files into variable-size chunks using content-defined chunking, stores each unique chunk once under its SHA-256 hash, and records every backup as a versioned snapshot manifest, so any older version can be restored. Backing up unchanged data a second time writes zero new chunks.

## How it works

```
source dir ──► chunker (gear rolling hash) ──► SHA-256 ──► chunk store (skip if hash exists)
                                                     └──► snapshot manifest (file → ordered chunk list)
```

- **Content-defined chunking** (`src/chunker.cpp`): a gear rolling hash picks chunk boundaries from the bytes themselves (min 2 KiB / avg 8 KiB / max 64 KiB). Inserting bytes near the start of a file only changes the chunks around the edit, not every chunk after it.
- **Content-addressed store** (`src/store.cpp`): chunks live at `chunks/<2-hex-prefix>/<sha256>`. A chunk that already exists is never written again.
- **Crash-safe writes**: every chunk and manifest is written to a temp file, `fsync`'d, atomically `rename`d into place, and the parent directory is `fsync`'d. A crash mid-backup cannot leave a half-written chunk or manifest.
- **Integrity on restore**: each chunk is re-hashed on read; a corrupted chunk fails the restore with an error instead of silently producing bad data.
- **Safety checks**: snapshot IDs are validated, duplicate snapshot IDs are rejected, and manifest paths that escape the target directory are refused on restore.
- **SHA-256** is implemented in-tree (`src/sha256.cpp`), checked against the FIPS 180-4 test vectors.

## Build and run

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure

./build/dss backup  ./repo ./my-files v1
./build/dss backup  ./repo ./my-files v2      # unchanged data → new_chunks=0
./build/dss list    ./repo
./build/dss restore ./repo v1 ./restored
```

Build with AddressSanitizer + UBSan: `cmake -S . -B build -DDSS_SANITIZE=ON`.

## Tests

GoogleTest suite in `tests/store_test.cpp`:

- SHA-256 known-answer vectors
- chunker covers the input exactly and respects min/max sizes
- an insertion only disturbs nearby chunks
- backup → restore round trip (nested dirs, empty files)
- second backup of unchanged data stores nothing new
- older snapshot still restores after the source changes
- corrupt chunk is detected on restore
- invalid / duplicate / missing snapshot IDs are rejected

CI runs the suite under ASan + UBSan on every push.

## Roadmap

Not built yet:

- multi-threaded chunk → hash → compress → write pipeline (bounded queue + thread pool)
- per-chunk compression (zstd / LZ4)
- LRU chunk cache for restores
- replicating chunks to a second machine over TCP
- garbage collection of chunks no snapshot references
- perf-based profiling and throughput benchmarks
