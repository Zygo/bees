#include "crucible/fd.h"
#include "crucible/fs.h"
#include "crucible/error.h"
#include "crucible/string.h"

#include <iostream>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace crucible;
using namespace std;

int
main(int argc, char **argv)
{
	catch_all([&]() {
		THROW_CHECK1(invalid_argument, argc, argc > 1);
		string filename = argv[1];

	
		cout << "File: " << filename << endl;
		Fd fd = open_or_die(filename, O_RDONLY);
		Fiemap fm;
		fm.fm_flags &= ~(FIEMAP_FLAG_SYNC);
		fm.m_max_count = 100;
		if (argc > 2) { fm.fm_start = stoull(argv[2], nullptr, 0); }
		if (argc > 3) { fm.fm_length = stoull(argv[3], nullptr, 0); }
		if (argc > 4) { fm.fm_flags = stoull(argv[4], nullptr, 0); }
		fm.fm_length = min(fm.fm_length, FIEMAP_MAX_OFFSET - fm.fm_start);
		uint64_t stop_at = fm.fm_start + fm.fm_length;
		uint64_t last_byte = fm.fm_start;
		do {
			fm.do_ioctl(fd);
			// cerr << fm;
			uint64_t last_logical = FIEMAP_MAX_OFFSET;
			for (auto &extent : fm.m_extents) {
				if (extent.fe_logical > last_byte) {
					cout << "Log " << to_hex(last_byte) << ".." << to_hex(extent.fe_logical) << " Hole" << endl;
				}
				cout << "Log " << to_hex(extent.fe_logical) << ".." << to_hex(extent.fe_logical + extent.fe_length)
					<< " Phy " << to_hex(extent.fe_physical) << ".." << to_hex(extent.fe_physical + extent.fe_length)
					<< " Flags " << fiemap_extent_flags_ntoa(extent.fe_flags) << endl;
				last_logical = extent.fe_logical + extent.fe_length;
				last_byte = last_logical;
			}
			fm.fm_start = last_logical;
		} while (fm.fm_start < stop_at);
	});
	exit(EXIT_SUCCESS);
}

