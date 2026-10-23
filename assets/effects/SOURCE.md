# Battle effects v1

Generated with the built-in `image_gen` tool on 2026-10-14.

- Source: `battle-effects-v1.png`, transparent original atlas.
- Cartridge preview: `battle-effects-gba.png`.
- Importer: `tools/import_battle_effects.py`.
- Eight effect families, three frames each, 48 shared colors. Transparent
  pixels stay transparent; each frame fits inside a 48×48 drawing area.
- The row boundaries are measured from this source atlas, rather than
  assuming the generator produced eight exactly equal rows.

## Generation prompt

Create ONE production sprite atlas image for a tiny 240x160 GBA Three Kingdoms battle game. Transparent alpha background. Exactly 3 columns and 8 rows, equal rectangular cells, no grid lines, no labels, no text, no watermark, no characters or environment. Three consecutive animation phases left-to-right per row, aligned consistently. Row1: orange-red fire explosion, small ignition, full swirling flare, dissipating embers. Row2: emerald healing aura with concentric golden oval rings and rising sparkles, three phases. Row3: gold arrow magic volley, sharp compact arrows with glowing trails, three phases pointing right. Row4: cyan-white zigzag lightning bolt striking down, crackling impact and fading branches. Row5: teal-white horizontal crescent shockwave, consecutive traveling forms pointing right. Row6: violet-gold ghost halberd blade slash, bold sweeping crescent with metallic blade silhouette, consecutive phases. Row7: coiled red-gold Chinese dragon made of flame, flying right, three phases. Row8: jagged earth boulders eruption, rising rock, peak fragments, falling debris. Each effect isolated in its own cell with generous transparent margin; NO cross-cell overlap. True crisp hand-drawn 16-bit pixel art with chunky deliberate pixel clusters, 2-3 shade ramps, strong silhouette, saturated bright core and dark colored outer edge, absolutely no blur or soft glow gradients. Needs to remain legible when each effect is reduced to 32-48 pixels tall. Equal cell spacing and dimensions for reliable programmatic slicing. Atlas aspect ratio 3:4 (768x1024 preferred), all eight rows visible, each cell center aligned.
