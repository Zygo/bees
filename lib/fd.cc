#include "crucible/chatter.h"
#include "crucible/error.h"
#include "crucible/fd.h"
#include "crucible/ntoa.h"
#include "crucible/string.h"

#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

namespace crucible {
	using namespace std;

	static const struct bits_ntoa_table o_flags_table[] = {
		NTOA_TABLE_ENTRY_BITS(O_APPEND),
		NTOA_TABLE_ENTRY_BITS(O_ASYNC),
		NTOA_TABLE_ENTRY_BITS(O_CLOEXEC),
		NTOA_TABLE_ENTRY_BITS(O_CREAT),
		NTOA_TABLE_ENTRY_BITS(O_DIRECT),
		NTOA_TABLE_ENTRY_BITS(O_DIRECTORY),
		NTOA_TABLE_ENTRY_BITS(O_EXCL),
		NTOA_TABLE_ENTRY_BITS(O_LARGEFILE),
		NTOA_TABLE_ENTRY_BITS(O_NOATIME),
		NTOA_TABLE_ENTRY_BITS(O_NOCTTY),
		NTOA_TABLE_ENTRY_BITS(O_NOFOLLOW),
		NTOA_TABLE_ENTRY_BITS(O_NONBLOCK),
		NTOA_TABLE_ENTRY_BITS(O_NDELAY), // NONBLOCK will prevent this
		NTOA_TABLE_ENTRY_BITS(O_SYNC),
		NTOA_TABLE_ENTRY_BITS(O_TRUNC),

		// These aren't really bit values
		NTOA_TABLE_ENTRY_BITS(O_RDWR),
		NTOA_TABLE_ENTRY_BITS(O_WRONLY),
		NTOA_TABLE_ENTRY_BITS(O_RDONLY),

		NTOA_TABLE_ENTRY_END(),
	};

	static const struct bits_ntoa_table o_mode_table[] = {
		NTOA_TABLE_ENTRY_BITS(S_IFMT),
		NTOA_TABLE_ENTRY_BITS(S_IFSOCK),
		NTOA_TABLE_ENTRY_BITS(S_IFLNK),
		NTOA_TABLE_ENTRY_BITS(S_IFREG),
		NTOA_TABLE_ENTRY_BITS(S_IFBLK),
		NTOA_TABLE_ENTRY_BITS(S_IFDIR),
		NTOA_TABLE_ENTRY_BITS(S_IFCHR),
		NTOA_TABLE_ENTRY_BITS(S_IFIFO),
		NTOA_TABLE_ENTRY_BITS(S_ISUID),
		NTOA_TABLE_ENTRY_BITS(S_ISGID),
		NTOA_TABLE_ENTRY_BITS(S_ISVTX),
		NTOA_TABLE_ENTRY_BITS(S_IRWXU),
		NTOA_TABLE_ENTRY_BITS(S_IRUSR),
		NTOA_TABLE_ENTRY_BITS(S_IWUSR),
		NTOA_TABLE_ENTRY_BITS(S_IXUSR),
		NTOA_TABLE_ENTRY_BITS(S_IRWXG),
		NTOA_TABLE_ENTRY_BITS(S_IRGRP),
		NTOA_TABLE_ENTRY_BITS(S_IWGRP),
		NTOA_TABLE_ENTRY_BITS(S_IXGRP),
		NTOA_TABLE_ENTRY_BITS(S_IRWXO),
		NTOA_TABLE_ENTRY_BITS(S_IROTH),
		NTOA_TABLE_ENTRY_BITS(S_IWOTH),
		NTOA_TABLE_ENTRY_BITS(S_IXOTH),
		NTOA_TABLE_ENTRY_END(),
	};

	string o_flags_ntoa(int flags)
	{
		return bits_ntoa(flags, o_flags_table);
	}

	string o_mode_ntoa(mode_t mode)
	{
		return bits_ntoa(mode, o_mode_table);
	}

