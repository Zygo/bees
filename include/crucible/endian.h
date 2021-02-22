#ifndef CRUCIBLE_ENDIAN_H
#define CRUCIBLE_ENDIAN_H

#include <cstdint>

#include <endian.h>

namespace crucible {

	template<class T>
	struct le_to_cpu_helper {
		T operator()(const T v);
	};

	template<> struct le_to_cpu_helper<uint64_t> {
		uint64_t operator()(const uint64_t v) { return le64toh(v); }
	};

#if __SIZEOF_LONG__ == 8
	// uint64_t is unsigned long on LP64 platforms
	template<> struct le_to_cpu_helper<unsigned long long> {
		unsigned long long operator()(const unsigned long long v) { return le64toh(v); }
	};
#endif

	template<> struct le_to_cpu_helper<uint32_t> {
		uint32_t operator()(const uint32_t v) { return le32toh(v); }
	};

	template<> struct le_to_cpu_helper<uint16_t> {
		uint16_t operator()(const uint16_t v) { return le64toh(v); }
	};

	template<> struct le_to_cpu_helper<uint8_t> {
		uint8_t operator()(const uint8_t v) { return v; }
	};

	template<class T>
	T
	le_to_cpu(const T v)
	{
		return le_to_cpu_helper<T>()(v);
	}

	template<class T>
	T
	get_unaligned(const void *const p)
	{
		struct not_aligned {
			T v;
		} __attribute__((packed));
		const not_aligned *const nap = reinterpret_cast<const not_aligned*>(p);
		return nap->v;
	}

}

#endif // CRUCIBLE_ENDIAN_H
