# Gaia Green

Godot Terrain Generator for 3D Tiled Maps

Godot 4.4 is currently used Version

# Overview

1. Pre-Computed Tile Set's
   1. Each tile is an index id
      1. This is because we use a bitset to store the valid neighbors of a tile
      1. 0 is reserved for empty tiles
   1. Give the Terrain Generator a set of tiles by index with pre-computed adjacencies for each of the size sides
   1. Weights can also be supplied to factor in more successful tiles
1. Pre-Defined Chunk Injection
   1. Give the Terrain Generator a set of pre-defined tile chunks, grid sections, chunks per section
      1. Take in a set of Relative Coordinate Chunks
      1. Take in Grid Sections
      1. Take in Chunks per Grid Section
   1. Randomly place chunks into each section
1. Height Manipulation
   1. Using [Simplex-Fractal-Noise](https://github.com/SRombauts/SimplexNoise?tab=readme-ov-file#readme)
   1. Height limitations are applied when collapsing the Wave Function
1. Collapse the Wave
   1. Using Wave Function Collapse determine a final grid with Tiles
1. Output a final 3 dimensional array with a tile index at each location
1. Use the generated 3d grid to generate a visual representation by iterating through the 3d array and instantiating the tiles in the game world

# Setup

## Windows

1. Setup a C++ Environment
   1. Possible Setup for VS Code : https://code.visualstudio.com/docs/cpp/config-mingw

Ensure that C++ can run :

```bash
gcc --version
g++ --version
gdb --version
```

# Godot

## Godot GDExtension

[This repository uses the Godot quickstart template for GDExtension development with Godot 4.0+.](https://github.com/godotengine/godot-cpp-template)

### Contents

- godot-cpp as a submodule (`godot-cpp/`)
- (`demo/`) Godot 4.4 Project that tests the Extension
- preconfigured source files for C++ development of the GDExtension (`src/`)
- setup to automatically generate `.xml` files in a `doc_classes/` directory to be parsed by Godot as [GDExtension built-in documentation](https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/gdextension_docs_system.html)

## Github

_Currently Commented Out for Base Development_

- GitHub Issues template (`.github/ISSUE_TEMPLATE.yml`)
- GitHub CI/CD workflows to publish your library packages when creating a release (`.github/workflows/builds.yml`)

This repository comes with a GitHub action that builds the GDExtension for cross-platform use. It triggers automatically for each pushed change. You can find and edit it in [builds.yml](.github/workflows/builds.yml).
After a workflow run is complete, you can find the file `godot-cpp-template.zip` on the `Actions` tab on GitHub.