	void
	IOHandle::close()
	{
		CHATTER_TRACE("close fd " << m_fd << " in " << this);
		if (m_fd >= 0) {
			// Assume that ::close always destroys the FD, even if errors are encountered;
			int closing_fd = m_fd;
			m_fd = -1;
			CHATTER_UNWIND("closing fd " << closing_fd << " in " << this);
			DIE_IF_MINUS_ONE(::close(closing_fd));
		}
	}

	IOHandle::~IOHandle()
	{
		CHATTER_TRACE("destroy fd " << m_fd << " in " << this);
		if (m_fd >= 0) {
			catch_all([&](){
				close();
			});
		}
	}

	IOHandle::IOHandle(int fd) :
		m_fd(fd)
	{
		CHATTER_TRACE("open fd " << m_fd << " in " << this);
	}

	int
	IOHandle::get_fd() const
	{
		return m_fd;
	}

	NamedPtr<IOHandle, int> Fd::s_named_ptr([](int fd) { return make_shared<IOHandle>(fd); });

	Fd::Fd() :
		m_handle(s_named_ptr(-1))
	{
	}

	Fd::Fd(int fd) :
		m_handle(s_named_ptr(fd < 0 ? -1 : fd))
	{
	}

	Fd &
	Fd::operator=(int const fd)
	{
		m_handle = s_named_ptr(fd < 0 ? -1 : fd);
		return *this;
	}

	Fd &
	Fd::operator=(const shared_ptr<IOHandle> &handle)
	{
		m_handle = s_named_ptr.insert(handle, handle->get_fd());
		return *this;
	}

	Fd::operator int() const
	{
		return m_handle->get_fd();
	}

	bool
	Fd::operator!() const
	{
		return m_handle->get_fd() < 0;
	}

	shared_ptr<IOHandle>
	Fd::operator->() const
	{
		return m_handle;
	}

	// XXX: necessary?  useful?
	template <>
	struct ChatterTraits<Fd> {
		Chatter &operator()(Chatter &c, const Fd &fd) const
		{
			c << "Fd {this=" << &fd << " fd=" << static_cast<int>(fd) << "}";
			return c;
		}
	};

	int
	open_or_die(const string &file, int flags, mode_t mode)
	{
		int fd(::open(file.c_str(), flags, mode));
		if (fd < 0) {
			THROW_ERRNO("open: name '" << file << "' mode " << oct << setfill('0') << setw(3) << mode << " flags " << o_flags_ntoa(flags));
		}
		return fd;
	}

	int
	openat_or_die(int dir_fd, const string &file, int flags, mode_t mode)
	{
		int fd(::openat(dir_fd, file.c_str(), flags, mode));
		if (fd < 0) {
			THROW_ERRNO("openat: dir_fd " << dir_fd << " " << name_fd(dir_fd) << " name '" << file << "' mode " << oct << setfill('0') << setw(3) << mode << " flags " << o_flags_ntoa(flags));
		}
		return fd;
	}

	static const struct bits_ntoa_table mmap_prot_table[] = {
		NTOA_TABLE_ENTRY_BITS(PROT_EXEC),
		NTOA_TABLE_ENTRY_BITS(PROT_READ),
		NTOA_TABLE_ENTRY_BITS(PROT_WRITE),
		NTOA_TABLE_ENTRY_BITS(PROT_NONE),
		NTOA_TABLE_ENTRY_END(),
	};

	string mmap_prot_ntoa(int prot)
	{
		return bits_ntoa(prot, mmap_prot_table);
	}

