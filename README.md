# Gaia Green

Godot Terrain Generator for 3D Tiled Maps

Godot 4.4 is currently used Version

## Overview

1. Prep FastNoiseLite
   1. Set Noise Function (i.e. SimplexNoise)
   1. Set a Seed for the Noise Function
1. Generate Elevation Map (Height Map)
   1. Tiles are isometric, need blocky/square patterns
   1. Reduce Resolution of Noise
   1. Posterize Reduced Resolution
   1. Upscale reduction to original size
1. Tile Placement
   1. Determine tile Types
   1. Use 0 Elevation for Water
   1. On Elevation Change between grid positions, use Cliffs or Ramps
   1. Determine Rotation of Tiles based on surrounding Tiles
1. Applicator
   1. Change the 3D Grid Map in Godot using 'set_cell_item'
   1. Providing the TileID, Grid Position, & Rotation Orientation

## Setup

### Windows

1. Setup a C++ Environment
   1. Possible Setup for VS Code : https://code.visualstudio.com/docs/cpp/config-mingw

Ensure that C++ can run :

```bash
gcc --version
g++ --version
gdb --version
```

## Contributing

Any and all Contributions are subject to the [CLA.md](https://github.com/kthecoder/GaiaGreen/blob/main/CLA.md)
