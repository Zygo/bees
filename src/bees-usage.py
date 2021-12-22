import sys

c_str_escape = str.maketrans({'\n': '\\n', '"': '\\"', '\\': '\\\\'})

print("const char *BEES_USAGE = ")
with open(sys.argv[1]) as file:
    for line in file:
        print('"{}"'.format(line.translate(c_str_escape)))
print(";")
