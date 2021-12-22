import sys

# Rewrite relative links in index file to remove 'docs/' prefix
with open(sys.argv[1]) as file:
    sys.stdout.write(file.read().replace('docs/', ''))
