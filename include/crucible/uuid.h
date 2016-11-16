#ifndef CRUCIBLE_UUID_H
#define CRUCIBLE_UUID_H

#include <string>

#include <uuid/uuid.h>

namespace crucible {
	using namespace std;

	string uuid_unparse(const unsigned char a[16]);
}

#endif // CRUCIBLE_UUID_H
