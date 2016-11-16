#ifndef CRUCIBLE_BENCODE_H
#define CRUCIBLE_BENCODE_H

#include "crucible/error.h"

#include <cctype>

#include <fstream>
#include <map>
#include <memory>
#include <iostream>
#include <string>
#include <vector>

namespace crucible {
	using namespace std;

	// So...much...forward declaration...
	struct bencode_variant;
	typedef shared_ptr<bencode_variant> bencode_variant_ptr;

	struct bencode_variant {
		virtual ~bencode_variant();
		virtual ostream& print(ostream &os, const string &parent = "") const = 0;
		virtual bencode_variant_ptr at(size_t i) const;
		virtual bencode_variant_ptr at(const string &s) const;
		virtual operator string() const;
	};

	ostream& operator<<(ostream &os, const bencode_variant_ptr &p);

	// i<base-10-ascii>e
	struct bencode_int : public bencode_variant {
		~bencode_int();
		bencode_int(int64_t i);
		ostream & print(ostream &os, const string &parent = "") const override;
	private:
		int64_t m_i;
	};

	// <length>:contents
	struct bencode_string : public bencode_variant {
		~bencode_string();
		bencode_string(string s);
		ostream & print(ostream &os, const string &parent = "") const override;
		operator string() const override;
	private:
		string m_s;
	};

	// l<contents>e
	struct bencode_list : public bencode_variant {
		~bencode_list();
		bencode_list(const vector<bencode_variant_ptr> &l);
		ostream & print(ostream &os, const string &parent = "") const override;
		using bencode_variant::at;
		bencode_variant_ptr at(size_t i) const override;
	private:
		vector<bencode_variant_ptr> m_l;
	};

	// d<contents>e (lexicographically sorted pairs of <key><value>, key is a string)
	struct bencode_dict : public bencode_variant {
		~bencode_dict();
		bencode_dict(const map<string, bencode_variant_ptr> &m);
		ostream& print(ostream &os, const string &parent = "") const override;
		using bencode_variant::at;
		bencode_variant_ptr at(const string &key) const override;
	private:
		map<string, bencode_variant_ptr> m_m;
	};

	bencode_variant_ptr bencode_decode_stream(istream &is);
};

#endif
