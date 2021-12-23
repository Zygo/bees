import sys

c_str_escape = str.maketrans({'\n': '\\n', '"': '\\"', '\\': '\\\\'})

with open(sys.argv[1]) as input, open(sys.argv[2], 'w') as output:
    output.write('const char *BEES_USAGE = \n')
    for line in input:
        output.write('"{}"\n'.format(line.translate(c_str_escape)))
    output.write(';\n')
