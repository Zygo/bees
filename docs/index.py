import sys

# Rewrite relative links in index file to remove 'docs/' prefix
with open(sys.argv[1]) as input, open(sys.argv[2], 'w') as output:
    output.write(input.read().replace('docs/', ''))
