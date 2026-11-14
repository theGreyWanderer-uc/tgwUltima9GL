U9GL replacement texture example
================================

05434_00.png replaces one of the game's sky textures with a smiley face, so you
can see a replacement working.

Try it
------

1. Copy 05434_00.png from this folder into the textures folder above it, so it
   sits at u9gl\textures\05434_00.png. Files in this example folder are not
   loaded: U9GL only reads files directly inside textures.
2. In U9GL.exe, open the Graphics page, set "Replacement textures" to On and
   press Save.
3. Start the game. On a clear day (not in a storm, not at dusk), look straight
   up above the Avatar. The smiley is the highest part of the sky.

To undo it, delete u9gl\textures\05434_00.png.

Make your own
-------------

Name: each file is named for the texture it replaces, <id>_<id2>.png
  (.tga and .bmp work too). Names ending in _a are skipped.

Finding a texture's name: on U9GL.exe's Diagnostics page, turn on
  "Texture dump" and play past what you want to replace. Every texture the
  game draws is saved once to %TEMP%\u9gl_textures. The first two numbers of a
  file name are the name to use: 05434_00_256x256_fmt0A.bmp becomes
  05434_00.png. Turn Texture dump off again afterwards.

Size: width and height must each be a power of two (256, 512, 1024, 2048),
  no larger than 2048. Keep the original's shape: a 256x256 original wants a
  square replacement, a 64x128 one twice as tall as it is wide.

Transparency: if the original has see-through parts, your file must carry
  them in its alpha channel (a PNG with transparency). Many of the game's
  textures mark holes with pure white instead; the dump's _a file shows real
  transparency, and pure white in the picture marks those holes. A
  replacement without the holes in its alpha draws them solid.

Folders: only files directly inside textures are loaded. Keep work in
  progress in a subfolder and copy finished files up.

Updates replace this example folder. Work on your own copies, not here.