	static const struct bits_ntoa_table mmap_flags_table[] = {
		NTOA_TABLE_ENTRY_BITS(MAP_SHARED),
		NTOA_TABLE_ENTRY_BITS(MAP_PRIVATE),
#ifdef MAP_32BIT
		NTOA_TABLE_ENTRY_BITS(MAP_32BIT),
#endif
		NTOA_TABLE_ENTRY_BITS(MAP_ANONYMOUS),
		NTOA_TABLE_ENTRY_BITS(MAP_DENYWRITE),
		NTOA_TABLE_ENTRY_BITS(MAP_EXECUTABLE),
#ifdef MAP_FILE
		NTOA_TABLE_ENTRY_BITS(MAP_FILE),
#endif
		NTOA_TABLE_ENTRY_BITS(MAP_FIXED),
		NTOA_TABLE_ENTRY_BITS(MAP_GROWSDOWN),
		NTOA_TABLE_ENTRY_BITS(MAP_HUGETLB),
		NTOA_TABLE_ENTRY_BITS(MAP_LOCKED),
		NTOA_TABLE_ENTRY_BITS(MAP_NONBLOCK),
		NTOA_TABLE_ENTRY_BITS(MAP_NORESERVE),
		NTOA_TABLE_ENTRY_BITS(MAP_POPULATE),
		NTOA_TABLE_ENTRY_BITS(MAP_STACK),
#ifdef MAP_UNINITIALIZED
		NTOA_TABLE_ENTRY_BITS(MAP_UNINITIALIZED),
#endif
		NTOA_TABLE_ENTRY_END(),
	};

	string mmap_flags_ntoa(int flags)
	{
		return bits_ntoa(flags, mmap_flags_table);
	}

