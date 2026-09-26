# QTikZ

QTikZ is a desktop TikZ editor built with Qt 6.
It combines source editing, compilation, and interactive geometry manipulation in a single application for rapid figure iteration.

## Purpose

QTikZ is designed for users who need to:

- edit TikZ/LaTeX source directly
- compile and preview results immediately
- adjust geometric primitives visually and write changes back to source

The project targets a practical authoring workflow rather than full TikZ language coverage.

## Core Capabilities

- Two-way editing workflow:
  - source-first editing in a LaTeX/TikZ editor
  - canvas-assisted editing through draggable control markers
- Live PDF preview using Qt PDF rendering
- Integrated compilation log with status feedback
- Configurable grid, snapping, and canvas interaction
- Built-in object insertion panel (line, polyline, circle, rectangle, ellipse, bezier, text)
- Object properties panel for geometry and style editing

## User Interface

- Left stripe and panel:
  - IDE-style vertical toggle (`Add object`) to show/hide the Objects panel
  - object insertion actions
- Main editor/preview area:
  - left: syntax-highlighted LaTeX/TikZ editor with line numbers
  - right: PDF preview canvas with zoom/pan and interactive markers
  - far right: scrollable Properties panel for selected objects
- Bottom area:
  - console output with clear action

## Editing and Interaction

### Source Editing

- Monospace code editor
- LaTeX syntax highlighting
- Auto indentation (`Indent` action)
- Undo/redo, save/save-as, modification tracking (`*` in title)

### Canvas Interaction

- Mouse wheel zoom
- Drag to pan
- Marker-based geometry updates for supported primitives
- Click-to-place insertion mode for object creation

### Properties Editing

For selected objects, QTikZ exposes editable properties such as:

- geometry values (coordinates, radii, control points)
- border color, endpoint caps (start/end), line style, thickness, opacity
- fill color and fill opacity

Updates are applied to the source and recompiled automatically.

## Compilation Pipeline

- Uses a local LaTeX compiler process (default `pdflatex`)
- Injects the preview grid and invisible numerical calibration into the temporary compile document
- Reads the picture origin recorded by PGF and its exported coordinate basis after compilation;
  calibration no longer relies on colored dots or pixel detection
- Loads generated PDF into preview canvas
- Reports compile output and status in the console pane

## Settings

`Edit -> Settings...` provides persistent application settings via `QSettings`:

- editor font family
- editor font size
- line number visibility
- auto-compile debounce delay
- LaTeX compiler command/path
- look and feel (`System`, `Light`, `Dark`)
- default grid/snap options

## Requirements

- Qt 6 (Core, Widgets, Pdf)
- CMake >= 3.21
- A working LaTeX installation with `pdflatex` (or another configured compiler)

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/qtikz
```

## Repository Layout

- `src/main.cpp`: application bootstrap
- `src/mainwindow.h`: main window interface and state
- `src/mainwindow.cpp`: window construction, menus/toolbars, editor setup
- `src/mainwindow_properties.cpp`: selection, properties logic, style/geometry application
- `src/settingsdialog.h`, `src/settingsdialog.cpp`: settings dialog
- `src/pdfcanvas.h`, `src/pdfcanvas.cpp`: preview rendering, interaction, marker drawing
- `src/compileservice.h`, `src/compileservice.cpp`: compile orchestration and temporary document generation
- `src/coordinateparser.h`, `src/coordinateparser.cpp`: parser utilities and source token mapping
- `src/model.h`: shared primitive/reference models

## Scope and Current Constraints

QTikZ intentionally focuses on a defined subset of TikZ constructs.
Complex or highly custom TikZ expressions may not be parsed into editable markers/properties.
Source editing remains fully available for unsupported constructs.

Numerical calibration is tested with pdfLaTeX and LuaLaTeX, including `article`,
cropped `standalone` pages, and picture-level rotations, scales and oblique axes.
Interactive calibration targets the first TikZ picture on the first physical page.
Nested scopes with their own transforms and external PDF cropping/rotation are not
covered by this global mapping. Missing calibration disables canvas coordinate editing.

To run the integration checks (requires both engines and the `standalone` package):

```bash
cmake -S . -B build -DQTIKZ_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0-or-later)**.

See `LICENSE` for the full text.
