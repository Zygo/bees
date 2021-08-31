#include "crucible/error.h"
#include "crucible/fd.h"
#include "crucible/fs.h"

using namespace crucible;
using namespace std;

int
main(int argc, char **argv)
{
	if (argc <= 2) {
		cerr << "Usage: " << argv[0] << " FILE1 FILE2 [...FILEn] > OUTFILE" << endl;
		cerr << "Catenates FILE1..FILEN using copy_file_range" << endl;
		return EXIT_FAILURE;
	}

	off_t out_pos = 0;

	while (*++argv) {
		string filename(*argv);

		Fd input_fd = open_or_die(filename, O_RDONLY);

		Stat st(input_fd);

		off_t len = st.st_size;

		cerr << "clone_range(" << filename << ", 0, " << len << ", STDOUT_FILENO, " << out_pos << ")" << flush;
		btrfs_clone_range(input_fd, 0, len, STDOUT_FILENO, out_pos);
		out_pos += len;
		cerr << endl;
	}

	return EXIT_SUCCESS;
}
