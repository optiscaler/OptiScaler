# Set ini keys inside their own section.
#
# Written because a global regex on "^Enabled=auto" turned on five features at once -- output
# scaling, sharpening, the magnifier and two others -- while trying to enable one. The ini has six
# keys called Enabled and they belong to different sections, so any edit that does not know which
# section it is in will hit all of them.

import io
import sys

PATH = r'D:\Games\cyberpunk-2077-optiscaler\bin\x64\OptiScaler.ini'

# (section, key, value). A section of None means the key is matched anywhere, for keys that are unique.
WANTED = [
    ('DlssNr', 'Enabled', 'true'),
    ('DlssNr', 'ProxyProbe', 'true'),
    ('Log', 'LogToFile', 'true'),
    ('Log', 'LogLevel', '2'),
    ('Magnifier', 'Enabled', 'false'),
    ('OutputScaling', 'Enabled', 'auto'),
    ('CAS', 'Enabled', 'auto'),
]


def main():
    lines = io.open(PATH, encoding='utf-8', errors='replace').read().splitlines(True)
    section = None
    applied = []

    for i, line in enumerate(lines):
        stripped = line.strip()

        if stripped.startswith('[') and stripped.endswith(']'):
            section = stripped[1:-1]
            continue

        if '=' not in stripped or stripped.startswith(';'):
            continue

        key = stripped.split('=', 1)[0].strip()

        for wantSection, wantKey, wantValue in WANTED:
            if section == wantSection and key == wantKey:
                ending = '\r\n' if line.endswith('\r\n') else '\n'
                lines[i] = '%s=%s%s' % (key, wantValue, ending)
                applied.append('[%s] %s=%s' % (section, key, wantValue))

    io.open(PATH, 'w', encoding='utf-8').write(''.join(lines))

    for a in applied:
        print(' ', a)

    missing = [w for w in WANTED if not any(('[%s] %s=' % (w[0], w[1])) in a for a in applied)]

    for m in missing:
        print('  NOT FOUND: [%s] %s' % (m[0], m[1]))


main()
