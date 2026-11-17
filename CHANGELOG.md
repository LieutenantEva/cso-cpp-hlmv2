# Changelog

### V1.01

* Fix crashes due to smaller studiomodel limits.

### V1.00

* Based on Half-Life Model Viewer 2.10

#### CSO MDL version 2026

* The model viewer can open and display "IDST version 2026" models (newer Counter-Strike Online clients) directly, without converting them.
* Vertices keep their blended bone weights and textures stay true color (BC7).
* The format is documented in `src/tools/hlmv/engine/shared/studiomodel/StudioV2026.h`.
* Version 2026 models can be saved. Everything the viewer edits is written back to the 2026 file, including bone and mesh scaling and replaced textures.
* Textures are exported as BC7 `.dds` files, which is the format stored in the model (`.bmp` is available too). They can be imported from `.dds` files (BC7 is used as it is, uncompressed files are compressed by a simple built in encoder) and from `.bmp` files.

Note: For the best quality import textures that were compressed with a dedicated tool as BC7 DDS files.

Limitations:

* Palette remapping (recoloring) does nothing for version 2026 models because their textures have no palette.
* Scaling only the bones or only the meshes of a model with vertices that are blended between bones can't be saved exactly.

