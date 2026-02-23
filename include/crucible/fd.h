#ifndef CRUCIBLE_FD_H
#define CRUCIBLE_FD_H

#include "crucible/bytevector.h"
#include "crucible/namedptr.h"

#include <cstring>

#include <string>
#include <vector>

// open
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// ioctl
#include <sys/ioctl.h>
#include <linux/fs.h>

// socket
#include <sys/socket.h>

// pread/pwrite
#include <unistd.h>

namespace crucible {
	using namespace std;

	/// File descriptor owner object.  It closes them when destroyed.
	/// Most of the functions here don't use it because these functions don't own FDs.
	/// All good names for such objects are taken.
	class IOHandle {
		IOHandle(const IOHandle &) = delete;
		IOHandle(IOHandle &&) = delete;
		IOHandle& operator=(IOHandle &&) = delete;
		IOHandle& operator=(const IOHandle &) = delete;
		int	m_fd;
		void close();
	public:
		virtual ~IOHandle();
		IOHandle(int fd = -1);
		int get_fd() const;
	};

	/// Copyable file descriptor.
	class Fd {
		static NamedPtr<IOHandle, int> s_named_ptr;
		shared_ptr<IOHandle> m_handle;
	public:
		using resource_type = IOHandle;
		Fd();
		Fd(int fd);
		Fd &operator=(int fd);
		Fd &operator=(const shared_ptr<IOHandle> &);
		operator int() const;
		bool operator!() const;
		shared_ptr<IOHandle> operator->() const;
	};

	void set_relative_path(string path);
	string relative_path();

	// Functions named "foo_or_die" throw exceptions on failure.

	/// Attempt to open the file with the given mode, throw exception on failure.
	int open_or_die(const string &file, int flags = O_RDONLY, mode_t mode = 0777);
	/// Attempt to open the file with the given mode, throw exception on failure.
	int openat_or_die(int dir_fd, const string &file, int flags = O_RDONLY, mode_t mode = 0777);

	/// Decode open flags
	string o_flags_ntoa(int flags);
	/// Decode open mode
	string o_mode_ntoa(mode_t mode);

