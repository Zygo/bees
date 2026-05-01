// test-snapshot-dedup.cc
// Tests LOGICAL_INO → multi-dest FIDEDUPERANGE for snapshot consolidation
//
// Requires: mounted btrfs filesystem at argv[1] with test data:
//   data/fileA, data/fileB (identical content, different extents)
//   .snapshots/snap1/fileA, snap1/fileB, snap2/fileA, snap2/fileB, snap3/fileA, snap3/fileB
//
// Usage: sudo ./test-snapshot-dedup /mnt/test-btrfs

#include "crucible/fs.h"
#include "crucible/error.h"
#include "crucible/fd.h"

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <sys/ioctl.h>

using namespace crucible;
using namespace std;

// Get physical extent offset via FIEMAP
uint64_t get_physical_offset(const string &path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        cerr << "Cannot open " << path << ": " << strerror(errno) << endl;
        return 0;
    }

    char buf[sizeof(struct fiemap) + sizeof(struct fiemap_extent)] = {};
    struct fiemap *fm = reinterpret_cast<struct fiemap *>(buf);

    fm->fm_start = 0;
    fm->fm_length = ~0ULL;
    fm->fm_flags = 0;
    fm->fm_extent_count = 1;

    if (ioctl(fd, FS_IOC_FIEMAP, fm) < 0) {
        cerr << "FIEMAP failed for " << path << ": " << strerror(errno) << endl;
        close(fd);
        return 0;
    }

    uint64_t phys = 0;
    if (fm->fm_mapped_extents > 0) {
        phys = fm->fm_extents[0].fe_physical;
    }
    close(fd);
    return phys;
}

