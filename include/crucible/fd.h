#ifndef CRUCIBLE_FD_H
#define CRUCIBLE_FD_H

#include "crucible/resource.h"

#include <cstring>

#include <string>
#include <vector>

// open
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// socket
#include <sys/socket.h>

// pread/pwrite
#include <unistd.h>

namespace crucible {
	using namespace std;

	// IOHandle is a file descriptor owner object.  It closes them when destroyed.
	// Most of the functions here don't use it because these functions don't own FDs.
	// All good names for such objects are taken.
	class IOHandle {
		IOHandle(const IOHandle &) = delete;
		IOHandle(IOHandle &&) = delete;
		IOHandle& operator=(IOHandle &&) = delete;
		IOHandle& operator=(const IOHandle &) = delete;
	protected:
		int	m_fd;
		IOHandle& operator=(int that) { m_fd = that; return *this; }
	public:
		virtual ~IOHandle();
		IOHandle(int fd);
		IOHandle();

		void close();
		int get_fd() const { return m_fd; }
		int release_fd();
	};

        template <>
        struct ResourceTraits<int, IOHandle> {
                int get_key(const IOHandle &res) const { return res.get_fd(); }
                shared_ptr<IOHandle> make_resource(int fd) const { return make_shared<IOHandle>(fd); }
                bool is_null_key(const int &key) const { return key < 0; }
                int get_null_key() const { return -1; }
        };

        typedef ResourceHandle<int, IOHandle> Fd;

	// Functions named "foo_or_die" throw exceptions on failure.

	// Attempt to open the file with the given mode
	int open_or_die(const string &file, int flags = O_RDONLY, mode_t mode = 0777);
	int openat_or_die(int dir_fd, const string &file, int flags = O_RDONLY, mode_t mode = 0777);

	// Decode open parameters
	string o_flags_ntoa(int flags);
	string o_mode_ntoa(mode_t mode);

	// mmap with its one weird error case
	void *mmap_or_die(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
	// Decode mmap parameters
	string mmap_prot_ntoa(int prot);
	string mmap_flags_ntoa(int flags);

	// Unlink, rename
	void unlink_or_die(const string &file);
	void rename_or_die(const string &from, const string &to);
	void renameat_or_die(int fromfd, const string &frompath, int tofd, const string &topath);

	// Read or write structs:
	// There is a template specialization to read or write strings
	// Three-arg version of read_or_die/write_or_die throws an error on incomplete read/writes
	// Four-arg version returns number of bytes read/written through reference arg

	void read_or_die(int fd, void *buf, size_t size);
	template <class T> void read_or_die(int fd, T& buf)
	{
		return read_or_die(fd, static_cast<void *>(&buf), sizeof(buf));
	}

	void read_partial_or_die(int fd, void *buf, size_t size_wanted, size_t &size_read);
	template <class T> void read_partial_or_die(int fd, T& buf, size_t &size_read)
	{
		return read_partial_or_die(fd, static_cast<void *>(&buf), sizeof(buf), size_read);
	}

	void pread_or_die(int fd, void *buf, size_t size, off_t offset);
	template <class T> void pread_or_die(int fd, T& buf, off_t offset)
	{
		return pread_or_die(fd, static_cast<void *>(&buf), sizeof(buf), offset);
	}

	void write_or_die(int fd, const void *buf, size_t size);
	template <class T> void write_or_die(int fd, const T& buf)
	{
		return write_or_die(fd, static_cast<const void *>(&buf), sizeof(buf));
	}

	void write_partial_or_die(int fd, const void *buf, size_t size_wanted, size_t &size_written);
	template <class T> void write_partial_or_die(int fd, const T& buf, size_t &size_written)
	{
		return write_partial_or_die(fd, static_cast<const void *>(&buf), sizeof(buf), size_written);
	}

	void pwrite_or_die(int fd, const void *buf, size_t size, off_t offset);
	template <class T> void pwrite_or_die(int fd, const T& buf, off_t offset)
	{
		return pwrite_or_die(fd, static_cast<const void *>(&buf), sizeof(buf), offset);
	}

	// Specialization for strings which reads/writes the string content, not the struct string
	template<> void write_or_die<string>(int fd, const string& str);
	template<> void pread_or_die<string>(int fd, string& str, off_t offset);
	template<> void pread_or_die<vector<char>>(int fd, vector<char>& str, off_t offset);
	template<> void pread_or_die<vector<uint8_t>>(int fd, vector<uint8_t>& str, off_t offset);
	template<> void pwrite_or_die<string>(int fd, const string& str, off_t offset);
	template<> void pwrite_or_die<vector<char>>(int fd, const vector<char>& str, off_t offset);
	template<> void pwrite_or_die<vector<uint8_t>>(int fd, const vector<uint8_t>& str, off_t offset);

	// A different approach to reading a simple string
	string read_string(int fd, size_t size);

	// A lot of Unix API wants you to initialize a struct and call
	// one function to fill it, another function to throw it away,
	// and has some unknown third thing you have to do when there's
	// an error.  That's also a C++ object with an exception-throwing
	// constructor.
	struct Stat : public stat {
		Stat();
		Stat(int f);
		Stat(const string &filename);
		Stat &fstat(int fd);
		Stat &lstat(const string &filename);
	};

	string st_mode_ntoa(mode_t mode);

	// Because it's not trivial to do correctly
	string readlink_or_die(const string &path);

	// Determine the name of a FD by readlink through /proc/self/fd/
	string name_fd(int fd);

	// Returns Fd objects because it does own them.
	pair<Fd, Fd> socketpair_or_die(int domain = AF_UNIX, int type = SOCK_STREAM, int protocol = 0);

	// like unique_lock but for flock instead of mutexes...and not trying
	// to hide the many and subtle differences between those two things *at all*.
	class Flock {
		int	m_fd;
		bool	m_locked;
		Flock(const Flock &) = delete;
		Flock(Flock &&) = delete;
		Flock &operator=(const Flock &) = delete;
		Flock &operator=(Flock &&) = delete;
	public:
		Flock();
		Flock(int fd);
		Flock(int fd, bool init_locked_state);
		~Flock();
		void lock();
		void try_lock();
		void unlock();
		bool owns_lock();
		operator bool();
		int fd();
	};

	// Doesn't use Fd objects because it's usually just used to replace stdin/stdout/stderr.
	void dup2_or_die(int fd_in, int fd_out);

}

#endif // CRUCIBLE_FD_H
