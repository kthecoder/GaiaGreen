# Gaia Green

A User-Controlled Pipeline for Organic Procedural Terrain Generation

_Godot 4.4 is currently used Version_

## Overview

We introduce a comprehensive, user-driven pipeline for generating rich, organic terrains. We employ FastNoiseLite to produce a base height map, which is then refined by a cascade of cellular automata filters that expand flat regions, patch & eliminate outliers, and selectively shrink water bodies. Optional directional bias simulates tectonic tilt, while blocky posterization adapts terrains to isometric grids. Graph-based flow accumulation yields road networks, and a two-tier Poisson disk sampler places large structures and smaller objects. We demonstrate the ability to influence random noise into terrain morphology.

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

## Implementation

1. User Control Inputs
   1. Seeding : `int32_t seed`
   2. Frequency : `float noiseFrequency`
      1. Allow user to define frequency: Lower-frequency noise gives broad zones; higher-frequency adds detail.
   3. Elevation Threshold : `int elevationMax`
      1. Allow user to determine the number of elevations created in the final solution
   4. Open Area Minimum : `int openAreaMin`
      1. Allow users to determine the minimum number of open areas for large structure placement
   5. Water Reduction Value : `waterRemoval`
      1. Water reduction percentage of 0 to 100
      2. Ignore values of 10 or lower
2. Produce Random Noise
   1. Using FastNoiseLite
   2. Allow user to define frequency: Lower-frequency noise gives broad zones; higher-frequency adds detail.
3. Generate Height Map
   1. Take the Filtered Noise, Normalize and Scale to (0, `elevationMax`)
   2. Assume 0 values are base elevation of Water
4. Noise Processing
   1. Noise Conversion (into square patterns)
      1. Send through filters to create blocky patterns useable for isometric 3d grids
      1. Use Elevation Threshold to determine values between 0 to Elevation Threshold
      1. Reduce Resolution by sampling one pixel per block
      1. Posterize
      1. Upscale back to Original Size
5. Cellular Automata (CA) Filter on Noise

   1. CA Filter A : Expanding Zones
      1. Using Flood Fill, find X number of `openAreaMin`, find flat zones that are minimum of 2x2 cells of flat area
      2. Smooth the noise by expanding the flat areas
      3. Create a CA rule that defines flattened areas and expand them
   1. CA Filter B : Patching / Removing Outliers
      1. Even out zero elevation && any cells that are islands
      2. If a cell is zero and has many zero neighbors, it stays zero. Also true for values of 1, 2, 3, etc. If a cell is x and has many x neighbors it stays x.
      3. If a cell is non-zero/x but surrounded by zeros/x, it becomes zero/x (growth).
      4. If a zero/x is isolated, it becomes non-zero/x+1 (pruning).
   1. CA Filter C : Water Reduction
      1. Use Flood Fill to find 0 regions
      2. Reduce its size by a percentage and flipping the 0s to 1s
      3. Have a variable to determine percentage by user (0-100)
      4. Skip this filter if user defined percentage is less than 10%
      5. Bias reduction on edges to simulate evaporation

6. Hydrology Path Generation
   1. Each grid cell in the elevation map is a node in a graph
   2. Downhill Flow
      1. Assign movement cost to each path, using the raw noise for the height map to determine costs by slope descent
         1. Steeper slopes are higher cost
      2. For each node point to the lowest cost neighbor
   3. Flow Accumulation : Count the neighbors that flow into our tile as a flow value
      1. Use the high flow values to create major roads && the small values to create minor roads
         1. Done by marking walkable cells that can’t be placed with objects
7. Open Area Finder / Large Structure Locations
   1. Find areas that have flat ground tiles of 3x3 cells
   2. Elevation buffer, ensure structures aren’t placed against cliffs and ramps
   3. Store the center cell into a list of available locations
8. Poisson Object Placement
   1. Create a copy of the finalized grid and increase its size by splitting cells into 4 pieces
   2. Finalized grid contains data about each cell's place-ability
      1. Use the data from the previous steps to know cells that can’t have objects placed on them
      2. River Pathing determines non-placeable cells
      3. Elevation changes are non-placeable cell's
   3. Use poisson disk sampling to place non-large objects using the higher density grid
9. Tile Placement
   1. Using a Dual-Grid System
      1. Determine the correct tile type for each cell in the grid
   2. Set the Grid map cells using Godot's Grid map system

## Contributing

Any and all Contributions are subject to the [CLA.md](https://github.com/kthecoder/GaiaGreen/blob/main/CLA.md)
