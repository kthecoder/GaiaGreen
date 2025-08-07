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

	// int dx[4] = { 1, -1, 0, 0 };
	// int dy[4] = { 0, 0, 1, -1 };

	//	Dual Grid System Setup
	//
	// 	  Produce a 2x Grid for the Data Grid
	//
	int widthx2 = width * 2;
	int heightx2 = height * 2;

	//
	// Noise Conversion Variables
	//
	int blockSize = 4;
	int reducedX = floor(widthx2 / blockSize);
	int reducedY = floor(heightx2 / blockSize);

	//
	// Open Area Tracker
	//
	struct Flat3x3s {
		int x, elevation, y;
	};
	vector<Flat3x3s> flatZones;

	//
	// Hydrology Path System
	//
	struct FlowCell {
		int flowToX = -1;
		int flowToY = -1;
		float slope = 0.0f;
	};

	float totalFlow = 0.0f;
	int flowCount = 0;

	vector<vector<FlowCell>> flowMap(widthx2, vector<FlowCell>(heightx2));
	vector<vector<int>> flowAccumulation(widthx2, vector<int>(heightx2, 1));
	vector<vector<vector<pair<int, int>>>> inflowMap(widthx2, vector<vector<pair<int, int>>>(heightx2));
	vector<vector<bool>> walkableMap(widthx2, vector<bool>(heightx2, true)); // Default: walkable

	//
	// Management Grids
	//
	//		Track: Low Res Map, Raw Noise, Elevation Map, Render Grid Tile Type
	//

	// Stores the Lower Resolution map for Noise Conversion
	vector<vector<int>> lowResMap(reducedX, vector<int>(reducedY, 0));
	// Raw Noise -> Stored for possible random value use
	vector<vector<float>> rawNoise(width, vector<float>(height, 0));
	// Data Grid -> Simply an Elevation Map double the size of our Render Grid
	vector<vector<int>> elevationMap(width * 2, vector<int>(height * 2, 0));
	// Render Grid -> Real size grid with final tile type values
	vector<vector<TileType>> tileMap(width, vector<TileType>(height, GROUND));

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

		// TODO
		// Phase Two : Find 2x2's and Expand
		//

		// TODO
		// Phase Three : Expand Single Cell's if openAreaMin still not satisfied
		//
	}

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
	// Phase 1 : Downhill Flow
	//
	//		Assign Movement Cost, Using slope descent,
	//		point to the lowest cost neighbor
	//

	for (int x = 1; x < widthx2 - 1; x++) {
		for (int y = 1; y < heightx2 - 1; y++) {
			float currentElevation = rawNoise[x][y];
			float lowestElevation = currentElevation;
			int targetX = x;
			int targetY = y;

			// Check 8 neighbors
			for (int dx = -1; dx <= 1; dx++) {
				for (int dy = -1; dy <= 1; dy++) {
					if (dx == 0 && dy == 0)
						continue;

					int nx = x + dx;
					int ny = y + dy;
					float neighborElevation = rawNoise[nx][ny];

					if (neighborElevation < lowestElevation) {
						lowestElevation = neighborElevation;
						targetX = nx;
						targetY = ny;
					}
				}
			}

			flowMap[x][y].flowToX = targetX;
			flowMap[x][y].flowToY = targetY;
			flowMap[x][y].slope = currentElevation - lowestElevation;
		}
	}

	//
	// Phase 2 : Flow Accumulation
	//
	//		Using slope descent, point to the lowest cost
	//		neighbor
	//

	// Inflow Map Builder
	//
	//	 Who flows into each cell
	for (int x = 1; x < widthx2 - 1; ++x) {
		for (int y = 1; y < heightx2 - 1; ++y) {
			int tx = flowMap[x][y].flowToX;
			int ty = flowMap[x][y].flowToY;
			if (tx != x || ty != y) {
				inflowMap[tx][ty].emplace_back(x, y);
			}
		}
	}

	// Flow Accumulation Tracker
	//
	//	 Find flow by iterating over all cells
	//
	for (int x = 1; x < widthx2 - 1; ++x) {
		for (int y = 1; y < heightx2 - 1; ++y) {
			for (auto &upstream : inflowMap[x][y]) {
				int ux = upstream.first;
				int uy = upstream.second;
				flowAccumulation[x][y] += flowAccumulation[ux][uy];
			}

			totalFlow += flowAccumulation[x][y];
			++flowCount;
		}
	}

	float averageFlow = (flowCount > 0) ? totalFlow / flowCount : 0.0f;

	//
	// Phase 3 : Define Walkable Areas
	//
	//		Using the flow paths & accumulation counts
	//		determine walkable area's on the map
	//
	//		Used later in Tile Placement to enforce
	//		Ramps over Cliffs
	//
	//		Determine path sizes based on Flow Accumulation
	//		Mark more cells in perpendicular direction to flow direction
	//		for cells with larger accumulation
	//

	int maxRiverWidth = 3; // Max number of perpendicular cells (to river flow direction) to mark

	for (int x = 1; x < widthx2 - 1; x++) {
		for (int y = 1; y < heightx2 - 1; y++) {
			float flow = flowAccumulation[x][y];
			if (flow < averageFlow)
				continue; // Only mark above-average flow cells

			walkableMap[x][y] = false;

			int dx = flowMap[x][y].flowToX - x;
			int dy = flowMap[x][y].flowToY - y;

			int perpX1 = -dy;
			int perpY1 = dx;
			int perpX2 = dy;
			int perpY2 = -dx;

			int riverWidth = min(maxRiverWidth, static_cast<int>(flow / averageFlow));

			for (int w = 1; w <= riverWidth; w++) {
				int px1 = x + perpX1 * w;
				int py1 = y + perpY1 * w;
				int px2 = x + perpX2 * w;
				int py2 = y + perpY2 * w;

				if (px1 > 0 && px1 < widthx2 && py1 > 0 && py1 < heightx2)
					walkableMap[px1][py1] = false;

				if (px2 > 0 && px2 < widthx2 && py2 > 0 && py2 < heightx2)
					walkableMap[px2][py2] = false;
			}
		}
	}

	//
	// Phase 4 : Setting Walkable Area's
	//
	//		Modify Elevation Map to ensure walkable area's are expressed
	//
	//		If the majority of cells in a 2x2 are marked walkable,
	//		ensure elevation map's neighbors are equal value
	//

	/*****************************************************

	Tile Placement

		Using a dual grid system, determine the correct
		tile's for each cell

	*****************************************************/

	// Loop over all the grid cells
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
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

	/*****************************************************

		Open Area Finder

			Find areas that have 3x3 flat areas
			Used for placing Large Structures

	*****************************************************/

	for (int i = 0; i <= width - 3; i += 3) {
		for (int j = 0; j <= height - 3; j += 3) {
			bool allGround = true;

			// Check Neighbors
			for (int dx = 0; dx < 3 && allGround; dx++) {
				for (int dy = 0; dy < 3; dy++) {
					if (tileMap[i + dx][j + dy] != GROUND) {
						allGround = false;
						break;
					}
				}
			}

			if (allGround) {
				int centerX = i + 1;
				int centerY = j + 1;
				int elevation = elevationMap[centerX][centerY];

				flatZones.push_back({ centerX, elevation, centerY });
			}
		}
	}

	/*****************************************************

		Poisson Object Placement

			TBD

	*****************************************************/
}

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "GridMap", "height", "width", "elevationMax", "seed", "noiseType", "waterRemoval", "noiseFreq"), &TerrainGen::generate);
}