"""Prepare the Live trust anchors for the Switch build."""
import hashlib
from pathlib import Path
import ssl
import sys


def main(output):
    output.mkdir(parents=True, exist_ok=True)
    roots = {
        'x1': ('wiiu_live_root_ca.inc', '96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6'),
        'x2': ('wiiu_live_root_x2_ca.inc', '69729b8e15a86efc177a57afb7171dfc64add28c2fca8cf1507e34453ccb1470'),
    }
    for name, (header, digest) in roots.items():
        pem = Path(__file__).with_name(f'isrg-root-{name}.pem')
        certificate = ssl.PEM_cert_to_DER_cert(pem.read_text(encoding='ascii'))
        if hashlib.sha256(certificate).hexdigest() != digest:
            raise SystemExit(f'ISRG Root {name.upper()} certificate fingerprint does not match')
        text = ''.join(', '.join(f'0x{byte:02x}' for byte in certificate[i:i + 12]) + ',\n'
                       for i in range(0, len(certificate), 12))
        target = output / header
        if not target.exists() or target.read_text(encoding='ascii') != text:
            target.write_text(text, encoding='ascii')


if __name__ == '__main__':
    main(Path(sys.argv[1]))
