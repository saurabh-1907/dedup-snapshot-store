#include <iostream>
#include <string>

#include "dss/store.hpp"

namespace {
int usage() {
    std::cerr << "usage:\n"
                 "  dss backup  <repo> <source-dir> <snapshot-id>\n"
                 "  dss restore <repo> <snapshot-id> <target-dir>\n"
                 "  dss list    <repo>\n";
    return 2;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    std::string cmd = argv[1];
    try {
        dss::Store store(argv[2]);
        if (cmd == "backup" && argc == 5) {
            auto s = store.backup(argv[3], argv[4]);
            std::cout << "files=" << s.files << " chunks=" << s.chunks_total << " new_chunks=" << s.chunks_new
                      << " bytes=" << s.bytes_total << " new_bytes=" << s.bytes_new << '\n';
        } else if (cmd == "restore" && argc == 5) {
            store.restore(argv[3], argv[4]);
        } else if (cmd == "list" && argc == 3) {
            for (const auto& id : store.list_snapshots()) std::cout << id << '\n';
        } else {
            return usage();
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
