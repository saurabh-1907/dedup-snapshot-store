#include "dss/store.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "dss/sha256.hpp"

namespace dss {
namespace {

[[noreturn]] void throw_errno(const std::string& what) {
    throw std::system_error(errno, std::generic_category(), what);
}

std::vector<std::uint8_t> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + p.string());
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.';
    }) && id != "." && id != "..";
}

}  // namespace

void atomic_write(const fs::path& dst, const std::uint8_t* data, std::size_t len) {
    fs::create_directories(dst.parent_path());
    fs::path tmp = dst;
    tmp += ".tmp." + std::to_string(::getpid());

    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) throw_errno("open " + tmp.string());
    std::size_t done = 0;
    while (done < len) {
        ssize_t n = ::write(fd, data + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            throw_errno("write " + tmp.string());
        }
        done += static_cast<std::size_t>(n);
    }
    if (::fsync(fd) != 0) { ::close(fd); throw_errno("fsync " + tmp.string()); }
    if (::close(fd) != 0) throw_errno("close " + tmp.string());
    if (::rename(tmp.c_str(), dst.c_str()) != 0) throw_errno("rename " + dst.string());

    int dfd = ::open(dst.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }
}

Store::Store(fs::path root, ChunkerConfig cfg) : root_(std::move(root)), chunker_(cfg) {
    fs::create_directories(root_ / "chunks");
    fs::create_directories(root_ / "snapshots");
}

fs::path Store::chunk_path(const std::string& digest) const {
    return root_ / "chunks" / digest.substr(0, 2) / digest;
}

bool Store::has_chunk(const std::string& digest) const { return fs::exists(chunk_path(digest)); }

bool Store::put_chunk(const std::string& digest, const std::uint8_t* data, std::size_t len) {
    if (has_chunk(digest)) return false;  // dedup: identical content already stored
    atomic_write(chunk_path(digest), data, len);
    return true;
}

std::vector<std::uint8_t> Store::read_chunk(const std::string& digest) const {
    auto bytes = read_file(chunk_path(digest));
    if (Sha256::hex(bytes.data(), bytes.size()) != digest)
        throw std::runtime_error("chunk " + digest + " is corrupt (hash mismatch)");
    return bytes;
}

BackupStats Store::backup(const fs::path& source_dir, const std::string& snapshot_id) {
    if (!valid_id(snapshot_id)) throw std::invalid_argument("invalid snapshot id: " + snapshot_id);
    if (!fs::is_directory(source_dir)) throw std::invalid_argument("not a directory: " + source_dir.string());
    if (fs::exists(root_ / "snapshots" / (snapshot_id + ".manifest")))
        throw std::invalid_argument("snapshot already exists: " + snapshot_id);

    Snapshot snap{snapshot_id, {}};
    BackupStats stats;
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(source_dir))
        if (e.is_regular_file()) files.push_back(e.path());
    std::sort(files.begin(), files.end());

    for (const auto& path : files) {
        auto bytes = read_file(path);
        FileEntry entry;
        entry.path = fs::relative(path, source_dir).generic_string();
        entry.size = bytes.size();
        for (const auto& span : chunker_.split(bytes.data(), bytes.size())) {
            const std::uint8_t* p = bytes.data() + span.offset;
            std::string digest = Sha256::hex(p, span.length);
            if (put_chunk(digest, p, span.length)) {
                ++stats.chunks_new;
                stats.bytes_new += span.length;
            }
            ++stats.chunks_total;
            entry.chunks.push_back(std::move(digest));
        }
        stats.bytes_total += entry.size;
        ++stats.files;
        snap.files.push_back(std::move(entry));
    }
    write_manifest(snap);
    return stats;
}

void Store::write_manifest(const Snapshot& snap) {
    std::ostringstream out;
    out << "dss-manifest 1\n";
    for (const auto& f : snap.files) {
        out << "file " << f.size << ' ' << f.chunks.size() << ' ' << f.path << '\n';
        for (const auto& c : f.chunks) out << c << '\n';
    }
    std::string s = out.str();
    atomic_write(root_ / "snapshots" / (snap.id + ".manifest"),
                 reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

Snapshot Store::load_snapshot(const std::string& snapshot_id) const {
    if (!valid_id(snapshot_id)) throw std::invalid_argument("invalid snapshot id: " + snapshot_id);
    std::ifstream in(root_ / "snapshots" / (snapshot_id + ".manifest"));
    if (!in) throw std::runtime_error("snapshot not found: " + snapshot_id);
    std::string header;
    std::getline(in, header);
    if (header != "dss-manifest 1") throw std::runtime_error("bad manifest header in " + snapshot_id);

    Snapshot snap{snapshot_id, {}};
    std::string tag;
    while (in >> tag) {
        if (tag != "file") throw std::runtime_error("malformed manifest " + snapshot_id);
        FileEntry f;
        std::size_t n = 0;
        in >> f.size >> n;
        in.get();  // the single space before the path
        std::getline(in, f.path);
        for (std::size_t i = 0; i < n; ++i) {
            std::string d;
            if (!(in >> d) || d.size() != 64) throw std::runtime_error("truncated manifest " + snapshot_id);
            f.chunks.push_back(d);
        }
        snap.files.push_back(std::move(f));
    }
    return snap;
}

void Store::restore(const std::string& snapshot_id, const fs::path& target_dir) const {
    Snapshot snap = load_snapshot(snapshot_id);
    for (const auto& f : snap.files) {
        fs::path rel(f.path);
        if (rel.is_absolute() || rel.lexically_normal().string().rfind("..", 0) == 0)
            throw std::runtime_error("unsafe path in manifest: " + f.path);
        std::vector<std::uint8_t> data;
        data.reserve(f.size);
        for (const auto& d : f.chunks) {
            auto c = read_chunk(d);
            data.insert(data.end(), c.begin(), c.end());
        }
        if (data.size() != f.size) throw std::runtime_error("size mismatch restoring " + f.path);
        atomic_write(target_dir / rel, data.data(), data.size());
    }
}

std::vector<std::string> Store::list_snapshots() const {
    std::vector<std::string> ids;
    for (const auto& e : fs::directory_iterator(root_ / "snapshots"))
        if (e.path().extension() == ".manifest") ids.push_back(e.path().stem().string());
    std::sort(ids.begin(), ids.end());
    return ids;
}

}  // namespace dss
