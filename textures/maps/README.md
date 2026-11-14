`U5brit.png` is the color reference for the generated Britannia map.

The game does not load or embed the PNG. It generates one pixel per active
world terrain tile whenever the map opens, using the colors in
`src/common/world_map_palette.h`. Custom terrain types inherit their base
tile color. Pixels initially match the game pixel size and aspect ratio; the
mouse wheel zooms, and arrows or dragging pan.
