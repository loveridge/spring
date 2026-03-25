Below is a concrete implementation plan for a maintainable font-rendering stack, with a proposed file layout and the responsibility of each file.

The plan assumes these goals:

- preserve the current Spring API shape where practical
- introduce a clean shaping layer for HarfBuzz
- separate font data, layout, wrapping, and rendering
- keep GPU backend swappable
- make headers smaller and less coupled

---

# 1. Target architecture

The implementation should be organized as a pipeline:

1. **Font face management**
   Loads and owns FreeType faces and fallback faces.

2. **Glyph cache / atlas**
   Rasterizes glyphs, stores metrics, packs into atlas textures, uploads dirty regions.

3. **Text shaping**
   Converts UTF-8 text spans into glyph runs using HarfBuzz.

4. **Text layout**
   Applies Spring control codes, line breaks, alignment, wrapping, ellipsis, and produces positioned glyphs.

5. **Rendering**
   Converts positioned glyphs into quads and submits them through a GL backend.

6. **Public font API**
   Exposes `Print`, `GetTextWidth`, `Wrap`, colors, buffering, etc.

This is the key structural change: each stage should have one job.

---

# 2. Implementation phases

## Phase 1: extract core types and constants

Create shared data structures and move global constants out of heavyweight headers.

Deliverables:

- common geometry/types headers
- text control code utilities
- render option enum

## Phase 2: isolate FreeType and glyph atlas

Refactor current `CFontTexture` into face management plus glyph caching.

Deliverables:

- face lifetime wrapper
- glyph cache with atlas update/upload
- glyph lookup by codepoint and glyph index

## Phase 3: add HarfBuzz shaper

Introduce a shaping layer that consumes printable spans and returns glyph runs.

Deliverables:

- `TextShaper` interface
- `HarfBuzzTextShaper` implementation
- shaped run/glyph data structures

## Phase 4: build text layout and wrapping layer

Move color-code parsing, span splitting, line layout, width measurement, and wrapping into a dedicated layouter/wrapper.

Deliverables:

- control-code-aware text span parser
- layouter producing positioned glyph runs
- wrapper for max-width/max-height with ellipsis

## Phase 5: refactor renderer backends

Separate renderer interface from implementations and make them consume already-positioned glyph quads.

Deliverables:

- renderer interface
- shader backend
- legacy backend
- null backend
- factory

## Phase 6: thin public `CglFont` façade

Keep existing external behavior, but delegate internally.

Deliverables:

- `CglFont` as public API only
- reduced header exposure
- caching of width/height/layout results

## Phase 7: integration and migration

Adapt existing call sites and preserve compatibility wrappers.

Deliverables:

- old API forwarding to new internal implementation
- tests and visual validation
- removal of obsolete inheritance

---

# 3. Proposed file layout

A reasonable directory structure:

```text
Rendering/Fonts/
  FontTypes.h
  FontOptions.h
  FontRegistry.h
  FontRegistry.cpp

  FreeType/
    FontLibrary.h
    FontLibrary.cpp
    FontFace.h
    FontFace.cpp
    FontFaceSet.h
    FontFaceSet.cpp

  Glyphs/
    GlyphInfo.h
    GlyphAtlasCache.h
    GlyphAtlasCache.cpp
    GlyphAtlasTexture.h
    GlyphAtlasTexture.cpp
    KerningCache.h
    KerningCache.cpp

  Text/
    TextControlCodes.h
    TextControlCodes.cpp
    TextShaper.h
    HarfBuzzTextShaper.h
    HarfBuzzTextShaper.cpp
    TextRun.h
    TextLayouter.h
    TextLayouter.cpp
    TextWrapper.h
    TextWrapper.cpp

  Render/
    IFontRenderer.h
    FontRendererFactory.h
    FontRendererFactory.cpp
    ShaderFontRenderer.h
    ShaderFontRenderer.cpp
    LegacyFontRenderer.h
    LegacyFontRenderer.cpp
    NullFontRenderer.h
    NullFontRenderer.cpp

  API/
    glFont.h
    glFont.cpp
```

You do not need all of these on day one, but this is the clean target.

---

# 4. File-by-file plan

## `FontTypes.h`

Purpose: shared low-level data types used by multiple layers.

Should contain:

- `GlyphRect`
- common metric structs
- `FontDescriptor`
- possibly small POD structs for colors/depth if reused internally

Example contents:

