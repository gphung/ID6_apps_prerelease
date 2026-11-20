"""Write the F87 M2 N55 BoostDeck.xml from the reference layout.

The N55 profile keeps temperatures and pressures as separate, saved unit choices
(BoostDeck/units.txt; defaults Celsius and psi), so OPTIONS offers four buttons in place of
Units: Imperial / Metric.

With --tiles it also makes the eight tiles configurable (BoostDeck/tiles.txt, n55_table):
each tile gets a caption image, OPTIONS gets Tiles, a screen with the eight positions laid out
like the tiles, and each position has a screen listing every metric. Picking a metric applies it
at once (swapping with a tile that already shows it). Everything else is unchanged.

Usage: python3 make_n55_layout.py reference/BoostDeck.xml out/BoostDeck.xml [--tiles]
"""
import sys
from pathlib import Path
from xml.etree import ElementTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
import n55_table as table  # noqa: E402

# IDs shared with plugin.cpp (kTiles*).
CAPTION_MODEL, BUTTON_MODEL, HEADER_MODEL, TILES_HEADER_MODEL = 120, 130, 140, 148
CAPTION_COMPONENT, TILES_LABEL, TILE_BUTTON, RESET_BUTTON = 530, 540, 550, 558
POSITION_LABEL, METRIC_BUTTON = 560, 600
TILES_STATE, POSITION_STATE = 310, 300
TILES_ACTION, POSITION_ACTION, RESET_ACTION = 174, 160, 170
# Caption images over the cleared captions (screen pixels: background at 24,13).
CAPTION_X, CAPTION_Y, CAPTION_W, CAPTION_H = (36, 294, 552, 810), (33, 241), 220, 30


def props(element, values):
    holder = ElementTree.SubElement(element, 'properties')
    for key, value in values:
        ElementTree.SubElement(holder, 'property', id=str(key), value=str(value))


def add_tiles(root):
    app = root.find('pluginApp')
    actions, models, states = app.find('actions'), app.find('models'), app.find('hmiStates')
    gauges = states.find("hmiState[@id='100']")
    used = {int(e.get('id')) for e in root.iter() if e.get('id') and e.tag != 'property'}
    metrics = table.metrics()
    keys = [k for k, *_ in metrics]
    new = []

    def element(parent, tag, **attributes):
        identifier = int(attributes['id'])
        if identifier in used:
            raise SystemExit('ID %d already used' % identifier)
        used.add(identifier)
        new.append(identifier)
        return ElementTree.SubElement(parent, tag, {k: str(v) for k, v in attributes.items()})

    element(actions, 'hmiAction', id=TILES_ACTION, target=TILES_STATE)
    element(actions, 'raAction', id=RESET_ACTION)
    for p in range(8):
        element(actions, 'hmiAction', id=POSITION_ACTION + p, target=POSITION_STATE + p)
        for m in list(range(len(metrics))) + [table.PICK_BLANK]:
            element(actions, 'raAction', id=table.PICK_FIRST + p * 64 + m)
    for p in range(8):
        element(models, 'imageIdModel', id=CAPTION_MODEL + p, imageId=table.CAPTION_BLANK)
        element(models, 'raDataModel', id=BUTTON_MODEL + p, modelType='Richtext')
        element(models, 'raDataModel', id=HEADER_MODEL + p, modelType='Richtext')
    element(models, 'raDataModel', id=TILES_HEADER_MODEL, modelType='Richtext')

    options = gauges.find('optionComponents')
    element(options, 'button', id=414, action=TILES_ACTION, text='Tiles')
    components = gauges.find('components')
    for p in range(8):
        image = element(components, 'image', id=CAPTION_COMPONENT + p, model=CAPTION_MODEL + p)
        props(image, ((20, CAPTION_X[p // 2]), (21, CAPTION_Y[p % 2]), (9, CAPTION_W), (10, CAPTION_H),
                      (2, 'false'), (3, 'true')))

    tiles = element(states, 'hmiState', id=TILES_STATE, parentId=100, text='Tiles')
    content = ElementTree.SubElement(tiles, 'components')
    label = element(content, 'label', id=TILES_LABEL, model=TILES_HEADER_MODEL)
    props(label, ((20, 58), (21, 20), (9, 1067), (10, 55)))
    for p in range(8):
        button = element(content, 'button', id=TILE_BUTTON + p, model=BUTTON_MODEL + p, action=POSITION_ACTION + p)
        props(button, ((20, 39 + 258 * (p // 2)), (21, 95 + 85 * (p % 2)), (9, 240), (10, 60)))
    reset = element(content, 'button', id=RESET_BUTTON, action=RESET_ACTION, text='Reset to default')
    props(reset, ((20, 39), (21, 290), (9, 360), (10, 60)))

    for p, name in enumerate(table.POSITIONS):
        state = element(states, 'hmiState', id=POSITION_STATE + p, parentId=TILES_STATE, text=name)
        content = ElementTree.SubElement(state, 'components')
        element(content, 'label', id=POSITION_LABEL + p, model=HEADER_MODEL + p)
        for k, key in enumerate(table.MENU):
            m = keys.index(key)
            element(content, 'button', id=METRIC_BUTTON + p * 48 + k, action=table.PICK_FIRST + p * 64 + m,
                    text=table.display_name(metrics[m][1]))
        element(content, 'button', id=METRIC_BUTTON + p * 48 + len(table.MENU),
                action=table.PICK_FIRST + p * 64 + table.PICK_BLANK, text='(blank)')
    return len(new)

ACTIONS_OLD = '      <raAction id="59" />\n'
ACTIONS_NEW = ACTIONS_OLD + ''.join('      <raAction id="%d" />\n' % a for a in (70, 71, 72, 73))
BUTTONS_OLD = ('          <button id="410" action="49" text="Units: Imperial" />\n'
               '          <button id="411" action="59" text="Units: Metric" />\n')
BUTTONS_NEW = ('          <button id="410" action="70" text="Temperature: Celsius" />\n'
               '          <button id="411" action="71" text="Temperature: Fahrenheit" />\n'
               '          <button id="412" action="72" text="Pressure: psi" />\n'
               '          <button id="413" action="73" text="Pressure: bar" />\n')


def main(source, out, *flags):
    if any(f != '--tiles' for f in flags):
        raise SystemExit(__doc__)
    text = open(source, encoding='utf-8').read()
    for old in (ACTIONS_OLD, BUTTONS_OLD):
        if text.count(old) != 1:
            raise SystemExit('reference layout changed: %r' % old.strip())
    if any(' id="%d"' % i in text for i in (412, 413)) or any('action="%d"' % a in text for a in (70, 71, 72, 73)):
        raise SystemExit('component or action ID already used')
    text = text.replace(ACTIONS_OLD, ACTIONS_NEW).replace(BUTTONS_OLD, BUTTONS_NEW)
    root = ElementTree.fromstring(text.encode('utf-8'))  # still well-formed
    if flags:
        add_tiles(root)
        ElementTree.indent(root, space='  ')
        text = "<?xml version='1.0' encoding='utf-8'?>\n" + ElementTree.tostring(root, encoding='unicode') + '\n'
    with open(out, 'w', encoding='utf-8') as f:
        f.write(text)


if __name__ == '__main__':
    main(*sys.argv[1:])
