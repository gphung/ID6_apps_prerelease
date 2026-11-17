"""Write the F87 M2 N55 BoostDeck.xml from the reference layout.

The N55 profile keeps temperatures and pressures as separate, saved unit choices
(BoostDeck/units.txt; defaults Celsius and psi), so OPTIONS offers four buttons in place of
Units: Imperial / Metric. Everything else is byte-identical to the reference layout.

Usage: python3 make_n55_layout.py reference/BoostDeck.xml out/BoostDeck.xml
"""
import sys
from xml.etree import ElementTree

ACTIONS_OLD = '      <raAction id="59" />\n'
ACTIONS_NEW = ACTIONS_OLD + ''.join('      <raAction id="%d" />\n' % a for a in (70, 71, 72, 73))
BUTTONS_OLD = ('          <button id="410" action="49" text="Units: Imperial" />\n'
               '          <button id="411" action="59" text="Units: Metric" />\n')
BUTTONS_NEW = ('          <button id="410" action="70" text="Temperature: Celsius" />\n'
               '          <button id="411" action="71" text="Temperature: Fahrenheit" />\n'
               '          <button id="412" action="72" text="Pressure: psi" />\n'
               '          <button id="413" action="73" text="Pressure: bar" />\n')


def main(source, out):
    text = open(source, encoding='utf-8').read()
    for old in (ACTIONS_OLD, BUTTONS_OLD):
        if text.count(old) != 1:
            raise SystemExit('reference layout changed: %r' % old.strip())
    if any(' id="%d"' % i in text for i in (412, 413)) or any('action="%d"' % a in text for a in (70, 71, 72, 73)):
        raise SystemExit('component or action ID already used')
    text = text.replace(ACTIONS_OLD, ACTIONS_NEW).replace(BUTTONS_OLD, BUTTONS_NEW)
    ElementTree.fromstring(text.encode('utf-8'))  # still well-formed
    with open(out, 'w', encoding='utf-8') as f:
        f.write(text)


if __name__ == '__main__':
    main(*sys.argv[1:])
