"""Add the F87 M2 N55 BoostDeck background (56301) to a packaged BoostDeck image archive.

The N55 profile shows engine oil temperature in the Transmission slot, captioned OIL TEMP, and
turns the tall Active Ethanol tile into an ENGINE warm-up panel: the tall bar becomes an oil
thermometer and a strip beside it marks the warm-up bands (bd_oil_band). Captions are built
from glyphs already in the reference background (56300), so the font matches exactly:
"OIL " from OIL PRESSURE, T, M, N, G and I from TIMING, and E and P from PRESSURE.
56300 is left unchanged.

Usage: python3 make_n55_background.py BoostDeck_ALL_Images.zip
"""
import sys
from io import BytesIO
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image, ImageDraw

REFERENCE, N55 = '56300.png', '56301.png'
TILE = (23, 27, 34, 255)
CAPTION_ROWS = (20, 49)          # glyph rows of the first-row captions
TALL_CAPTION_SHIFT = -6          # the tall tile's caption sits 6 px higher

# Glyph columns in the reference background.
O_I_L = (791, 826)                                   # "OIL" in OIL PRESSURE
T, M, N, G, I = (274, 286), (296, 311), (322, 335), (339, 352), (289, 291)
E, P = (943, 954), (835, 847)
WORD_SPACE = 835 - 826 - 1                           # OIL -> PRESSURE

# Caption areas to clear, and where each caption starts.
TRANSMISSION_CAPTION, TRANSMISSION_X = (528, 712), 532
ETHANOL_CAPTION, ETHANOL_TILE = (1040, 1166), (1032, 1171)

# The tall bar (BoostDeck.xml component 208 at 1106,71 40x250, background at 24,13).
BAR_LEFT, BAR_TOP, BAR_WIDTH, BAR_HEIGHT = 1106 - 24, 71 - 13, 40, 250
BAR_MIN_F, BAR_MAX_F = 100.0, 300.0
BANDS = ((100, 180, '#58ceff'), (180, 194, '#ff4f1a'), (194, 266, '#6cdb93'), (266, 300, '#ff4545'))
STRIP_GAP, STRIP_WIDTH = 6, 4


def clear(image, columns, rows):
    ImageDraw.Draw(image).rectangle((columns[0], rows[0], columns[1] - 1, rows[1] - 1), fill=TILE)


def paste_word(image, reference, glyphs, spacing, x, y_shift=0):
    y0, y1 = CAPTION_ROWS
    for (a, b), space in zip(glyphs, spacing):
        glyph = reference.crop((a, y0, b + 1, y1))
        image.paste(glyph, (x, y0 + y_shift))
        x += glyph.width + space
    return x


def word_width(glyphs, spacing):
    return sum(b - a + 1 for a, b in glyphs) + sum(spacing[:-1])


def n55_background(reference):
    y0, y1 = CAPTION_ROWS
    image = reference.copy()
    # OIL TEMP in the Transmission tile.
    clear(image, TRANSMISSION_CAPTION, CAPTION_ROWS)
    x = paste_word(image, reference, [O_I_L], [WORD_SPACE], TRANSMISSION_X)
    paste_word(image, reference, [T, E, M, P], [4, 4, 4, 0], x)
    # ENGINE, centred in the tall tile.
    clear(image, ETHANOL_CAPTION, (y0 + TALL_CAPTION_SHIFT, y1 + TALL_CAPTION_SHIFT))
    engine, spacing = [E, N, G, I, N, E], [4, 3, 4, 4, 4, 0]
    x = round((ETHANOL_TILE[0] + ETHANOL_TILE[1]) / 2 - word_width(engine, spacing) / 2)
    paste_word(image, reference, engine, spacing, x, TALL_CAPTION_SHIFT)
    # Warm-up band strip beside the thermometer: 100 F at the bottom, 300 F at the top.
    draw = ImageDraw.Draw(image)
    left = BAR_LEFT + BAR_WIDTH + STRIP_GAP
    bottom = BAR_TOP + BAR_HEIGHT

    def y(fahrenheit):
        return bottom - (fahrenheit - BAR_MIN_F) / (BAR_MAX_F - BAR_MIN_F) * BAR_HEIGHT

    for low, high, colour in BANDS:
        draw.rounded_rectangle((left, round(y(high)) + 1, left + STRIP_WIDTH - 1, round(y(low)) - 1),
                               radius=1, fill=colour)
    return image


def main(path):
    with ZipFile(path) as archive:
        names = archive.namelist()
        reference = Image.open(BytesIO(archive.read(REFERENCE))).convert('RGBA')
    if reference.size != (1172, 400):
        raise SystemExit('unexpected reference background size %s' % (reference.size,))
    if N55 in names:
        raise SystemExit('%s already present' % N55)
    data = BytesIO()
    n55_background(reference).save(data, format='PNG', optimize=True)
    with ZipFile(path, 'a', compression=ZIP_DEFLATED) as archive:
        archive.writestr(ZipInfo(N55), data.getvalue(), ZIP_DEFLATED)


if __name__ == '__main__':
    main(sys.argv[1])
