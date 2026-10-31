"""Parallel Beer integrals: [1,2] gives (1,5/8); [1,3/2] gives (1/2,9/32)."""
from array import array
from math import exp
from pathlib import Path
import re
import sys


def pixels(stem):
    raw = Path(stem + '.ppm').read_bytes()
    header = re.match(rb'P6\s+(?:#[^\n]*\n\s*)*(\d+)\s+(\d+)\s+65535\s', raw)
    assert header is not None, stem + ': expected linear 16-bit PPM'
    assert tuple(map(int, header.groups())) == (12, 12), stem + ': wrong dimensions'
    values = array('H')
    values.frombytes(raw[header.end():])
    if sys.byteorder == 'little':
        values.byteswap()
    assert len(values) == 12 * 12 * 3, stem + ': incomplete raster'
    assert min(values) > 128, stem + ': dark pixels make attenuation vacuous'
    assert max(values) < 65000, stem + ': clipping prevents attenuation comparison'
    log = Path(stem + '.log').read_text()
    stored = re.search(r'Surface photons stored:\s*(\d+)', log)
    assert stored and int(stored[1]) > 0, stem + ': no surface photon evidence'
    return values


for method, source, integrals in ((1, '', (1.0, 0.625)),
                                   (2, '', (1.0, 0.625)),
                                   (2, 'inside_', (0.5, 0.28125))):
    prefix = f'media_prepared_photons_{method}_{source}'
    clear = pixels(prefix + 'clear')
    for case, integral in zip(('constant', 'varying'), integrals):
        measured = pixels(prefix + case)
        for channel, sigma in enumerate((0.2, 0.4, 0.6)):
            expected = exp(-sigma * integral)
            ratios = [b / a for a, b in zip(clear[channel::3], measured[channel::3])]
            # Fixed 3% flux budget covers photon-map quantization and 16-bit output.
            error = max(abs(ratio - expected) for ratio in ratios)
            assert error < 0.03, (method, source, case, channel, expected, min(ratios), max(ratios))
            print(f'method {method} {source}{case} channel {channel}: expected {expected:.6f}, '
                  f'measured {sum(ratios) / len(ratios):.6f}, max error {error:.6f}')