// Print extent info for all files
void print_extents(const string &mount, const vector<string> &relpaths) {
    for (const auto &rel : relpaths) {
        string full = mount + "/" + rel;
        uint64_t phys = get_physical_offset(full);
        printf("  %-40s phys=%lu\n", rel.c_str(), phys);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <btrfs-mountpoint>" << endl;
        return 1;
    }

    string mount = argv[1];
    int mount_fd = open(mount.c_str(), O_RDONLY);
    if (mount_fd < 0) {
        cerr << "Cannot open " << mount << ": " << strerror(errno) << endl;
        return 1;
    }

    vector<string> all_files = {
        "data/fileA", "data/fileB",
        ".snapshots/snap1/fileA", ".snapshots/snap1/fileB",
        ".snapshots/snap2/fileA", ".snapshots/snap2/fileB",
        ".snapshots/snap3/fileA", ".snapshots/snap3/fileB",
    };

    cout << "=== TEST 1: Extents VOR Dedup ===" << endl;
    print_extents(mount, all_files);

    uint64_t phys_A = get_physical_offset(mount + "/data/fileA");
    uint64_t phys_B = get_physical_offset(mount + "/data/fileB");
    assert(phys_A != phys_B && "fileA and fileB should have different extents");
    cout << "OK: fileA and fileB have different extents" << endl;

    // --- Step 1: Get fileB's physical offset BEFORE dedup ---
    uint64_t old_phys_B = phys_B;
    cout << "\nfileB's physical offset before dedup: " << old_phys_B << endl;

    // --- Step 2: FIDEDUPERANGE fileA → fileB (1:1, like bees does) ---
    cout << "\n=== Dedup: fileA → fileB (1:1) ===" << endl;
    {
        string src_path = mount + "/data/fileA";
        string dst_path = mount + "/data/fileB";
        int src_fd = open(src_path.c_str(), O_RDONLY);
        int dst_fd = open(dst_path.c_str(), O_RDONLY);
        assert(src_fd >= 0 && dst_fd >= 0);

        struct stat st;
        fstat(src_fd, &st);
        off_t size = st.st_size;

        BtrfsExtentSame bes(src_fd, 0, size);
        bes.add(dst_fd, 0);
        bes.do_ioctl();

        cout << "  status=" << bes.m_info[0].status
             << " bytes_deduped=" << bes.m_info[0].bytes_deduped << endl;
        assert(bes.m_info[0].status == 0 && "Dedup should succeed");
        close(src_fd);
        close(dst_fd);
    }

    cout << "\n=== Extents NACH 1:1 Dedup ===" << endl;
    print_extents(mount, all_files);

    // Verify: live files share extent, snapshots of B still on old extent
    uint64_t new_phys_B = get_physical_offset(mount + "/data/fileB");
    assert(new_phys_B == phys_A && "Live fileB should now share fileA's extent");
    uint64_t snap1_B = get_physical_offset(mount + "/.snapshots/snap1/fileB");
    assert(snap1_B == old_phys_B && "snap1/fileB should still have old extent");
    cout << "OK: Live deduped, snapshots NOT consolidated (as expected)" << endl;

    // --- Step 3: LOGICAL_INO on old_phys_B → find all inodes on old extent ---
    cout << "\n=== LOGICAL_INO: who references old extent " << old_phys_B << "? ===" << endl;
    BtrfsIoctlLogicalInoArgs logical_ino(old_phys_B);
    logical_ino.do_ioctl(mount_fd);

    cout << "  Found " << logical_ino.m_iors.size() << " references:" << endl;
    vector<pair<int, off_t>> snapshot_targets;  // fd + offset for multi-dest

    for (const auto &ior : logical_ino.m_iors) {
        cout << "    inum=" << ior.m_inum << " offset=" << ior.m_offset
             << " root=" << ior.m_root << endl;

        // Open by root+inum via INO_PATH
        BtrfsIoctlInoPathArgs ino_path(ior.m_inum);
        if (ino_path.do_ioctl_nothrow(mount_fd)) {
            for (const auto &p : ino_path.m_paths) {
                cout << "      path=" << p << endl;
            }
        }

        // Open the file for multi-dest dedup
        // We need to open via subvolume — use INO_LOOKUP
        BtrfsIoctlInoLookupArgs ino_lookup(ior.m_inum);
        // treeid = root
        ino_lookup.treeid = ior.m_root;
        if (ino_lookup.do_ioctl_nothrow(mount_fd)) {
            string relpath(ino_lookup.name);
            // Open relative to mount
            // For snapshots we need to find the subvol mount path
            // For now: try INO_PATH result
        }
    }

    // --- Step 4: Multi-dest FIDEDUPERANGE ---
    // Use fileA as src, all snapshot/fileB copies as destinations
    cout << "\n=== Multi-dest FIDEDUPERANGE: fileA → all snapshot/fileB ===" << endl;
    {
        string src_path = mount + "/data/fileA";
        int src_fd = open(src_path.c_str(), O_RDONLY);
        assert(src_fd >= 0);

        struct stat st;
        fstat(src_fd, &st);
        off_t size = st.st_size;

        BtrfsExtentSame bes(src_fd, 0, size);

        // Add all snapshot fileB copies as destinations
        vector<string> snap_B_paths = {
            mount + "/.snapshots/snap1/fileB",
            mount + "/.snapshots/snap2/fileB",
            mount + "/.snapshots/snap3/fileB",
        };
        vector<int> dst_fds;
        for (const auto &p : snap_B_paths) {
            int fd = open(p.c_str(), O_RDONLY);
            if (fd >= 0) {
                bes.add(fd, 0);
                dst_fds.push_back(fd);
                cout << "  Added dest: " << p << endl;
            } else {
                cerr << "  Cannot open " << p << ": " << strerror(errno) << endl;
            }
        }

        bes.do_ioctl();

        for (size_t i = 0; i < bes.m_info.size(); i++) {
            cout << "  dest[" << i << "] status=" << bes.m_info[i].status
                 << " bytes_deduped=" << bes.m_info[i].bytes_deduped << endl;
        }

        for (int fd : dst_fds) close(fd);
        close(src_fd);
    }

    cout << "\n=== Extents NACH Multi-dest Dedup ===" << endl;
    print_extents(mount, all_files);

    // Final verification
    cout << "\n=== Final Verification ===" << endl;
    uint64_t expected = phys_A;
    bool all_ok = true;
    for (const auto &rel : all_files) {
        uint64_t phys = get_physical_offset(mount + "/" + rel);
        bool ok = (phys == expected);
        printf("  %-40s phys=%-12lu %s\n", rel.c_str(), phys, ok ? "OK" : "FAIL");
        if (!ok) all_ok = false;
    }

    close(mount_fd);

    if (all_ok) {
        cout << "\n=== ALL TESTS PASSED ===" << endl;
        return 0;
    } else {
        cout << "\n=== SOME TESTS FAILED ===" << endl;
        return 1;
    }
}