	/// mmap with its one weird error case
	void *mmap_or_die(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
	/// Decode mmap prot
	string mmap_prot_ntoa(int prot);
	/// Decode mmap flags
	string mmap_flags_ntoa(int flags);

	/// Rename, throw exception on failure.
	void rename_or_die(const string &from, const string &to);
	/// Rename, throw exception on failure.
	void renameat_or_die(int fromfd, const string &frompath, int tofd, const string &topath);

	/// Truncate, throw exception on failure.
	void ftruncate_or_die(int fd, off_t size);

	// Read or write structs:
	// There is a template specialization to read or write strings
	// Three-arg version of read_or_die/write_or_die throws an error on incomplete read/writes
	// Four-arg version returns number of bytes read/written through reference arg

	/// Attempt read by pointer and length, throw exception on IO error or short read.
	void read_or_die(int fd, void *buf, size_t size);
	/// Attempt read of a POD struct, throw exception on IO error or short read.
	template <class T> void read_or_die(int fd, T& buf)
	{
		return read_or_die(fd, static_cast<void *>(&buf), sizeof(buf));
	}

	/// Attempt read by pointer and length, throw exception on IO error but not short read.
	void read_partial_or_die(int fd, void *buf, size_t size_wanted, size_t &size_read);
	/// Attempt read of a POD struct, throw exception on IO error but not short read.
	template <class T> void read_partial_or_die(int fd, T& buf, size_t &size_read)
	{
		return read_partial_or_die(fd, static_cast<void *>(&buf), sizeof(buf), size_read);
	}

	/// Attempt read at position by pointer and length, throw exception on IO error but not short read.
	void pread_or_die(int fd, void *buf, size_t size, off_t offset);
	/// Attempt read at position of a POD struct, throw exception on IO error but not short read.
	template <class T> void pread_or_die(int fd, T& buf, off_t offset)
	{
		return pread_or_die(fd, static_cast<void *>(&buf), sizeof(buf), offset);
	}

	/// Attempt write by pointer and length, throw exception on IO error or short write.
	void write_or_die(int fd, const void *buf, size_t size);
	/// Attempt write of a POD struct, throw exception on IO error or short write.
	template <class T> void write_or_die(int fd, const T& buf)
	{
		return write_or_die(fd, static_cast<const void *>(&buf), sizeof(buf));
	}

	/// Attempt write by pointer and length, throw exception on IO error but not short write.
	void write_partial_or_die(int fd, const void *buf, size_t size_wanted, size_t &size_written);
	/// Attempt write of a POD struct, throw exception on IO error but not short write.
	template <class T> void write_partial_or_die(int fd, const T& buf, size_t &size_written)
	{
		return write_partial_or_die(fd, static_cast<const void *>(&buf), sizeof(buf), size_written);
	}

	/// Attempt write at position by pointer and length, throw exception on IO error or short write.
	void pwrite_or_die(int fd, const void *buf, size_t size, off_t offset);
	/// Attempt write at position of a POD struct, throw exception on IO error or short write.
	template <class T> void pwrite_or_die(int fd, const T& buf, off_t offset)
	{
		return pwrite_or_die(fd, static_cast<const void *>(&buf), sizeof(buf), offset);
	}

	// Specialization for strings which reads/writes the string content, not the struct string
	template<> void write_or_die<string>(int fd, const string& str);
	template<> void pread_or_die<string>(int fd, string& str, off_t offset);
	template<> void pwrite_or_die<string>(int fd, const string& str, off_t offset);
	template<> void pread_or_die<ByteVector>(int fd, ByteVector& str, off_t offset);
	template<> void pwrite_or_die<ByteVector>(int fd, const ByteVector& str, off_t offset);
	// Deprecated
	template<> void pread_or_die<vector<uint8_t>>(int fd, vector<uint8_t>& str, off_t offset) = delete;
	template<> void pwrite_or_die<vector<uint8_t>>(int fd, const vector<uint8_t>& str, off_t offset) = delete;
	template<> void pread_or_die<vector<char>>(int fd, vector<char>& str, off_t offset) = delete;
	template<> void pwrite_or_die<vector<char>>(int fd, const vector<char>& str, off_t offset) = delete;

	/// Read a simple string.
	string read_string(int fd, size_t size);

	/// RAII wrapper for struct stat.  Constructor calls fstat() or stat()
	/// and throws on error, eliminating the init/fill/cleanup pattern.
	struct Stat : public stat {
		/// Default-construct an empty Stat (all fields zero).
		Stat();
		/// Construct by calling fstat() on @p f; throws on error.
		Stat(int f);
		/// Construct by calling stat() on @p filename; throws on error.
		Stat(const string &filename);
		/// Fill from fstat() on @p fd; throws on error.
		Stat &fstat(int fd);
		/// Fill from lstat() on @p filename (does not follow symlinks); throws on error.
		Stat &lstat(const string &filename);
	};

	/// Get inode flags (FS_IOC_GETFLAGS); throws on error.
	int ioctl_iflags_get(int fd);
	/// Set inode flags (FS_IOC_SETFLAGS); throws on error.
	void ioctl_iflags_set(int fd, int attr);

	/// Decode a stat mode bitmask to a human-readable string (e.g. "rwxr-xr-x").
	string st_mode_ntoa(mode_t mode);

	/// Because it's not trivial to do correctly
	string readlink_or_die(const string &path);

	/// Determine the name of a FD by readlink through /proc/self/fd/
	string name_fd(int fd);

	/// Returns Fd objects because it does own them.
	pair<Fd, Fd> socketpair_or_die(int domain = AF_UNIX, int type = SOCK_STREAM, int protocol = 0);

	/// RAII advisory lock (flock) guard, analogous to unique_lock for mutexes.
	/// Note that flock and mutex semantics differ significantly: flock locks are
	/// per open-file-description, not per-thread, and are not recursive.
	class Flock {
		int	m_fd;
		bool	m_locked;
		Flock(const Flock &) = delete;
		Flock(Flock &&) = delete;
		Flock &operator=(const Flock &) = delete;
		Flock &operator=(Flock &&) = delete;
	public:
		/// Construct an unowned Flock not associated with any fd.
		Flock();
		/// Construct and acquire an exclusive flock on @p fd; blocks until available.
		Flock(int fd);
		/// Construct a Flock on @p fd with an explicitly specified initial lock state.
		Flock(int fd, bool init_locked_state);
		/// Release the lock if held, then destroy.
		~Flock();
		/// Acquire an exclusive flock; blocks until available.
		void lock();
		/// Attempt to acquire an exclusive flock; throws if not immediately available.
		void try_lock();
		/// Release the flock.
		void unlock();
		/// Return true if the lock is currently held.
		bool owns_lock();
		/// Return true if the lock is currently held.
		operator bool();
		/// Return the associated file descriptor.
		int fd();
	};

	/// Doesn't use Fd objects because it's usually just used to replace stdin/stdout/stderr.
	void dup2_or_die(int fd_in, int fd_out);

}

#endif // CRUCIBLE_FD_H
