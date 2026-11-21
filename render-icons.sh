#!/bin/sh
# Copyright (C) 2026 The CGTK authors. Under the GNU General Public
# License, version 2 or later: see COPYING.
#
# Each tool's PNGs and XPM, from its one SVG, with rsvg-convert and ImageMagick.
# What this makes is kept in the repository, so neither is needed to build.
set -e
cd "$(dirname "$0")"

for t in cghex cgcalc cgpaint cgedit; do
    for s in 16 22 24 32 48 64 128 256; do
        rsvg-convert -w $s -h $s $t/icons/$t.svg -o $t/icons/$t-$s.png
    done

    # The XPM is the 48 with each pixel either there or not, in at most
    # 255 colours.
    convert $t/icons/$t-48.png -channel A -threshold 50% +channel \
        +dither -colors 255 xpm:- |
        sed "s/^static .*\[\] = {\$/static char *${t}_xpm[] = {/" > $t/icons/$t.xpm
done