- glyph rectangle type replacing `IGlyphRect`
- font identity fields: file path, pixel size, outline size/weight
- maybe `GlyphKey` for codepoint vs glyph-index lookup

This file should contain only plain data and trivial helpers.

---

## `FontOptions.h`

Purpose: render and alignment flags.

Should contain:

- scoped enum replacing global `FONT_*` constants
- bitmask operator helpers

Example:

- `enum class FontOption : uint32_t`
- `HasOption(flags, FontOption::Outline)`

This keeps option definitions out of `glFont.h` clutter and avoids global integer flags.

---

## `FontRegistry.h` / `FontRegistry.cpp`

Purpose: process-wide font initialization and fallback management.

Should contain:

- static/global font subsystem init/shutdown
- fallback font registration
- optional shared registry of loaded faces
- maybe system font discovery hooks

Methods:

- `Init()`
- `Shutdown()`
- `AddFallbackFont(path)`
- `ClearFallbackFonts()`

This is where current static operations from `CFontTexture` should move.

---

# FreeType layer

## `FreeType/FontLibrary.h` / `.cpp`

Purpose: own and initialize the global FreeType library and optional Fontconfig setup.

Should contain:

- RAII wrapper around `FT_Library`
- one-time initialization
- fontconfig initialization if you still need it

This replaces `FtLibraryHandlerProxy` with a real subsystem type.

---

## `FreeType/FontFace.h` / `.cpp`

Purpose: wrap a single `FT_Face` and its backing memory safely.

Should contain:

- font file bytes wrapper
- face wrapper with RAII
- accessors for `FT_Face`

Methods:

- `GetFTFace()`
- family/style queries
- maybe a helper for `FT_Load_Glyph`

This file should not know anything about atlases, renderers, or wrapping.

---

## `FreeType/FontFaceSet.h` / `.cpp`

Purpose: represent the primary face plus fallback faces.

Should contain:

- primary face
- ordered fallback faces
- lookup helpers to find a face supporting a codepoint or glyph
- maybe glyph-name lookup if still needed

Methods:

- `GetPrimaryFace()`
- `FindFaceForCodepoint(char32_t)`
- `GetFaces()`

This is the boundary between font identity and glyph rasterization.

---

# Glyph layer

## `Glyphs/GlyphInfo.h`

Purpose: define the cached glyph record.

Should contain:

- atlas coordinates
- bitmap bounds
- metrics
- owning face reference
- glyph id / codepoint metadata

Fields:

- glyph index
- source codepoint if applicable
- advance, descender, height
- atlas UVs for primary and shadow/outline

Keep it as a data-only definition.

---

## `Glyphs/GlyphAtlasTexture.h` / `.cpp`

Purpose: manage the atlas bitmap and GL texture resource.

Should contain:

- atlas dimensions
- dirty state
- upload/update logic
- texture creation / reallocation

Methods:

- `NeedsUpdate()`
- `NeedsUpload()`
- `UpdateBitmapRegion(...)`
- `Upload()`
- `Reallocate(...)`

This separates “glyph cache contents” from “GPU texture mechanics.”

---

## `Glyphs/KerningCache.h` / `.cpp`

Purpose: store legacy kerning data if still required.

Should contain:

- ASCII precache
- dynamic kerning cache for fallback / legacy paths

This may become much less important after HarfBuzz, but keep it isolated so it can be removed later.

---

## `Glyphs/GlyphAtlasCache.h` / `.cpp`

Purpose: the main glyph cache and rasterizer.

Should contain:

- `FontFaceSet` reference
- `GlyphAtlasTexture`
- atlas allocator
- glyph maps
- glyph loading by codepoint and glyph index
- atlas packing
- outline/shadow bitmap generation

Methods:

- `GetGlyphByCodepoint(char32_t)`
- `GetGlyphByGlyphIndex(uint32_t, std::shared_ptr<FontFace>)`
- `EnsureGlyphs(span<char32_t>)`
- `EnsureGlyphIndices(...)`
- `ClearGlyphs()`
- `PreloadGlyphs()`
- `ReallocAtlases()`

This file is the proper home for most of current `CFontTexture.cpp` implementation logic.

It should not know about wrapping or alignment.

---

# Text/control-code layer

## `Text/TextControlCodes.h` / `.cpp`

Purpose: isolate Spring-specific inline formatting codes.

Should contain:

- control-code constants
- helpers to recognize and skip control codes
- structures for extracted color control data

