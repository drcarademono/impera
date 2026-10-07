These are the unmodified ScaleFX pass 0–4 shaders by Sp00kyFox from:
https://github.com/libretro/glsl-shaders/tree/435612fe4f1023117b3aae48c88603fb413404a3/scalefx/shaders

Each shader contains its original copyright and MIT permission notice.
Impera compiles the vertex/fragment sections with a desktop GLSL 130 or
OpenGL ES GLSL 300 header, runs the first four passes at native resolution,
and samples their 3x subpixel reconstruction in the final output pass.
The first two intermediate targets retain floating-point metric data.
