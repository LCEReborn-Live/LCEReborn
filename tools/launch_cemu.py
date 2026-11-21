#!/usr/bin/env python3
"""Launch the prepared Wii U bundle with the CA needed by LCE Live in Cemu."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import sys


TOOLS = Path(__file__).resolve().parent
ROOT_CA = TOOLS / 'gamedata' / 'isrg-root-x1.pem'
ROOT_CA_SHA256 = '96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6'


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cemu', nargs='?', default='Cemu.exe' if os.name == 'nt' else 'cemu',
                        help='path to the Cemu executable, or its name on PATH')
    parser.add_argument('--bundle', type=Path, default=TOOLS.parent / 'build' / 'LCEReborn.wuhb',
                        help='prepared Wii U bundle (default: build/LCEReborn.wuhb)')
    args = parser.parse_args(argv)
    executable = shutil.which(args.cemu)
    if not executable:
        parser.error('Cemu was not found. Pass the path to the emulator as the first argument.')
    executable = Path(executable).resolve()
    bundle = args.bundle.resolve()
    if not bundle.is_file():
        parser.error(f'{bundle} was not found. Run tools/package.py first.')

    try:
        certificate = ssl.PEM_cert_to_DER_cert(ROOT_CA.read_text(encoding='ascii'))
        if hashlib.sha256(certificate).hexdigest() != ROOT_CA_SHA256:
            raise ValueError('the bundled ISRG Root X1 certificate fingerprint does not match')
        env = os.environ.copy()
        env['SSL_CERT_FILE'] = str(ROOT_CA)
        return subprocess.call([str(executable), '-g', str(bundle)], cwd=executable.parent, env=env)
    except (OSError, ValueError) as error:
        print(f'Could not launch Cemu: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
