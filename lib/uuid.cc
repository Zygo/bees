#include "crucible/uuid.h"

namespace crucible {
	using namespace std;

	const size_t uuid_unparsed_size = 37; // "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx\0"

	string
	uuid_unparse(const unsigned char in[16])
	{
		char out[uuid_unparsed_size];
		::uuid_unparse(in, out);
		return string(out);
	}

}
