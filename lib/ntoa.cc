#include "crucible/ntoa.h"

#include "crucible/error.h"
#include "crucible/string.h"

namespace crucible {
	using namespace std;

	string bits_ntoa(unsigned long long n, const bits_ntoa_table *table)
	{
		string out;
		while (n && table->a) {
			// No bits in n outside of mask
			THROW_CHECK2(invalid_argument, table->mask, table->n, ((~table->mask) & table->n) == 0);
			if ( (n & table->mask) == table->n) {
				if (!out.empty()) {
					out += "|";
				}
				out += table->a;
				n &= ~(table->mask);
			}
			++table;
		}
		if (n) {
			if (!out.empty()) {
				out += "|";
			}
			out += to_hex(n);
		}
		if (out.empty()) {
			out = "0";
		}
		return out;
	}


};
