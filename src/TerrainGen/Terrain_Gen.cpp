#include "Terrain_Gen.h"

using namespace godot;

TerrainGen::TerrainGen() {
	noise.instantiate();
}

TerrainGen::~TerrainGen() {
}

void TerrainGen::generate(
		GridMap *myGridMap,
		int height,
		int width,
		int elevationMax, // i.e. Depth/Height
		int seed,
		int openAreaMin,
		int noiseType,
		double waterRemoval,
		float noiseFreq) {
	/*****************************************************

		Setup

	*****************************************************/
	struct Flat3x3s {
		int x, elevation, y;
	};
	vector<Flat3x3s> flatZones;

	// Raw Noise -> Stored for possible random value use
	vector<vector<float>> rawNoise(width, vector<float>(height, 0));
	// Data Grid -> Simply an Elevation Map double the size of our Render Grid
	vector<vector<int>> elevationMap(width * 2, vector<int>(height * 2, 0));
	// Real Grid
	vector<vector<int>> realGrid(width, vector<int>(height, 0));
	// Render Grid
	vector<vector<TileType>> tileMap(width, vector<TileType>(height, GROUND));

	// Dual Grid System Setup
	// Produce a 2x Grid for the Data Grid
	int widthx2 = width * 2;
	int heightx2 = height * 2;

	int blockSize = 4;
	int reducedX = floor(widthx2 / blockSize);
	int reducedY = floor(heightx2 / blockSize);
	vector<vector<int>> lowResMap(reducedX, vector<int>(reducedY, 0));

	int dx[4] = { 1, -1, 0, 0 };
	int dy[4] = { 0, 0, 1, -1 };

	/*****************************************************

		Setup the Noise Function

	*****************************************************/
	noise->set_noise_type(static_cast<FastNoiseLite::NoiseType>(noiseType));
	noise->set_fractal_type(FastNoiseLite::FractalType::FRACTAL_NONE);
	noise->set_seed(seed);
	noise->set_frequency(noiseFreq);

	/*****************************************************

		Produce Random Noise & Generate Height Map

			Generates Height map in the Doubled Grid Size

	*****************************************************/

	// Generate the Elevation Map
	for (int x = 0; x < widthx2; x++) {
		for (int y = 0; y < heightx2; y++) {
			float currentNoise = noise->get_noise_2d((float)x, (float)y);

			rawNoise[x][y] = currentNoise;
			// Normalize and scale noise to [0, elevationMax]
			float normalizedNoise = (currentNoise + 1.0f) / 2.0f;
			elevationMap[x][y] = round(normalizedNoise * elevationMax);
		}
	}

	/*****************************************************

		Noise Conversion

			Take normalized & scaled noise and convert it
			to a blocky square splotch pattern

	*****************************************************/

	// Reduce Resolution by sampling one pixel per block
	for (int x = 0; x < reducedX; x++) {
		for (int y = 0; y < reducedY; y++) {
			int sampleX = x * blockSize;
			int sampleY = y * blockSize;

			lowResMap[x][y] = elevationMap[sampleX][sampleY];
		}
	}

	// Posterize
	for (int x = 0; x < reducedX; x++) {
		for (int y = 0; y < reducedY; y++) {
			int quantizedLevel = round(lowResMap[x][y]);
			lowResMap[x][y] = max(0, min(elevationMax, quantizedLevel));
		}
	}

	// Upscale back to Original Size
	for (int x = 0; x < widthx2; x++) {
		for (int y = 0; y < heightx2; y++) {
			int srcX = x / blockSize;
			int srcY = y / blockSize;

			int elevationValue = lowResMap[srcX][srcY];

			float randomFactor = (float)rand() / RAND_MAX;
			if (elevationValue == 0 && randomFactor < waterRemoval) { // Chance to turn Water to Ground
				elevationMap[x][y] = 1;
			} else {
				elevationMap[x][y] = elevationValue;
			}
		}
	}

	/*****************************************************

		Cellular Automata Filter A

			Find flat 3x3 zones and expand

	*****************************************************/

	//
	// Phase One : Locate Natural 3x3 chunks
	//
	int openAreaCount = 0;
	while (openAreaCount < openAreaMin) {
		// Due to the Dual Grid Tile Assignment System
		// We really need to ensure the dual grid has 4x4 open areas
		// Which we will call 3x3's since they eventually become 3x3 tile chunks
		int FlatChunks3x3 = 0; // 3x3's (4x4 due to Dual Grid)
		int FlatChunks2x2 = 0; // 2x2's (3x3 due to Dual Grid)

		// Locate natural 3x3 Flat Ground Areas
		if (FlatChunks3x3 != -1) {
			int total3x3 = 0;
			// Start at 1,1 due to neighbor checks
			// Go over chunks, not every cell
			for (int x = 1; x < widthx2 - 4; x += 4) {
				for (int y = 1; y < heightx2 - 4; y += 4) {
					// n6 is our center cell
					// +----+----+----+----+
					// | n1 | n2 | n3 | n4 |
					// +----+----+----+----+
					// | n5 | n6 | n7 | n8 |
					// +----+----+----+----+
					// | n9 | n10 | n11 | n12 |
					// +----+----+----+----+
					// | n13 | n14 | n15 | n16 |
					// +----+----+----+----+
					int n6 = elevationMap[x][y];
					bool eqN1 = n6 == elevationMap[x - 1][y - 1];
					bool eqN2 = n6 == elevationMap[x][y - 1];
					bool eqN3 = n6 == elevationMap[x + 1][y - 1];
					bool eqN4 = n6 == elevationMap[x + 2][y - 1];
					bool eqN5 = n6 == elevationMap[x - 1][y];
					bool eqN7 = n6 == elevationMap[x + 1][y];
					bool eqN8 = n6 == elevationMap[x + 2][y];
					bool eqN9 = n6 == elevationMap[x - 1][y + 1];
					bool eqN10 = n6 == elevationMap[x][y + 1];
					bool eqN11 = n6 == elevationMap[x + 1][y + 1];
					bool eqN12 = n6 == elevationMap[x + 2][y + 1];
					bool eqN13 = n6 == elevationMap[x - 1][y + 2];
					bool eqN14 = n6 == elevationMap[x][y + 2];
					bool eqN15 = n6 == elevationMap[x + 1][y + 2];
					bool eqN16 = n6 == elevationMap[x + 2][y + 2];

					if (eqN1 && eqN2 && eqN3 && eqN4 && eqN5 && eqN7 && eqN8 &&
							eqN9 && eqN10 && eqN11 && eqN12 && eqN13 && eqN14 && eqN15 && eqN16) {
						total3x3 += 1;
						openAreaCount += 1;
						if (flatZones.size() >= openAreaMin)
							break;
					}
				}
			}

			// If None, deny entry back into if statement
			if (total3x3 == 0) {
				FlatChunks3x3 = -1;
			}
		}
	}

	//
	// Phase Two : Find 2x2's and Expand
	//

	//
	// Phase Three : Expand Single Cell's if openAreaMin still not satisfied
	//

	/*****************************************************

		Cellular Automata Filter B

			Patching / Removing Outliers

	*****************************************************/

	/*****************************************************

		Cellular Automata Filter C

			Water Reduction

	*****************************************************/

	/*****************************************************

		Hydrology Path Generation

			Using the RawNoise -> Downhill Flow, Flow Accumulation

			Define non-placeable cells for
			Poisson Disk Sampling

	*****************************************************/

	//
	// Phase One : Downhill Flow
	//
	//		Assign Movement Cost, Using slope descent,
	//		point to the lowest cost neighbor
	//

	//
	// Phase Two : Flow Accumulation
	//
	//		Using slope descent, point to the lowest cost
	//		neighbor
	//

	//
	// Phase X : Define Walkable Areas
	//
	//		Use the walkable area map later in
	//		Tile Placement to enforce Ramps over Cliffs
	//
	//		Determine path sizes based on Flow Accumulation
	//

	/*****************************************************

		Dual Grid Phase Two

			Reduce big data grid into real size

	*****************************************************/

	/*****************************************************

		Open Area Finder

			Find areas that have 3x3 flat areas
			Used for placing Large Structures

	*****************************************************/

	// TODO : Find Flat Zones
	// flatZones.push_back({ x, realGrid[x][y], y });

	/*****************************************************

		Poisson Object Placement

			TBD

	*****************************************************/

	/*****************************************************

	Tile Placement

		Using a dual grid system, determine the correct
		tile's for each cell

	*****************************************************/

	// Loop over all the grid cells
	for (int x = 0; x < width; ++x) {
		for (int y = 0; y < height; ++y) {
			// Godot GridMap Tile Rotation
			int tilesRotation = NORTH;

			// Neighbor's of Data Grid Map
			// +----+----+
			// | n1 | n2 |
			// +----+----+
			// | n3 | n4 |
			// +----+----+
			//
			// Get the surrounding DataGrid Neighbors overlap of the Render Grid
			int n1 = elevationMap[y][x];
			int n2 = elevationMap[y][x + 1];
			int n3 = elevationMap[y + 1][x];
			int n4 = elevationMap[y + 1][x + 1];

			// Determine the Elevation Value
			//
			//	Elevation of tile is the max of the neighbors elevation,
			//	assuming that the random noise is consistent in its spread
			//
			int elevation = max({ n1, n2, n3, n4 });

			// Determine Elevation Changes & Rotations & Tile Choice
			//
			// 	Elevation change needs to be known in Left, Right, Up, & Down
			//	due to the determination of a tiles type and rotation.
			//
			//	The tiles determine their type in relation to elevation due to
			//	their characteristics & setting water to elevation 0.
			//

			//-------------------------//
			// Basic Tiles
			//-------------------------//

			// Water's Water
			// +---+---+
			// | 0 | 0 |
			// +---+---+
			// | 0 | 0 |
			// +---+---+
			//
			if (n1 == 0 && n2 == 0 && n3 == 0 && n4 == 0) {
				tileMap[x][y] = WATER;
			}

			// Ground's Ground
			// +----+----+  +---+---+  +---+---+
			// | n1 | n2 |  | 1 | 1 |  | 2 | 2 |
			// +----+----+  +---+---+  +---+---+
			// | n3 | n4 |  | 1 | 1 |  | 2 | 2 |
			// +----+----+  +---+---+  +---+---+
			//
			if (n1 == n2 && n2 == n3 && n3 == n4 && n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0) {
				tileMap[x][y] = GROUND;
			}

			//-------------------------//
			// Water Edges
			//-------------------------//

			// Water's Edge South
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			if (n1 == 0 && n2 == 0 && n3 > 0 && n4 > 0) {
				tileMap[x][y] = WATER_EDGE;
				tilesRotation = SOUTH;
			}
			// Water's Edge North
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 0 | 0 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 == 0 && n4 == 0) {
				tileMap[x][y] = WATER_EDGE;
				tilesRotation = NORTH;
			}
			// Water's Edge East
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 0 | 1 |
			// +----+----+  +---+---+
			//
			else if (n1 == 0 && n3 == 0 && n2 > 0 && n4 > 0) {
				tileMap[x][y] = WATER_EDGE;
				tilesRotation = EAST;

			}
			// Water's Edge West
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 0 |
			// +----+----+  +---+---+
			//
			else if (n2 == 0 && n4 == 0 && n1 > 0 && n3 > 0) {
				tileMap[x][y] = WATER_EDGE;
				tilesRotation = WEST;
			}

			//-------------------------//
			// Water Corners
			//-------------------------//

			// Water's Corner North
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 0 | 1 |
			// +----+----+  +---+---+
			//
			if (n1 == 0 && n2 == 0 && n3 == 0 && n4 > 0) {
				tileMap[x][y] = WATER_CORNER;
				tilesRotation = NORTH;
			}
			// Water's Corner South
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 0 | 0 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 == 0 && n3 == 0 && n4 == 0) {
				tileMap[x][y] = WATER_CORNER;
				tilesRotation = SOUTH;
			}
			// Water's Corner East
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 0 | 0 |
			// +----+----+  +---+---+
			//
			else if (n1 == 0 && n2 > 0 && n3 == 0 && n4 == 0) {
				tileMap[x][y] = WATER_CORNER;
				tilesRotation = EAST;
			}
			// Water's Corner West
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 0 |
			// +----+----+  +---+---+
			//
			else if (n1 == 0 && n2 == 0 && n3 > 0 && n4 == 0) {
				tileMap[x][y] = WATER_CORNER;
				tilesRotation = WEST;
			}

			//-------------------------//
			// Cliff's & Ramp's Corner
			//-------------------------//

			//TODO : Decide how cliffs vs ramps are picked

			// Corner North
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 2 |
			// +----+----+  +---+---+
			//
			// TODO : Determine the way of choosing between cliffs and ramps
			if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n4 > n1 && n4 > n2 && n4 > n3) {
				tileMap[x][y] = CLIFF_CORNER;
				tilesRotation = NORTH;
			}
			// Corner South
			// +----+----+  +---+---+
			// | n1 | n2 |  | 2 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			// TODO : Determine the way of choosing between cliffs and ramps
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 > n2 && n1 > n3 && n1 > n4) {
				tileMap[x][y] = CLIFF_CORNER;
				tilesRotation = SOUTH;
			}
			// Corner East
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			// TODO : Determine the way of choosing between cliffs and ramps
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n2 > n1 && n2 > n3 && n2 > n4) {
				tileMap[x][y] = CLIFF_CORNER;
				tilesRotation = EAST;
			}
			// Corner East
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 0 |
			// +----+----+  +---+---+
			//
			// TODO : Determine the way of choosing between cliffs and ramps
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n3 > n1 && n3 > n2 && n3 > n4) {
				tileMap[x][y] = CLIFF_CORNER;
				tilesRotation = WEST;
			}

			//-------------------------//
			// Cliff's & Ramp's Edges
			//-------------------------//

			// Cliff or Ramp Edge North
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 2 | 2 |
			// +----+----+  +---+---+
			//
			// TODO : Determine the way of choosing between cliffs and ramps
			if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 < n3 && n1 < n4 && n2 < n4 && n2 < n3) {
				tileMap[x][y] = CLIFF;
				tilesRotation = NORTH;
			}
			// Cliff's Edge South
			// +----+----+  +---+---+
			// | n1 | n2 |  | 2 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			// TODO : Determine the way of choosing between cliffs and ramps
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 > n3 && n1 > n4 && n2 > n4 && n2 > n3) {
				tileMap[x][y] = CLIFF;
				tilesRotation = SOUTH;
			}
			// Cliff's Edge East
			// +----+----+  +---+---+
			// | n1 | n2 |  | 2 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 2 | 1 |
			// +----+----+  +---+---+
			//
			// TODO : Determine the way of choosing between cliffs and ramps
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 < n2 && n1 < n4 && n3 < n2 && n3 < n4) {
				tileMap[x][y] = CLIFF;
				tilesRotation = EAST;
			}
			// Cliff's Edge West
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 2 |
			// +----+----+  +---+---+
			//
			// TODO : Determine the way of choosing between cliffs and ramps
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 > n2 && n1 > n4 && n3 > n2 && n3 > n4) {
				tileMap[x][y] = CLIFF;
				tilesRotation = WEST;
			}

			/*****************************************************

				Grid Map Cell Setter

			*****************************************************/

			myGridMap->set_cell_item(Vector3i(x, elevation, y), tileMap[x][y], tilesRotation);
		}
	}

	//TODO : Another run through required to check adjacent tiles, especially tiles touching corner tiles. TO ensure a cliff corner connects to cliffs
}

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "GridMap", "height", "width", "elevationMax", "seed", "noiseType", "waterRemoval", "noiseFreq"), &TerrainGen::generate);
}