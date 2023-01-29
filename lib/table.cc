#include "crucible/table.h"

#include "crucible/string.h"

namespace crucible {
	namespace Table {
		using namespace std;

		Content
		Fill(const char c)
		{
			return [=](size_t width, size_t height) -> string {
				string rv;
				while (height--) {
					rv += string(width, c);
					if (height) {
						rv += "\n";
					}
				}
				return rv;
			};
		}

		Content
		Text(const string &s)
		{
			return [=](size_t width, size_t height) -> string {
				const auto lines = split("\n", s);
				string rv;
				size_t line_count = 0;
				for (const auto &i : lines) {
					if (line_count++) {
						rv += "\n";
					}
					if (i.length() < width) {
						rv += string(width - i.length(), ' ');
					}
					rv += i;
				}
				while (line_count < height) {
					if (line_count++) {
						rv += "\n";
					}
					rv += string(width, ' ');
				}
				return rv;
			};
		}

		Content
		Number(const string &s)
		{
			return [=](size_t width, size_t height) -> string {
				const auto lines = split("\n", s);
				string rv;
				size_t line_count = 0;
				for (const auto &i : lines) {
					if (line_count++) {
						rv += "\n";
					}
					if (i.length() < width) {
						rv += string(width - i.length(), ' ');
					}
					rv += i;
				}
				while (line_count < height) {
					if (line_count++) {
						rv += "\n";
					}
					rv += string(width, ' ');
				}
				return rv;
			};
		}

		Cell::Cell(const Content &fn) :
			m_content(fn)
		{
		}

		Cell&
		Cell::operator=(const Content &fn)
		{
			m_content = fn;
			return *this;
		}

		string
		Cell::text(size_t width, size_t height) const
		{
			return m_content(width, height);
		}

		size_t
		Dimension::size() const
		{
			return m_elements.size();
		}

		size_t
		Dimension::insert(size_t pos)
		{
			++m_next_pos;
			const auto insert_pos = min(m_elements.size(), pos);
			const auto it = m_elements.begin() + insert_pos;
			m_elements.insert(it, m_next_pos);
			return insert_pos;
		}

		void
		Dimension::erase(size_t pos)
		{
			const auto it = m_elements.begin() + min(m_elements.size(), pos);
			m_elements.erase(it);
		}

		size_t
		Dimension::at(size_t pos) const
		{
			return m_elements.at(pos);
		}

		Dimension&
		Table::rows()
		{
			return m_rows;
		};

		const Dimension&
		Table::rows() const
		{
			return m_rows;
		};

		Dimension&
		Table::cols()
		{
			return m_cols;
		};

		const Dimension&
		Table::cols() const
		{
			return m_cols;
		};

		const Cell&
		Table::at(size_t row, size_t col) const
		{
			const auto row_idx = m_rows.at(row);
			const auto col_idx = m_cols.at(col);
			const auto found = m_cells.find(make_pair(row_idx, col_idx));
			if (found == m_cells.end()) {
				static const Cell s_empty(Fill('.'));
				return s_empty;
			}
			return found->second;
		};

		Cell&
		Table::at(size_t row, size_t col)
		{
			const auto row_idx = m_rows.at(row);
			const auto col_idx = m_cols.at(col);
			return m_cells[make_pair(row_idx, col_idx)];
		};

		static
		pair<size_t, size_t>
		text_size(const string &s)
		{
			const auto s_split = split("\n", s);
			size_t width = 0;
			for (const auto &i : s_split) {
				width = max(width, i.length());
			}
			return make_pair(width, s_split.size());
		}

		ostream& operator<<(ostream &os, const Table &table)
		{
			const auto rows = table.rows().size();
			const auto cols = table.cols().size();
			vector<size_t> row_heights(rows, 1);
			vector<size_t> col_widths(cols, 1);
			// Get the size of all fixed- and minimum-sized content cells
			for (size_t row = 0; row < table.rows().size(); ++row) {
				vector<string> col_text;
				for (size_t col = 0; col < table.cols().size(); ++col) {
					col_text.push_back(table.at(row, col).text(0, 0));
					const auto tsize = text_size(*col_text.rbegin());
					row_heights[row] = max(row_heights[row], tsize.second);
					col_widths[col] = max(col_widths[col], tsize.first);
				}
			}
			// Render the table
			for (size_t row = 0; row < table.rows().size(); ++row) {
				vector<string> lines(row_heights[row], "");
				for (size_t col = 0; col < table.cols().size(); ++col) {
					const auto& table_cell = table.at(row, col);
					const auto table_text = table_cell.text(col_widths[col], row_heights[row]);
					auto col_lines = split("\n", table_text);
					col_lines.resize(row_heights[row], "");
					for (size_t line = 0; line < row_heights[row]; ++line) {
						if (col > 0) {
							lines[line] += table.mid();
						}
						lines[line] += col_lines[line];
					}
				}
				for (const auto &line : lines) {
					os << table.left() << line << table.right() << "\n";
				}
			}
			return os;
		}

		void
		Table::left(const string &s)
		{
			m_left = s;
		}

		void
		Table::mid(const string &s)
		{
			m_mid = s;
		}

		void
		Table::right(const string &s)
		{
			m_right = s;
		}

		const string&
		Table::left() const
		{
			return m_left;
		}

		const string&
		Table::mid() const
		{
			return m_mid;
		}

		const string&
		Table::right() const
		{
			return m_right;
		}
	}
}
