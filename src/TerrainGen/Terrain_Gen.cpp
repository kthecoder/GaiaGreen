#include "Terrain_Gen.h"

using namespace godot;

TerrainGen::TerrainGen() {
	noise.instantiate();
}

TerrainGen::~TerrainGen() {
}

void TerrainGen::generate(GridMap *myGridMap, int height, int width, int depth, int seed, int noiseType, double waterRemoval, float noiseFreq) {
	/*****************************************************

		Setup the Noise Function

	*****************************************************/
	noise->set_noise_type(static_cast<FastNoiseLite::NoiseType>(noiseType));
	noise->set_fractal_type(FastNoiseLite::FractalType::FRACTAL_NONE);
	noise->set_seed(seed);
	noise->set_frequency(noiseFreq);

	/*****************************************************

		Function dependent Variables

	*****************************************************/

	vector<vector<vector<int>>> gridMap(
			depth, vector<vector<int>>(width, vector<int>(height, 0)));

	vector<vector<float>> rawNoise(width, vector<float>(height, 0));
	vector<vector<int>> elevationMap(width, vector<int>(height, 0));
	vector<vector<TileType>> tileMap(width, vector<TileType>(height, GROUND));

	int blockSize = 4;
	int reducedX = floor(width / blockSize);
	int reducedY = floor(height / blockSize);
	vector<vector<int>> lowResMap(reducedX, vector<int>(reducedY, 0));

	int dx[4] = { 1, -1, 0, 0 };
	int dy[4] = { 0, 0, 1, -1 };

	/*****************************************************

		Noise Elevation Generation & Modification

	*****************************************************/

	// Generate the Elevation Map
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			float currentNoise = noise->get_noise_2d((float)x, (float)y);

			rawNoise[x][y] = currentNoise;
			// Normalize and scale noise to [0, depth]
			float normalizedNoise = (currentNoise + 1.0f) / 2.0f;
			elevationMap[x][y] = round(normalizedNoise * depth);
		}
	}

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
			lowResMap[x][y] = max(0, min(depth, quantizedLevel));
		}
	}

	// Upscale back to Original Size
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
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

	//Smoothing
	// 	Ensure elevation change's aren't in succession
	//TODO...

	// Water Clean Up
	for (int x = 1; x < width - 1; x++) {
		for (int y = 1; y < height - 1; y++) {
			if (elevationMap[x][y] > 0) { // Check if it's an elevated tile
				int waterCount = 0;

				// Check surrounding 3x3 neighbors
				for (int dx = -1; dx <= 1; dx++) {
					for (int dy = -1; dy <= 1; dy++) {
						if (elevationMap[x + dx][y + dy] == 0) {
							waterCount++;
						}
					}
				}

				// If it's surrounded by water, set elevation to 0
				if (waterCount >= 5) {
					elevationMap[x][y] = 0;
				}
			}
		}
	}

	/*****************************************************

	Tile Placement

	*****************************************************/

	// Loop over all the grid cells
	for (int x = 0; x < width; ++x) {
		for (int y = 0; y < height; ++y) {
			//Get the elevation for the current grid cell
			int elevation = elevationMap[x][y];

			//Init the current grid cell to ground
			tileMap[x][y] = GROUND;

			// Setup booleans & Ints
			bool needsRamp = false, needsCliff = false, adjacentToWater = false;

			int rotationOrientation = 0;
			int numElevationChanges = 0;
			int numDirectionChanges = 0;
			int maxElevationChange = 0;
			int waterEdgeNeighbors = 0;
			int waterNeighbors[4] = { 0, 0, 0, 0 };

			// If Elevation is 0, we get water tiles
			if (elevation == 0) {
				myGridMap->set_cell_item(Vector3i(x, elevation, y), WATER, 0);
				continue;
			}

			// Get the cardinal neighbors
			int neighborElevations[4] = { elevation, elevation, elevation, elevation };

			// Loop through and assign value to the cardinal neighbors
			for (int d = 0; d < 4; ++d) {
				int nx = x + dx[d];
				int ny = y + dy[d];

				if (nx >= 0 && ny >= 0 && nx < width && ny < height) {
					// Get Neighbors Elevation
					neighborElevations[d] = elevationMap[nx][ny];
					if (neighborElevations[d] == 0) {
						// If neighbor is water, store that info
						adjacentToWater = true;
						waterNeighbors[d] = 1;
					}
				}
			}

			// Loop through the grid cells cardinal neighbors again
			for (int d = 0; d < 4; ++d) {
				// Find the Elevation diff between neighbors
				int diff = neighborElevations[d] - elevation;

				// If not 0, then there is a change
				if (diff != 0)
					numDirectionChanges++;

				// If an elevation diff of 1
				if (diff == 1 || diff == -1) {
					//We have elevation change

					numElevationChanges++;
					maxElevationChange = max(maxElevationChange, abs(diff));

					float randomFactor = (float)rand() / RAND_MAX;

					// Set Cliffs or Ramps since Elevation has changed
					if (rawNoise[x][y] < 0.4) {
						tileMap[x][y] = CLIFF;
					} else {
						tileMap[x][y] = RAMP;
					}
				}
			}

			// If grid cell is adjacent to water
			if (adjacentToWater) {
				// Get the cardinal neighbors
				waterEdgeNeighbors = waterNeighbors[0] + waterNeighbors[1] + waterNeighbors[2] + waterNeighbors[3];

				// Decide if it needs to be a water edge or a water corner
				if (waterEdgeNeighbors == 1) {
					tileMap[x][y] = WATER_EDGE;
				} else if (
						(waterNeighbors[0] && waterNeighbors[1]) ||
						(waterNeighbors[1] && waterNeighbors[2]) ||
						(waterNeighbors[2] && waterNeighbors[3]) ||
						(waterNeighbors[3] && waterNeighbors[0])) {
					tileMap[x][y] = WATER_CORNER; // Two adjacent water edges form a corner
				}
			}

			if (tileMap[x][y] == RAMP || tileMap[x][y] == CLIFF || tileMap[x][y] == RAMP_CORNER || tileMap[x][y] == CLIFF_CORNER) {
				int highestNeighborIndex = -1;
				int secondaryNeighborIndex = -1;
				int highestNeighborElevation = elevation;
				int secondaryNeighborElevation = elevation;

				for (int d = 0; d < 4; ++d) {
					int diff = neighborElevations[d] - elevation;
					if (diff > 0) {
						if (diff > highestNeighborElevation - elevation) {
							secondaryNeighborIndex = highestNeighborIndex;
							secondaryNeighborElevation = highestNeighborElevation;
							highestNeighborElevation = neighborElevations[d];
							highestNeighborIndex = d;
						} else if (diff > secondaryNeighborElevation - elevation) {
							secondaryNeighborElevation = neighborElevations[d];
							secondaryNeighborIndex = d;
						}
					}
				}

				// Determine if we have a corner case (two adjacent neighbors at higher elevation)
				bool isCorner = (secondaryNeighborIndex != -1 && abs(neighborElevations[secondaryNeighborIndex] - elevation) > 0);

				// Apply rotation based on elevation direction
				if (highestNeighborIndex != -1) {
					if (!isCorner) {
						switch (highestNeighborIndex) {
							case 0:
								rotationOrientation = 0;
								break; // North
							case 1:
								rotationOrientation = 10;
								break; // East
							case 2:
								rotationOrientation = 16;
								break; // South
							case 3:
								rotationOrientation = 22;
								break; // West
						}
					} else {
						// Handle corner rotation logic
						if ((highestNeighborIndex == 0 && secondaryNeighborIndex == 1) ||
								(highestNeighborIndex == 1 && secondaryNeighborIndex == 0)) {
							rotationOrientation = 0; // Top-right corner
						} else if ((highestNeighborIndex == 2 && secondaryNeighborIndex == 3) ||
								(highestNeighborIndex == 3 && secondaryNeighborIndex == 2)) {
							rotationOrientation = 10; // Bottom-left corner
						} else if ((highestNeighborIndex == 0 && secondaryNeighborIndex == 3) ||
								(highestNeighborIndex == 3 && secondaryNeighborIndex == 0)) {
							rotationOrientation = 16; // Top-left corner
						} else if ((highestNeighborIndex == 1 && secondaryNeighborIndex == 2) ||
								(highestNeighborIndex == 2 && secondaryNeighborIndex == 1)) {
							rotationOrientation = 22; // Bottom-right corner
						}
					}
				}
			}

			/*****************************************************

				Grid Map Cell Setter

			*****************************************************/

			myGridMap->set_cell_item(Vector3i(x, elevation, y), tileMap[x][y], rotationOrientation);
		}
	}
}

TerrainGen::TileType TerrainGen::isCornerTile(int x, int y, vector<vector<TileType>> &tileMap) {
	if (x < 1 || y < 1 || x >= tileMap.size() - 1 || y >= tileMap[0].size() - 1)
		return GROUND; // Bounds check

	TileType TL = tileMap[x - 1][y - 1];
	TileType TR = tileMap[x + 1][y - 1];
	TileType BL = tileMap[x - 1][y + 1];
	TileType BR = tileMap[x + 1][y + 1];

	// Check diagonal patterns for L-shapes
	if ((TL == WATER && BR == WATER) || (TR == WATER && BL == WATER)) {
		return WATER_CORNER; // Water Corner
	}

	if ((TL == CLIFF && BR == CLIFF) || (TR == CLIFF && BL == CLIFF)) {
		return CLIFF_CORNER; // Cliff Corner
	}

	if ((TL == RAMP && BR == RAMP) || (TR == RAMP && BL == RAMP)) {
		return RAMP_CORNER; // Ramp Corner
	}

	return GROUND;
}

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "GridMap", "height", "width", "depth", "seed", "noiseType", "waterRemoval", "noiseFreq"), &TerrainGen::generate);
}