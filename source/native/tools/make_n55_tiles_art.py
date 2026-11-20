"""Add the configurable-tile artwork to a BoostDeck image archive that already has 56301
(tools/make_n55_background.py):

  56302          56301 with the eight tile captions cleared (ENGINE and its band strip stay)
  57000 + m      the caption of metric m (n55_table), drawn where the old captions were
  57063          an empty caption, for a blank tile
  58000 + ...    bars that start at the zero mark, for each NB_ZERO zero position:
                 58000 + set * 600 + palette * 33 + step (step 0..32, palette 0..15)

Captions use a DIN font so they match the originals: DIN Alternate Bold on macOS by default, or
any font given with --font (D-DIN, under the SIL Open Font License, also matches).

Usage: python3 make_n55_tiles_art.py BoostDeck_ALL_Images.zip [--font path.ttf] [--preview out.png]
"""
import argparse
import sys
from io import BytesIO
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import n55_table as table  # noqa: E402

DEFAULT_FONT = '/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf'
SOURCE, OUT = '56301.png', '56302.png'
TILE = (23, 27, 34, 255)
CAPTION_COLOUR = (186, 200, 216, 255)       # the original captions' fill
CAPTION_SIZE = (220, 30)                    # BoostDeck.xml caption images
CAPTION_TEXT = (4, 5)                       # cap top 5 px down, as the originals (rows 25-43)
CAP_HEIGHT = 19
# Background tile columns and caption rows (background pixels; the image sits at 24,13).
COLUMNS = (0, 258, 516, 774)
CAPTION_ROWS = (20, 228)
CAPTION_CLEAR = (8, 236)                    # x range inside a tile
TRACK, BAR_SIZE, RADIUS = (38, 51, 66, 255), (240, 12), 4
MARK = (186, 200, 216, 255)
COLOURS = ('#ff4f1a', '#58ceff', '#6cdb93', '#e2eaf0', '#ad72ff', '#ff6cba', '#20f1e6', '#ff4545',
           '#58ceff', '#5de0bf', '#cbacff', '#ffbe70', '#6cdb93', '#60c5ff', '#ffb76a', '#6fd9ba')


def png(image):
    data = BytesIO()
    image.save(data, format='PNG', optimize=True)
    return data.getvalue()


def caption_font(path):
    for size in range(18, 40):
        font = ImageFont.truetype(path, size)
        box = font.getbbox('B')
        if box[3] - box[1] >= CAP_HEIGHT:
            return font, box[1]
    raise SystemExit('font too small')


def caption(text, font, top):
    image = Image.new('RGBA', CAPTION_SIZE, (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    if draw.textlength(text, font=font) > CAPTION_SIZE[0] - CAPTION_TEXT[0]:
        raise SystemExit('caption too wide: ' + text)
    draw.text((CAPTION_TEXT[0], CAPTION_TEXT[1] - top), text, font=font, fill=CAPTION_COLOUR)
    return image


def background(source):
    image = source.copy()
    draw = ImageDraw.Draw(image)
    for x in COLUMNS:
        for y in CAPTION_ROWS:
            draw.rectangle((x + CAPTION_CLEAR[0], y, x + CAPTION_CLEAR[1] - 1, y + CAPTION_SIZE[1] - 1), fill=TILE)
    return image


def zero_bar(zero, step, colour):
    """Track, the fill between the zero mark and step, and the mark."""
    w, h = BAR_SIZE
    image = Image.new('RGBA', BAR_SIZE, (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((0, 0, w - 1, h - 1), radius=RADIUS, fill=TRACK)
    zx, sx = round(w * zero / table.STEPS), round(w * step / table.STEPS)
    if step != zero:
        a, b = min(zx, sx), max(zx, sx)
        b = max(b, a + 4)
        draw.rounded_rectangle((a, 0, min(b, w) - 1, h - 1), radius=RADIUS if a == 0 or b >= w else 1, fill=colour)
    draw.rectangle((max(zx - 1, 0), 0, min(zx, w - 1), h - 1), fill=MARK)
    return image


def build(source, font_path):
    font, top = caption_font(font_path)
    files = {OUT: png(background(source)), '%d.png' % table.CAPTION_BLANK: png(Image.new('RGBA', CAPTION_SIZE))}
    for index, (_, text, *_rest) in enumerate(table.metrics()):
        files['%d.png' % (table.CAPTION_FIRST + index)] = png(caption(text, font, top))
    for zero_index, zero in enumerate(table.zero_sets()):
        for palette, colour in enumerate(COLOURS):
            for step in range(table.STEPS + 1):
                identifier = table.ZERO_FIRST + zero_index * table.ZERO_SET + palette * (table.STEPS + 1) + step
                files['%d.png' % identifier] = png(zero_bar(zero, step, colour))
    return files


def preview(source, files, out):
    """The cleared background with the default tiles' captions, as the car would draw it."""
    image = Image.open(BytesIO(files[OUT])).convert('RGBA')
    keys = [k for k, *_ in table.metrics()]
    defaults = ('boost', 'afr', 'timing', 'iat', 'oil_temp', 'hpfp', 'oil_pressure', 'coolant')
    for p, key in enumerate(defaults):
        cap = Image.open(BytesIO(files['%d.png' % (table.CAPTION_FIRST + keys.index(key))]))
        image.alpha_composite(cap, (COLUMNS[p // 2] + 12, CAPTION_ROWS[p % 2]))
    sheet = Image.new('RGBA', (image.width, image.height * 2 + 10), TILE)
    sheet.paste(source, (0, 0))
    sheet.paste(image, (0, image.height + 10))
    sheet.save(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('archive')
    parser.add_argument('--font', default=DEFAULT_FONT)
    parser.add_argument('--preview')
    args = parser.parse_args()
    with ZipFile(args.archive) as archive:
        names = set(archive.namelist())
        source = Image.open(BytesIO(archive.read(SOURCE))).convert('RGBA')
    files = build(source, args.font)
    if names & set(files):
        raise SystemExit('archive already has %s' % sorted(names & set(files))[0])
    if args.preview:
        preview(source, files, args.preview)
    with ZipFile(args.archive, 'a', compression=ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(ZipInfo(name), data, ZIP_DEFLATED)
    print('%d images added' % len(files))


if __name__ == '__main__':
    main()
