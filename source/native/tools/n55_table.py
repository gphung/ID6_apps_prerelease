"""The N55 BoostDeck metric table, read from src/n55_metrics.c so the art and layout tools and the
native code share one definition."""
import re
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / 'src' / 'n55_metrics.c'
STEPS = 32
PSI_PER_BAR = 14.5037738
ACRONYMS = {'AFR', 'HPFP', 'LPFP', 'RPM'}
# OPTIONS > Tiles: positions are column p // 2 and row p % 2, as in BoostDeck.xml.
POSITIONS = tuple(('Top %d' if p % 2 == 0 else 'Bottom %d') % (p // 2 + 1) for p in range(8))
# Order of the metric lists: related values together.
MENU = ('boost', 'boost_target', 'afr', 'afr_target', 'fuel_trim', 'throttle', 'load', 'air_mass', 'torque',
        'rpm', 'speed', 'timing', 'timing1', 'timing2', 'timing3', 'timing4', 'timing5', 'timing6', 'knock',
        'misfire1', 'misfire2', 'misfire3', 'misfire4', 'misfire5', 'misfire6', 'wastegate', 'oil_temp',
        'oil_pressure', 'coolant', 'iat', 'hpfp', 'hpfp_target', 'lpfp', 'exhaust', 'fuel_temp', 'ambient',
        'battery', 'battery_amps', 'battery_pct', 'start_margin', 'battery_temp')
# IDs shared with plugin.cpp.
CAPTION_FIRST, CAPTION_BLANK = 57000, 57063
ZERO_FIRST, ZERO_SET = 58000, 600          # + set * ZERO_SET + palette * 33 + step
PALETTES = 16                              # 8 colours, then the 8 Multicolor slot colours
PICK_FIRST, PICK_BLANK = 1000, 63          # raAction PICK_FIRST + position * 64 + metric (63: blank)


def _value(text):
    text = text.strip()
    m = re.fullmatch(r'PSI\((.+)\)', text)
    if m:
        return float(eval(m.group(1), {})) / PSI_PER_BAR
    m = re.fullmatch(r'F\((.+)\)', text)
    if m:
        return (float(m.group(1)) - 32) / 1.8
    return float(eval(text, {}))


def metrics():
    """[(key, caption, bar_style, lo, hi)] in table order (the metric index)."""
    body = SOURCE.read_text()
    body = body[body.index('const nm_metric nm_metrics[NM_COUNT] = {'):]
    body = body[:body.index('};')]
    out = []
    for line in re.findall(r'^\s*M\((.*)\),\s*$', body, re.M):
        args = [a.strip() for a in re.split(r',(?![^(]*\))', line)]
        key, caption = args[0].strip('"'), args[1].strip('"')
        out.append((key, caption, args[7], _value(args[12]), _value(args[13])))
    if len(out) != len(MENU) or sorted(k for k, *_ in out) != sorted(MENU):
        raise SystemExit('MENU and the n55_metrics.c table differ')
    return out


def zero_step(lo, hi):
    return int((0 - lo) / (hi - lo) * STEPS + .5)


def zero_sets():
    """The distinct zero-mark steps of the NB_ZERO bars, ascending: the zero bar set index."""
    return sorted({zero_step(lo, hi) for _, _, bar, lo, hi in metrics() if bar == 'NB_ZERO'})


def display_name(caption):
    """BOOST TARGET -> Boost Target; HPFP TARGET -> HPFP Target."""
    return ' '.join(w if w in ACRONYMS or not w.isalpha() else w.capitalize() for w in caption.split(' '))
