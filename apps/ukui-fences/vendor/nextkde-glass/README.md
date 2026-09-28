# NextKde glass provenance

Shader files are unmodified copies of the locally tested ukui-kaishicaidan-v2
vendor sources, originally from xikario/NextKde-kylinos commit
0dcd60cbd8723a6da95349a82fbc63385081c22a:
`vendor/kwin-effects-glass/src/shaders/{glass,snells-glass}.glsl`.
Upstream GPL v3 license is preserved in LICENSE.

FenceGlassRenderer adapts the Qt5/OpenGL 2.1 renderer from that local V2 app.
It uses upstream Snell refraction, dispersion and liquid glints, with the V2
local adaptive scrim and caustic. Diffusion is shared across all fences, and
each fence caches its clean material independently of icons/text.
Background input is the desktop wallpaper, not live windows or a screenshot.
Pointer-following reflection is a local QPainter overlay. CPU fallback uses
blur and tint only. Magnetic contours use a locally generated signed-distance
texture and antialiased coverage in the adapter, feeding the same upstream
optics with shape-aware distances/normals rather than a rectangular silhouette.
