#!/usr/bin/env python3
"""Check the independent point-source cylinder reference: pass its executable path."""
import json
import math
import subprocess
import sys


def run(**options):
    command = [sys.argv[1], 'Mode=cylinder_point', 'Photons=100000']
    command += [f'{key}={value}' for key, value in options.items()]
    result = json.loads(subprocess.check_output(command, text=True))
    for channel in result['channels']:
        ledger = channel['ledger']
        assert channel['closure_count'] == result['photons_per_channel']
        assert sum(item['count'] for item in ledger.values()) == channel['closure_count']
        assert sum(channel['exit_counts']) == sum(ledger[key]['count'] for key in ('ballistic', 'single', 'multiple'))
        assert math.isclose(sum(item['flux_lm'] for item in ledger.values()), 12, abs_tol=1e-9)
        observed = channel['intercepted_count'] / result['photons_per_channel']
        assert abs(observed - result['analytic_intercept_fraction']) < 5 * channel['intercept_fraction_se']
        previous = channel['exit_counts']
        for cone in channel['normal_cones']:
            assert len(cone['counts']) == len(previous)
            assert all(0 <= count <= outer for count, outer in zip(cone['counts'], previous))
            for count, flux, error in zip(cone['counts'], cone['flux_lm'], cone['flux_se_lm']):
                fraction = count / result['photons_per_channel']
                assert math.isclose(flux, 12 * fraction, abs_tol=1e-9)
                assert math.isclose(error, 12 * math.sqrt(fraction * (1 - fraction) / result['photons_per_channel']), abs_tol=1e-9)
            assert math.isclose(cone['zero_count_flux_upper95_lm'], 12 * -math.expm1(math.log(.05) / result['photons_per_channel']), abs_tol=1e-12)
            previous = cone['counts']
    return result


one = run(Threads=1)
two = run(Threads=2)
one.pop('threads')
two.pop('threads')
assert one == two, 'photon streams must be independent of thread scheduling'
limited = run(Threads=2, MaxSteps=1)
assert all(c['ledger']['unresolved']['count'] > 0 for c in limited['channels'])
transparent = run(Threads=2, Scale=1e6)
assert all(c['ledger']['ballistic']['count'] > 0.99 * (c['intercepted_count'] - c['ledger']['entry_reflected']['count']) for c in transparent['channels'])
print('PASS: closure, all-exit profiles, analytic interception, thread invariance, residual ledger, ballistic limit, nested angular cones and uncertainty')
