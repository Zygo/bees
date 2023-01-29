#include "tests.h"

#include "crucible/table.h"

using namespace crucible;
using namespace std;

void
print_table(const Table::Table& t)
{
	cerr << "BEGIN TABLE\n";
	cerr << t;
	cerr << "END TABLE\n";
	cerr << endl;
}

void
test_table()
{
	Table::Table t;
	t.insert_row(Table::endpos, vector<Table::Content> {
		Table::Text("Hello, World!"),
		Table::Text("2"),
		Table::Text("3"),
		Table::Text("4"),
	});
	print_table(t);
	t.insert_row(Table::endpos, vector<Table::Content> {
		Table::Text("Greeting"),
		Table::Text("two"),
		Table::Text("three"),
		Table::Text("four"),
	});
	print_table(t);
	t.insert_row(Table::endpos, vector<Table::Content> {
		Table::Fill('-'),
		Table::Text("ii"),
		Table::Text("iii"),
		Table::Text("iv"),
	});
	print_table(t);
	t.mid(" | ");
	t.left("| ");
	t.right(" |");
	print_table(t);
	t.insert_col(1, vector<Table::Content> {
		Table::Text("1"),
		Table::Text("one"),
		Table::Text("i"),
		Table::Text("I"),
	});
	print_table(t);
	t.at(2, 1) = Table::Text("Two\nLines");
	print_table(t);
}

int
main(int, char**)
{
	RUN_A_TEST(test_table());

	exit(EXIT_SUCCESS);
}