Definitions:

- `ColorCodeIndicator`
- `ColorCodeIndicatorEx`
- `ColorResetIndicator`
- CR/LF helpers

Functions:

- `SkipColorCodes(...)`
- `TryParseColorCode(...)`
- `IsLineBreak(...)`

This removes parsing logic from multiple unrelated classes.

---

## `Text/TextRun.h`

Purpose: shared text-layout structs.

Should contain:

- text span types
- shaped glyph/run structs
- laid-out line/run structs
- maybe wrap word/line structs

Examples:

- `TextSpan`
- `ShapedGlyph`
- `ShapedRun`
- `LaidOutGlyph`
- `LaidOutLine`

This becomes the common language of the shaping/layout pipeline.

---

## `Text/TextShaper.h`

Purpose: shaping abstraction.

Should contain:

- interface for shaping text spans
- no HarfBuzz headers in the interface if avoidable

Methods:

- `ShapeUtf8Span(std::string_view utf8)`
- optional overloads with script/lang/direction hints

This allows the rest of the pipeline to depend on shaping generically.

---

## `Text/HarfBuzzTextShaper.h` / `.cpp`

Purpose: HarfBuzz-backed shaper implementation.

Should contain:

- `hb_font_t` management
- buffer setup
- UTF-8 shaping
- output conversion to `ShapedRun`

Methods:

- constructor from `FT_Face` or face set
- shape span
- maybe script/direction detection hooks

This file should know about HarfBuzz and FreeType, but not about GL rendering or text wrapping.

---

## `Text/TextLayouter.h` / `.cpp`

Purpose: convert text into positioned runs for measurement and rendering.

Should contain:

- control-code aware parsing of input string
- span splitting by line break and formatting codes
- width/height computation from shaped runs
- line layout
- horizontal/vertical alignment
- output of positioned glyphs

Methods:

- `MeasureText(...)`
- `LayoutText(...)`
- `SplitIntoLines(...)`
- maybe `LayoutSingleLine(...)`

This is the core coordination layer between shaping and rendering.

It should use:

- `TextControlCodes`
- `TextShaper`
- `GlyphAtlasCache`

but should not own GL state.

---

## `Text/TextWrapper.h` / `.cpp`

Purpose: wrapping and ellipsis.

Should contain:

- max-width/max-height wrapping
- split-word logic
- line truncation and ellipsis insertion
- remerge color codes

Methods:

- `WrapInPlace(...)`
- `Wrap(...)`

This file should depend on `TextLayouter` or a narrower measuring interface, not on glyph caches directly.

This replaces `CTextWrap` as a pure algorithm/service.

---

# Render layer

## `Render/IFontRenderer.h`

Purpose: renderer interface only.

Should contain:

- virtual draw API for prepared quads or positioned glyphs
- GL state push/pop
- texture sync hooks
- stats

Methods:

- `AddPrimaryQuad(...)`
- `AddOutlineQuad(...)`
- `DrawQueued()`
- `HandleTextureUpdate(...)`
- `PushState(...)`
- `PopState(...)`

This file should be small and implementation-agnostic.

---

## `Render/FontRendererFactory.h` / `.cpp`

Purpose: choose backend and create renderer instance.

Should contain:

- backend selection logic
- fallback to null renderer if needed

Methods:

- `Create()`

---

## `Render/ShaderFontRenderer.h` / `.cpp`

Purpose: modern shader backend.

Should contain:

- shader program lifetime
- primary/outline render buffers
- shader uniform setup
- draw submission

Should not contain font layout logic.

---

## `Render/LegacyFontRenderer.h` / `.cpp`

Purpose: fixed-function / no-shader backend.

Should contain:

- legacy vertex/index accumulation
- texture matrix handling
- fixed-function draw path

Again, renderer only.

---

## `Render/NullFontRenderer.h` / `.cpp`

Purpose: no-op backend for headless or failure mode.

Should contain:

- empty implementations
- stats reporting if needed

---

# Public API layer

## `API/glFont.h`

Purpose: the public font class seen by the rest of the engine.

Should contain:

- `CglFont` declaration
- factory/load helpers
- public print/measure/wrap API
- color/depth/matrix setters
- little else

Should not contain:

- renderer implementation details
- text wrapping structs
- glyph atlas internals
- HarfBuzz includes

Key methods:

