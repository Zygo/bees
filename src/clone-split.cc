#include "crucible/error.h"
#include "crucible/fd.h"
#include "crucible/fs.h"

using namespace crucible;
using namespace std;

int
main(int argc, char **argv)
{
	if (argc != 3) {
		cerr << "Usage: " << argv[0] << " FILE SIZE" << endl;
		cerr << "Splits FILE into SIZE-byte pieces using copy_file_range" << endl;
	}

	string filename(argv[1]);
	off_t out_size(stoull(argv[2], 0, 0));

	Fd input_fd = open_or_die(filename, O_RDONLY);

	Stat st(input_fd);

	for (off_t pos = 0; pos < st.st_size; pos += out_size) {
		char pos_name[64];
		off_t len = min(st.st_size - pos, out_size);
		snprintf(pos_name, sizeof(pos_name), "0x%016llx", static_cast<long long>(pos));
		string out_name = filename + '.' + pos_name;
		cout << out_name << flush;
		Fd output_fd = open_or_die(out_name, O_WRONLY | O_EXCL | O_CREAT);
		btrfs_clone_range(input_fd, pos, len, output_fd, 0);
		cout << endl;
	}

	return 0;
}
