"""Print the object-format unit id (low 16 bits of FNV-1a) for each name given.

    unitid.py hello.c crt0.s
"""
import sys


def unit_id(name):
    h = 0x811C9DC5
    for b in name.encode():
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return "%04x" % (h & 0xFFFF)


if __name__ == "__main__":
    for n in sys.argv[1:]:
        print(n, unit_id(n))