	void *
	mmap_or_die(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
	{
		void *rv = mmap(addr, length, prot, flags, fd, offset);
		if (rv == MAP_FAILED) {
			THROW_ERRNO("mmap: addr " << addr << " length " << length
				<< " prot " << mmap_prot_ntoa(prot)
				<< " flags " << mmap_flags_ntoa(flags)
				<< " fd " << fd << " offset " << offset);
		}
		return rv;
	}

	void
	rename_or_die(const string &from, const string &to)
	{
		if (::rename(from.c_str(), to.c_str())) {
			THROW_ERRNO("rename: " << from << " -> " << to);
		}
	}

	void
	renameat_or_die(int fromfd, const string &frompath, int tofd, const string &topath)
	{
		if (::renameat(fromfd, frompath.c_str(), tofd, topath.c_str())) {
			THROW_ERRNO("renameat: " << name_fd(fromfd) << "/" << frompath
					<< " -> " << name_fd(tofd) << "/" << topath);
		}
	}

	void
	ftruncate_or_die(int fd, off_t size)
	{
		if (::ftruncate(fd, size)) {
			THROW_ERRNO("ftruncate: " << name_fd(fd) << " size " << size);
		}
	}

	string
	socket_domain_ntoa(int domain)
	{
		static const bits_ntoa_table table[] = {
			NTOA_TABLE_ENTRY_ENUM(AF_UNIX),
			NTOA_TABLE_ENTRY_ENUM(AF_LOCAL),	// probably the same as AF_UNIX
			NTOA_TABLE_ENTRY_ENUM(AF_INET),
			NTOA_TABLE_ENTRY_ENUM(AF_INET6),
			NTOA_TABLE_ENTRY_ENUM(AF_PACKET),
			NTOA_TABLE_ENTRY_END()
		};
		return bits_ntoa(domain, table);
	}

	string
	socket_type_ntoa(int type)
	{
		static const bits_ntoa_table table[] = {
			NTOA_TABLE_ENTRY_BITS(SOCK_CLOEXEC),
			NTOA_TABLE_ENTRY_BITS(SOCK_NONBLOCK),
			NTOA_TABLE_ENTRY_ENUM(SOCK_STREAM),
			NTOA_TABLE_ENTRY_ENUM(SOCK_DGRAM),
			NTOA_TABLE_ENTRY_ENUM(SOCK_RAW),
			NTOA_TABLE_ENTRY_ENUM(SOCK_PACKET),
			NTOA_TABLE_ENTRY_END()
		};
		return bits_ntoa(type, table);
	}

	string
	socket_protocol_ntoa(int protocol)
	{
		static const bits_ntoa_table table[] = {
			// an empty table just prints the number
			NTOA_TABLE_ENTRY_END()
		};
		return bits_ntoa(protocol, table);
	}

	Fd
	socket_or_die(int domain, int type, int protocol)
	{
		Fd fd(::socket(domain, type, protocol));
		if (fd < 0) {
			THROW_ERRNO("socket: domain " << socket_domain_ntoa(domain)
				<< " type " << socket_type_ntoa(type)
				<< " protocol " << socket_protocol_ntoa(protocol));
		}
		return fd;
	}

	void
	write_or_die_partial(int fd, const void *buf, size_t size_wanted, size_t &size_written)
	{
		if (size_wanted > (static_cast<size_t>(~0) >> 1)) {
			THROW_ERROR(invalid_argument, "cannot read " << size_wanted << ", more than signed size allows");
		}
                if (fd < 0) {
                        THROW_ERROR(invalid_argument, "write: trying to write on a closed file descriptor");
                }
		int rv = write(fd, buf, size_wanted);
		if (rv < 0) {
			THROW_ERRNO("write: " << size_wanted << " bytes returned " << rv);
		}
		size_written = rv;
	}

	void
	write_or_die(int fd, const void *buf, size_t size)
	{
		size_t size_written = 0;
		write_or_die_partial(fd, buf, size, size_written);
		if (size_written != size) {
			THROW_ERROR(runtime_error, "write: only " << size_written << " of " << size << " bytes written");
		}
	}

	void
	pwrite_or_die(int fd, const void *buf, size_t size, off_t offset)
	{
		if (size > (static_cast<size_t>(~0) >> 1)) {
			THROW_ERROR(invalid_argument, "pwrite: cannot write " << size << ", more than signed size allows");
		}
                if (fd < 0) {
                        THROW_ERROR(invalid_argument, "pwrite: trying to write on a closed file descriptor");
                }
		int rv = ::pwrite(fd, buf, size, offset);
		if (rv != static_cast<int>(size)) {
			THROW_ERROR(runtime_error, "pwrite: only " << rv << " of " << size << " bytes written at fd " << name_fd(fd) << " offset " << offset);
		}
	}

	template<>
	void
	write_or_die<string>(int fd, const string &text)
	{
		return write_or_die(fd, text.data(), text.size());
	}

	void
	read_partial_or_die(int fd, void *buf, size_t size, size_t &size_read)
	{
		if (size > (static_cast<size_t>(~0) >> 1)) {
			THROW_ERROR(invalid_argument, "cannot read " << size << ", more than signed size allows");
		}
		if (fd < 0) {
			THROW_ERROR(runtime_error, "read: trying to read on a closed file descriptor");
		}
		size_read = 0;
		while (size) {
			int rv = read(fd, buf, size);
			if (rv < 0) {
				if (errno == EINTR) {
					CHATTER_TRACE("resuming after EINTR");
					continue;
				}
				THROW_ERRNO("read: " << size << " bytes");
			}
			if (rv > static_cast<int>(size)) {
				THROW_ERROR(runtime_error, "read: somehow read more bytes (" << rv << ") than requested (" << size << ")");
			}
			if (rv == 0) break;
			size_read += rv;
			size -= rv;
			// CHATTER("read " << rv << " bytes from fd " << fd);
		}
	}

	string
	read_string(int fd, size_t size)
	{
		string rv(size, '\0');
		size_t size_read = 0;
		void *rvp = const_cast<char *>(rv.data());
		read_partial_or_die(fd, rvp, size, size_read);
		rv.resize(size_read);
		return rv;
	}

	void
	read_or_die(int fd, void *buf, size_t size)
	{
		size_t size_read = 0;
		read_partial_or_die(fd, buf, size, size_read);
		if (size_read != size) {
			THROW_ERROR(runtime_error, "read: " << size_read << " of " << size << " bytes");
		}
	}

	void
	pread_or_die(int fd, void *buf, size_t size, off_t offset)
	{
		if (size > (static_cast<size_t>(~0) >> 1)) {
			THROW_ERROR(invalid_argument, "cannot read " << size << ", more than signed size allows");
		}
		if (fd < 0) {
			throw runtime_error("read: trying to read on a closed file descriptor");
		} else {
			while (size) {
				int rv = pread(fd, buf, size, offset);
				if (rv < 0) {
					if (errno == EINTR) {
						CHATTER(__func__ << "resuming after EINTR");
						continue;
					}
					THROW_ERRNO("pread: " << size << " bytes");
				}
				if (rv != static_cast<int>(size)) {
					THROW_ERROR(runtime_error, "pread: " << size << " bytes at fd " << name_fd(fd) << " offset " << offset << " returned " << rv);
				}
				break;
			}
		}
	}

	template<>
	void
	pread_or_die<string>(int fd, string &text, off_t offset)
	{
		return pread_or_die(fd, const_cast<char *>(text.data()), text.size(), offset);
	}

	template<>
	void
	pread_or_die<ByteVector>(int fd, ByteVector &text, off_t offset)
	{
		return pread_or_die(fd, text.data(), text.size(), offset);
	}

	template<>
	void
	pwrite_or_die<ByteVector>(int fd, const ByteVector &text, off_t offset)
	{
		return pwrite_or_die(fd, text.data(), text.size(), offset);
	}

	template<>
	void
	pwrite_or_die<string>(int fd, const string &text, off_t offset)
	{
		return pwrite_or_die(fd, text.data(), text.size(), offset);
	}

	Stat::Stat()
	{
		memset_zero<stat>(this);
	}

	Stat &
	Stat::lstat(const string &filename)
	{
		CHATTER_UNWIND("lstat " << filename);
		DIE_IF_MINUS_ONE(::lstat(filename.c_str(), this));
		return *this;
	}

	Stat &
	Stat::fstat(int fd)
	{
		CHATTER_UNWIND("fstat " << fd);
		DIE_IF_MINUS_ONE(::fstat(fd, this));
		return *this;
	}

	Stat::Stat(int fd)
	{
		memset_zero<stat>(this);
		fstat(fd);
	}

	Stat::Stat(const string &filename)
	{
		memset_zero<stat>(this);
		lstat(filename);
	}

	int
	ioctl_iflags_get(int fd)
	{
		int attr = 0;
		DIE_IF_MINUS_ONE(ioctl(fd, FS_IOC_GETFLAGS, &attr));
		return attr;
	}

	void
	ioctl_iflags_set(int fd, int attr)
	{
		DIE_IF_MINUS_ONE(ioctl(fd, FS_IOC_SETFLAGS, &attr));
	}

	string
	readlink_or_die(const string &path)
	{
		// Start with a reasonable guess since it will usually work
		off_t size = 4096;
		while (size < 1048576) {
			char buf[size + 1];
			int rv;
			DIE_IF_MINUS_ONE(rv = readlink(path.c_str(), buf, size + 1));
			// No negative values allowed except -1
			THROW_CHECK1(runtime_error, rv, rv >= 0);
			if (rv <= size) {
				buf[rv] = 0;
				return buf;
			}
			// cerr << "Retrying readlink(" << path << ", buf, " << size + 1 << ")" << endl;
			// This is from the Linux readlink(2) man page (release 3.44).
			// It only works when the filesystem reports st_size accurately for symlinks,
			// and at least one doesn't, so we can't rely on it at all.
			// size = lstat_or_die(path).st_size;
			size *= 2;
		}
		THROW_ERROR(runtime_error, "readlink: maximum buffer size exceeded");
	}

	static string __relative_path;

	string
	relative_path()
	{
		return __relative_path;
	}

	void
	set_relative_path(string path)
	{
		path = path + "/";
		for (string::size_type i = path.find("//"); i != string::npos; i = path.find("//")) {
			path.erase(i, 1);
		}
		__relative_path = path;
	}

	// Turn a FD into a human-recognizable filename OR an error message.
	string
	name_fd(int fd)
	{
		try {
			ostringstream oss;
			oss << "/proc/self/fd/" << fd;
			string path = readlink_or_die(oss.str());
			if (!__relative_path.empty() && 0 == path.find(__relative_path))
			{
				path.erase(0, __relative_path.length());
			}
			return path;
		} catch (exception &e) {
			return string(e.what());
		}
	}

	bool
	assert_no_leaked_fds()
	{
		struct rlimit rlim;
		int rv = getrlimit(RLIMIT_NOFILE, &rlim);
		if (rv) {
			perror("getrlimit(RLIMIT_NOFILE)");
			// Well, that sucked.  Guess.
			rlim.rlim_cur = 1024;
		}
		CHATTER("Checking for leaked FDs in range 3.." << rlim.rlim_cur);
		int leaked_fds = 0;
		for (unsigned i = 3; i < rlim.rlim_cur; ++i) {
			struct stat buf;
			if (! fstat(i, &buf)) {
				CHATTER("WARNING: fd " << i << " open at exit");
				++leaked_fds;
			}
		}
		CHATTER(leaked_fds << " leaked FD(s) found");
		return leaked_fds == 0;
	}

	pair<Fd, Fd>
	socketpair_or_die(int domain, int type, int protocol)
	{
		pair<Fd, Fd> rv;
                int sv[2];
                DIE_IF_MINUS_ONE(socketpair(domain, type, protocol, sv));
		rv.first = sv[0];
		rv.second = sv[1];
		return rv;
	}

	void
	dup2_or_die(int fd_in, int fd_out)
	{
		DIE_IF_MINUS_ONE(dup2(fd_in, fd_out));
	}

	string
	st_mode_ntoa(mode_t mode)
	{
		static const bits_ntoa_table table[] = {
			NTOA_TABLE_ENTRY_BITS(S_IFMT),
			NTOA_TABLE_ENTRY_BITS(S_IFSOCK),
			NTOA_TABLE_ENTRY_BITS(S_IFLNK),
			NTOA_TABLE_ENTRY_BITS(S_IFMT),
			NTOA_TABLE_ENTRY_BITS(S_IFSOCK),
			NTOA_TABLE_ENTRY_BITS(S_IFLNK),
			NTOA_TABLE_ENTRY_BITS(S_IFREG),
			NTOA_TABLE_ENTRY_BITS(S_IFBLK),
			NTOA_TABLE_ENTRY_BITS(S_IFDIR),
			NTOA_TABLE_ENTRY_BITS(S_IFCHR),
			NTOA_TABLE_ENTRY_BITS(S_IFIFO),
			NTOA_TABLE_ENTRY_BITS(S_ISUID),
			NTOA_TABLE_ENTRY_BITS(S_ISGID),
			NTOA_TABLE_ENTRY_BITS(S_ISVTX),
			NTOA_TABLE_ENTRY_BITS(S_IRWXU),
			NTOA_TABLE_ENTRY_BITS(S_IRUSR),
			NTOA_TABLE_ENTRY_BITS(S_IWUSR),
			NTOA_TABLE_ENTRY_BITS(S_IXUSR),
			NTOA_TABLE_ENTRY_BITS(S_IRWXG),
			NTOA_TABLE_ENTRY_BITS(S_IRGRP),
			NTOA_TABLE_ENTRY_BITS(S_IWGRP),
			NTOA_TABLE_ENTRY_BITS(S_IXGRP),
			NTOA_TABLE_ENTRY_BITS(S_IRWXO),
			NTOA_TABLE_ENTRY_BITS(S_IROTH),
			NTOA_TABLE_ENTRY_BITS(S_IWOTH),
			NTOA_TABLE_ENTRY_BITS(S_IXOTH),
			NTOA_TABLE_ENTRY_END()
		};
		return bits_ntoa(mode, table);
	}

};