- `LoadFont(...)`
- `Begin()`, `End()`
- `DrawBuffered()`
- `Print(...)`
- `PrintWorld(...)`
- `GetTextWidth(...)`
- `GetTextHeight(...)`
- `Wrap(...)`
- color/depth config

Internally, `CglFont` should hold a private implementation object or private members for:

- face set
- glyph cache
- shaper
- layouter
- wrapper
- renderer

A `pimpl` is a good fit here.

---

## `API/glFont.cpp`

Purpose: implement the public façade.

Should contain:

- construction and destruction
- buffering lifecycle
- immediate vs buffered rendering behavior
- top-level calls delegating to layouter/shaper/cache/renderer
- user-visible compatibility behavior

This file should coordinate, not implement all details directly.

---

# 5. Recommended class responsibilities

## `CglFont`

Public-facing controller. No deep implementation logic.

## `GlyphAtlasCache`

Glyph loading, atlas population, texture dirtiness.

## `HarfBuzzTextShaper`

Shape printable spans into glyph runs.

## `TextLayouter`

Parse control codes, compute metrics, position glyphs.

## `TextWrapper`

Wrap text to width/height with ellipsis.

## `IFontRenderer`

Backend draw submission.

---

# 6. Migration from current classes

## Current `CFontTexture`

Split into:

- `FontFace`
- `FontFaceSet`
- `GlyphAtlasCache`
- `GlyphAtlasTexture`
- `KerningCache`
- `FontRegistry`

## Current `CTextWrap`

Split into:

- `TextControlCodes`
- `TextWrapper`
- some pieces into `TextLayouter`

## Current `CglFont`

Keep as the public API, but thin it drastically.

## Current `CglFontRenderer`

Keep conceptually, but split into:

- `IFontRenderer`
- `ShaderFontRenderer`
- `LegacyFontRenderer`
- `NullFontRenderer`
- `FontRendererFactory`

---

# 7. Important implementation decisions

## Use composition, not inheritance

Do not keep:

```cpp
CglFont : CTextWrap : CFontTexture
```

Use:

```cpp
CglFont
  has-a GlyphAtlasCache
  has-a TextShaper
  has-a TextLayouter
  has-a TextWrapper
  has-a IFontRenderer
```

This is the most important structural change.

## Keep headers narrow

Move as much as possible from headers into `.cpp`.
Avoid including renderer headers in `glFont.h`.
Avoid including HarfBuzz headers outside shaping implementation.

## Support glyph lookup by glyph index

This is required for HarfBuzz to work correctly.

## Treat Spring control codes as a preprocessing/layering concern

Do not feed them directly into HarfBuzz.

---

# 8. Suggested implementation order by file

A practical order that minimizes churn:

1. `FontTypes.h`
2. `FontOptions.h`
3. `TextControlCodes.h/.cpp`
4. `FontLibrary.h/.cpp`
5. `FontFace.h/.cpp`
6. `FontFaceSet.h/.cpp`
7. `GlyphInfo.h`
8. `GlyphAtlasTexture.h/.cpp`
9. `GlyphAtlasCache.h/.cpp`
10. `TextRun.h`
11. `TextShaper.h`
12. `HarfBuzzTextShaper.h/.cpp`
13. `TextLayouter.h/.cpp`
14. `TextWrapper.h/.cpp`
15. `IFontRenderer.h`
16. renderer backend files
17. `FontRendererFactory.h/.cpp`
18. new `glFont.h/.cpp`
19. compatibility migration and cleanup

This sequence builds from the bottom up.

---

# 9. Testing plan per layer

## Font face/glyph cache

- load a face
- rasterize ASCII glyphs
- rasterize fallback glyphs
- atlas upload and reallocation

## HarfBuzz shaper

- ligatures (`fi`, `ffi`)
- Arabic joining
- combining marks
- cluster mapping

## Layout

- inline color codes
- CR/LF handling
- alignment
- width/height correctness

## Wrapper

- long words
- max-height ellipsis
- color-code preservation after wrapping

## Renderer

- shader path
- legacy path
- headless/null path
- buffered/immediate output parity

---

# 10. Resulting benefits

After this implementation:

- each file has one coherent responsibility
- HarfBuzz has a natural place in the design
- the public font API is simpler
- header coupling and rebuild cost go down
- renderer backends become easier to maintain
- wrapping and layout can evolve independently of glyph rasterization

If you want, I can turn this into a more concrete “starter skeleton” with example class declarations for each file.
