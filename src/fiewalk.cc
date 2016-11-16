#include "crucible/extentwalker.h"
#include "crucible/error.h"
#include "crucible/string.h"

#include <iostream>

#include <fcntl.h>
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
		BtrfsExtentWalker ew(fd);
		off_t pos = 0;
		if (argc > 2) { pos = stoull(argv[2], nullptr, 0); }
		ew.seek(pos);
		do {
			// cout << "\n\n>>>" << ew.current() << "<<<\n\n" << endl;
			cout << ew.current() << endl;
		} while (ew.next());
#if 0
		cout << "\n\n\nAnd now, backwards...\n\n\n" << endl;
		do {
			cout << "\n\n>>>" << ew.current() << "<<<\n\n" << endl;
		} while (ew.prev());
		cout << "\n\n\nDone!\n\n\n" << endl;
#endif
	});
	exit(EXIT_SUCCESS);
}

