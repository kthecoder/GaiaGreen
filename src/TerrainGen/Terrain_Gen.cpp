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

	vector<vector<int>> elevationMap(width, vector<int>(height, 0));

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
	/*****************************************************

	Tile Placement

	*****************************************************/

	bool forceCliff = false; // Track cliff sections

	for (int x = 0; x < width; ++x) {
		for (int y = 0; y < height; ++y) {
			int elevation = elevationMap[x][y];

			TileType tileType = GROUND;

			bool needsRamp = false, needsCliff = false, adjacentToWater = false;

			int rotationOrientation = 0;
			int numElevationChanges = 0;
			int numDirectionChanges = 0;
			int maxElevationChange = 0;
			int waterEdgeNeighbors = 0;
			int waterNeighbors[4] = { 0, 0, 0, 0 };

			if (elevation == 0) {
				myGridMap->set_cell_item(Vector3i(x, elevation, y), WATER, 0);
				continue;
			}

			int neighborElevations[4] = { elevation, elevation, elevation, elevation };

			for (int d = 0; d < 4; ++d) {
				int nx = x + dx[d];
				int ny = y + dy[d];

				if (nx >= 0 && ny >= 0 && nx < width && ny < height) {
					neighborElevations[d] = elevationMap[nx][ny];
				}

				if (nx >= 0 && ny >= 0 && nx < width && ny < height) {
					neighborElevations[d] = elevationMap[nx][ny];
					if (neighborElevations[d] == 0) {
						adjacentToWater = true;
						waterNeighbors[d] = 1;
					}
				}
			}

			for (int d = 0; d < 4; ++d) {
				int diff = neighborElevations[d] - elevation;

				if (diff != 0)
					numDirectionChanges++;

				if (diff == 1 || diff == -1) {
					numElevationChanges++;
					maxElevationChange = max(maxElevationChange, abs(diff));

					float randomFactor = (float)rand() / RAND_MAX;

					if (forceCliff || randomFactor < 0.2) {
						tileType = CLIFF;
						forceCliff = true;
					} else {
						tileType = RAMP;
						forceCliff = false;
					}
				}
			}

			if (adjacentToWater) {
				waterEdgeNeighbors = waterNeighbors[0] + waterNeighbors[1] + waterNeighbors[2] + waterNeighbors[3];

				if (waterEdgeNeighbors == 1) {
					tileType = WATER_EDGE;
				} else if (
						(waterNeighbors[0] && waterNeighbors[1]) ||
						(waterNeighbors[1] && waterNeighbors[2]) ||
						(waterNeighbors[2] && waterNeighbors[3]) ||
						(waterNeighbors[3] && waterNeighbors[0])) {
					tileType = WATER_CORNER; // Two adjacent water edges form a corner
				}
			}

			if (tileType == CLIFF && forceCliff) {
				forceCliff = true;
			} else if (tileType == RAMP) {
				forceCliff = false;
			}

			if (numElevationChanges >= 2) {
				if (tileType == RAMP)
					tileType = RAMP_CORNER;
				if (tileType == CLIFF)
					tileType = CLIFF_CORNER;
			}

			if (tileType == WATER) {
				if (numDirectionChanges == 1) {
					tileType = WATER_EDGE;
				} else if (numDirectionChanges >= 2) {
					tileType = WATER_CORNER;
				}
			}

			if (tileType == RAMP || tileType == CLIFF || tileType == RAMP_CORNER || tileType == CLIFF_CORNER) {
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

			myGridMap->set_cell_item(Vector3i(x, elevation, y), tileType, rotationOrientation);
		}
	}
}

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "GridMap", "height", "width", "depth", "seed", "noiseType", "waterRemoval", "noiseFreq"), &TerrainGen::generate);
}