#ifndef CRUCIBLE_TABLE_H
#define CRUCIBLE_TABLE_H

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace crucible {
	namespace Table {
		using namespace std;

		using Content = function<string(size_t width, size_t height)>;
		const size_t endpos = numeric_limits<size_t>::max();

		Content Fill(const char c);
		Content Text(const string& s);

		template <class T>
		Content Number(const T& num)
		{
			ostringstream oss;
			oss << num;
			return Text(oss.str());
		}

		class Cell {
			Content m_content;
		public:
			Cell(const Content &fn = [](size_t, size_t) { return string(); } );
			Cell& operator=(const Content &fn);
			string text(size_t width, size_t height) const;
		};

		class Dimension {
			size_t m_next_pos = 0;
			vector<size_t> m_elements;
		friend class Table;
			size_t at(size_t) const;
		public:
			size_t size() const;
			size_t insert(size_t pos);
			void erase(size_t pos);
		};

		class Table {
			Dimension m_rows, m_cols;
			map<pair<size_t, size_t>, Cell> m_cells;
			string m_left = "|";
			string m_mid = "|";
			string m_right = "|";
		public:
			Dimension &rows();
			const Dimension& rows() const;
			Dimension &cols();
			const Dimension& cols() const;
			Cell& at(size_t row, size_t col);
			const Cell& at(size_t row, size_t col) const;
			template <class T> void insert_row(size_t pos, const T& container);
			template <class T> void insert_col(size_t pos, const T& container);
			void left(const string &s);
			void mid(const string &s);
			void right(const string &s);
			const string& left() const;
			const string& mid() const;
			const string& right() const;
		};

		ostream& operator<<(ostream &os, const Table &table);

		template <class T>
		void
		Table::insert_row(size_t pos, const T& container)
		{
			const auto new_pos = m_rows.insert(pos);
			size_t col = 0;
			for (const auto &i : container) {
				if (col >= cols().size()) {
					cols().insert(col);
				}
				at(new_pos, col++) = i;
			}
		}

		template <class T>
		void
		Table::insert_col(size_t pos, const T& container)
		{
			const auto new_pos = m_cols.insert(pos);
			size_t row = 0;
			for (const auto &i : container) {
				if (row >= rows().size()) {
					rows().insert(row);
				}
				at(row++, new_pos) = i;
			}
		}

	}
}

#endif // CRUCIBLE_TABLE_H
