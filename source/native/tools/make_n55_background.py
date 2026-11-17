"""Add the F87 M2 N55 BoostDeck background (56301) to a packaged BoostDeck image archive.

The N55 profile shows engine oil temperature and its warm-up band in the Transmission slot,
captioned ENGINE. The caption is built from glyphs already in the reference background
(56300): E from PRESSURE, and N, G and I from TIMING, so the font matches exactly. 56300 is
left unchanged.

Usage: python3 make_n55_background.py BoostDeck_ALL_Images.zip
"""
import sys
from io import BytesIO
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image

REFERENCE, N55 = '56300.png', '56301.png'
TILE = (23, 27, 34, 255)
CAPTION_ROWS = (20, 49)
TRANSMISSION_CAPTION = (528, 712)
E, N, G, I = (943, 954), (322, 335), (339, 352), (289, 291)
ENGINE = (E, N, G, I, N, E)
# Spacing after each glyph, taken from the reference: N->G is 3 px in TIMING, the rest 4 px.
SPACING = (4, 3, 4, 4, 4, 0)


def n55_background(reference):
    y0, y1 = CAPTION_ROWS
    image = reference.copy()
    for x in range(*TRANSMISSION_CAPTION):
        for y in range(y0, y1):
            image.putpixel((x, y), TILE)
    x = 532
    for (a, b), space in zip(ENGINE, SPACING):
        glyph = reference.crop((a, y0, b + 1, y1))
        image.paste(glyph, (x, y0))
        x += glyph.width + space
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
