Place the supplied **256 × 256** Britannia map here as **U5brit.png**.
Each pixel corresponds to one world tile: (0,0) is the northwest corner.

CMake embeds this image in the engine, including release packages. Adding or
replacing it triggers reconfiguration on the next build. During development,
`textures/maps/U5brit.png` relative to the working directory overrides the
embedded copy. Visual Studio project builds copy the PNG beside the executable under
`textures/maps/`. The artwork uses square pixels and nearest-neighbor sampling.
