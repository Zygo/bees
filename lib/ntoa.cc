#include "crucible/ntoa.h"

#include <cassert>
#include <sstream>
#include <string>

namespace crucible {
	using namespace std;

	string bits_ntoa(unsigned long long n, const bits_ntoa_table *table)
	{
		string out;
		while (n && table->a) {
			// No bits in n outside of mask
			assert( ((~table->mask) & table->n) == 0);
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
			ostringstream oss;
			oss << "0x" << hex << n;
			if (!out.empty()) {
				out += "|";
			}
			out += oss.str();
		}
		if (out.empty()) {
			out = "0";
		}
		return out;
	}


};
