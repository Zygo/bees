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
		uint64_t start = 0;
		uint64_t length = Fiemap::s_fiemap_max_offset;
		if (argc > 2) { start = stoull(argv[2], nullptr, 0); }
		if (argc > 3) { length = stoull(argv[3], nullptr, 0); }
		length = min(length, Fiemap::s_fiemap_max_offset - start);
		Fiemap fm(start, length);
		fm.m_flags &= ~(FIEMAP_FLAG_SYNC);
		fm.m_max_count = 100;
		if (argc > 4) { fm.m_flags = stoull(argv[4], nullptr, 0); }
		uint64_t stop_at = start + length;
		uint64_t last_byte = start;
		do {
			fm.do_ioctl(fd);
			// cerr << fm;
			uint64_t last_logical = Fiemap::s_fiemap_max_offset;
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
			fm.m_start = last_logical;
		} while (fm.m_start < stop_at);
	});
	exit(EXIT_SUCCESS);
}

