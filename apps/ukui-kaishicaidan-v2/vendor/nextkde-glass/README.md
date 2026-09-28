# NextKde glass shader provenance

Unmodified shader sources copied from:
https://github.com/xikario/NextKde-kylinos

Commit: 0dcd60cbd8723a6da95349a82fbc63385081c22a
Paths: vendor/kwin-effects-glass/src/shaders/{glass,snells-glass}.glsl
License: upstream GPL v3 (see LICENSE in this directory).

The Qt5 adapter in src/NextKdeGlassView.cpp expands the local shader include,
maps texture() to GLSL 1.20 texture2D(), supplies uniforms, and calls upstream
snellsRefraction(), applySoftMaterial() and applyLiquidGlints().
Snell refraction, RGB dispersion, SDF normals, body lens and directional rim
glints are upstream code, not approximations re-labelled as an upstream port.

The R3 adapter uses separate lightly blurred optical and diffused body textures,
an adaptive neutral scrim, a local directional caustic, and premultiplied output.
These material choices are LOCAL adaptations, not an exact upstream pipeline.
The parent also paints a pointer-following rim reflection. This is an
open-time screen snapshot, NOT the live KWin framebuffer or KWin6 blur pipeline.
The CPU fallback retains readable blur/tint but has no optical refraction.

R5 adds a local control-mode branch for nested glass. It samples a clean crop
of the cached panel material (not widget contents), uses smaller optical
parameters and avoids applying the panel scrim twice. Control images are
cached under an 8 MiB budget; pointer glints are a separate QPainter overlay.
Upstream shader files and the panel's R4 material parameters remain unchanged.
