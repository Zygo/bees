#include <crucible/error.h>
#include <crucible/fd.h>
#include <crucible/ntoa.h>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

#include <unistd.h>
#include <sys/fanotify.h>

using namespace crucible;
using namespace std;

static
void
usage(const char *name)
{
	cerr << "Usage: " << name << " directory" << endl;
	cerr << "Reports fanotify events from directory" << endl;
}

struct fan_read_block {
	struct fanotify_event_metadata fem;
	// more here in the future.  Maybe.
};

static inline
string
fan_flag_ntoa(uint64_t ui)
{
	static const bits_ntoa_table flag_names[] = {
		NTOA_TABLE_ENTRY_BITS(FAN_ACCESS),
		NTOA_TABLE_ENTRY_BITS(FAN_OPEN),
		NTOA_TABLE_ENTRY_BITS(FAN_MODIFY),
		NTOA_TABLE_ENTRY_BITS(FAN_CLOSE),
		NTOA_TABLE_ENTRY_BITS(FAN_CLOSE_WRITE),
		NTOA_TABLE_ENTRY_BITS(FAN_CLOSE_NOWRITE),
		NTOA_TABLE_ENTRY_BITS(FAN_Q_OVERFLOW),
		NTOA_TABLE_ENTRY_BITS(FAN_ACCESS_PERM),
		NTOA_TABLE_ENTRY_BITS(FAN_OPEN_PERM),
		NTOA_TABLE_ENTRY_END()
	};
	return bits_ntoa(ui, flag_names);
}

int
main(int argc, char **argv)
{
	if (argc < 1) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	Fd fd;

	DIE_IF_MINUS_ONE(fd = fanotify_init(FAN_CLASS_NOTIF, O_RDONLY | O_LARGEFILE | O_CLOEXEC | O_NOATIME));

	for (char **argvp = argv + 1; *argvp; ++argvp) {
		cerr << "fanotify_mark(" << *argvp << ")..." << flush;
		DIE_IF_MINUS_ONE(fanotify_mark(fd, FAN_MARK_ADD | FAN_MARK_MOUNT, FAN_CLOSE_WRITE | FAN_CLOSE_NOWRITE | FAN_OPEN, FAN_NOFD, *argvp));
		cerr << endl;
	}

	while (1) {
		struct fan_read_block frb;
		read_or_die(fd, frb);

#if 0
		cout << "event_len\t= " << frb.fem.event_len << endl;
		cout << "vers\t= " << static_cast<int>(frb.fem.vers) << endl;
		cout << "reserved\t= " << static_cast<int>(frb.fem.reserved) << endl;
		cout << "metadata_len\t= " << frb.fem.metadata_len << endl;
		cout << "mask\t= " << hex << frb.fem.mask << dec << "\t" << fan_flag_ntoa(frb.fem.mask) << endl;
		cout << "fd\t= " << frb.fem.fd << endl;
		cout << "pid\t= " << frb.fem.pid << endl;
#endif

		cout << "flags " << fan_flag_ntoa(frb.fem.mask) << " pid " << frb.fem.pid << ' ' << flush;

		Fd event_fd(frb.fem.fd);
		ostringstream oss;
		oss << "/proc/self/fd/" << event_fd;
		cout << "file " << readlink_or_die(oss.str()) << endl;

		// cout << endl;
	}
	
	return EXIT_SUCCESS;
}
