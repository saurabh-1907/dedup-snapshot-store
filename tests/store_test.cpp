#include <gtest/gtest.h>

#include <fstream>
#include <random>
#include <set>

#include "dss/chunker.hpp"
#include "dss/sha256.hpp"
#include "dss/store.hpp"

namespace fs = std::filesystem;

namespace {
std::vector<std::uint8_t> random_bytes(std::size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) b = static_cast<std::uint8_t>(rng());
    return v;
}

void write_file(const fs::path& p, const std::vector<std::uint8_t>& data) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), data.size());
}

std::vector<std::uint8_t> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("dss-test-" + std::to_string(std::random_device{}()));
        fs::create_directories(path_);
    }
    ~TempDir() { fs::remove_all(path_); }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};
}  // namespace

TEST(Sha256, KnownVectors) {
    EXPECT_EQ(dss::Sha256::hex(nullptr, 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::string abc = "abc";
    EXPECT_EQ(dss::Sha256::hex(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Chunker, CoversInputExactlyAndRespectsBounds) {
    dss::ChunkerConfig cfg;
    dss::Chunker chunker(cfg);
    auto data = random_bytes(1 << 20, 1);
    auto spans = chunker.split(data.data(), data.size());
    std::size_t expected = 0;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        EXPECT_EQ(spans[i].offset, expected);
        EXPECT_LE(spans[i].length, cfg.max_size);
        if (i + 1 < spans.size()) EXPECT_GE(spans[i].length, cfg.min_size);
        expected += spans[i].length;
    }
    EXPECT_EQ(expected, data.size());
}

TEST(Chunker, InsertionOnlyDisturbsNearbyChunks) {
    dss::Chunker chunker;
    auto data = random_bytes(512 * 1024, 2);
    auto edited = data;
    edited.insert(edited.begin() + 1000, {1, 2, 3, 4, 5});

    auto digests = [&](const std::vector<std::uint8_t>& d) {
        std::set<std::string> out;
        for (auto s : chunker.split(d.data(), d.size())) out.insert(dss::Sha256::hex(d.data() + s.offset, s.length));
        return out;
    };
    auto a = digests(data), b = digests(edited);
    std::size_t shared = 0;
    for (const auto& d : a) shared += b.count(d);
    EXPECT_GE(shared + 3, a.size());  // all but a couple of chunks survive the edit
}

TEST(Chunker, RejectsBadConfig) {
    EXPECT_THROW(dss::Chunker({1024, 3000, 8192}), std::invalid_argument);
    EXPECT_THROW(dss::Chunker({16384, 8192, 65536}), std::invalid_argument);
}

TEST(Store, BackupRestoreRoundTrip) {
    TempDir tmp;
    fs::path src = tmp.path() / "src", repo = tmp.path() / "repo", out = tmp.path() / "out";
    write_file(src / "a.bin", random_bytes(300 * 1024, 3));
    write_file(src / "nested/b.bin", random_bytes(70 * 1024, 4));
    write_file(src / "empty.txt", {});

    dss::Store store(repo);
    auto stats = store.backup(src, "v1");
    EXPECT_EQ(stats.files, 3u);
    store.restore("v1", out);

    for (auto rel : {"a.bin", "nested/b.bin", "empty.txt"}) EXPECT_EQ(read_file(src / rel), read_file(out / rel)) << rel;
}

TEST(Store, SecondBackupOfUnchangedDataStoresNothingNew) {
    TempDir tmp;
    fs::path src = tmp.path() / "src", repo = tmp.path() / "repo";
    write_file(src / "a.bin", random_bytes(400 * 1024, 5));

    dss::Store store(repo);
    auto first = store.backup(src, "v1");
    auto second = store.backup(src, "v2");
    EXPECT_GT(first.chunks_new, 0u);
    EXPECT_EQ(second.chunks_new, 0u);
    EXPECT_EQ(second.bytes_new, 0u);
    EXPECT_EQ(store.list_snapshots(), (std::vector<std::string>{"v1", "v2"}));
}

TEST(Store, OlderSnapshotStillRestoresAfterFileChanges) {
    TempDir tmp;
    fs::path src = tmp.path() / "src", repo = tmp.path() / "repo", out = tmp.path() / "out";
    auto original = random_bytes(200 * 1024, 6);
    write_file(src / "a.bin", original);

    dss::Store store(repo);
    store.backup(src, "v1");
    write_file(src / "a.bin", random_bytes(200 * 1024, 7));
    store.backup(src, "v2");

    store.restore("v1", out);
    EXPECT_EQ(read_file(out / "a.bin"), original);
}

TEST(Store, DetectsCorruptChunk) {
    TempDir tmp;
    fs::path src = tmp.path() / "src", repo = tmp.path() / "repo";
    write_file(src / "a.bin", random_bytes(10 * 1024, 8));
    dss::Store store(repo);
    store.backup(src, "v1");

    auto snap = store.load_snapshot("v1");
    auto digest = snap.files.at(0).chunks.at(0);
    fs::path chunk = repo / "chunks" / digest.substr(0, 2) / digest;
    write_file(chunk, {0xde, 0xad});
    EXPECT_THROW(store.restore("v1", tmp.path() / "out"), std::runtime_error);
}

TEST(Store, RejectsBadSnapshotIdsAndDuplicates) {
    TempDir tmp;
    fs::path src = tmp.path() / "src";
    write_file(src / "a.txt", {'x'});
    dss::Store store(tmp.path() / "repo");
    EXPECT_THROW(store.backup(src, "../escape"), std::invalid_argument);
    store.backup(src, "v1");
    EXPECT_THROW(store.backup(src, "v1"), std::invalid_argument);
    EXPECT_THROW(store.restore("missing", tmp.path() / "out"), std::runtime_error);
}
