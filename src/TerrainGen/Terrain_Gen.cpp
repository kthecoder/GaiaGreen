#include "Terrain_Gen.h"

using namespace godot;

TerrainGen::TerrainGen() {
	noise.instantiate();
}

TerrainGen::~TerrainGen() {
}

void TerrainGen::generate(GridMap *myGridMap, int height, int width, int depth, int seed, int noiseType, int noiseOctaves, float jitter, float noiseFreq) {
	/*****************************************************

		Setup the Noise Function

	*****************************************************/
	noise->set_noise_type(static_cast<FastNoiseLite::NoiseType>(noiseType));
	noise->set_fractal_type(FastNoiseLite::FractalType::FRACTAL_NONE);
	noise->set_seed(seed);
	noise->set_fractal_octaves(noiseOctaves);
	noise->set_cellular_distance_function(FastNoiseLite::CellularDistanceFunction::DISTANCE_MANHATTAN);
	noise->set_cellular_jitter(jitter);
	noise->set_fractal_lacunarity(2.0);
	noise->set_fractal_gain(0.5);
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

		Noise Map Generation & Smoothing

	*****************************************************/

	// Generate the Elevation Map
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			float currentNoise = noise->get_noise_2d((float)x, (float)y);

			float bias = 0.1, stepSize = 4;
			int rawNoise = round(pow(((currentNoise + 1.0f) / 2.0f) + bias, 1.5) * depth);
			elevationMap[x][y] = round(rawNoise / stepSize) * stepSize;
		}
	}

	// Downsample by sampling one pixel per block.
	for (int x = 0; x < reducedX; x++) {
		for (int y = 0; y < reducedY; y++) {
			int sampleX = x * blockSize;
			int sampleY = y * blockSize;

			lowResMap[y][x] = (elevationMap[x][y] + 1.0f) / 2.0f;
		}
	}

	//Posterize
	for (int x = 0; x < reducedX; x++) {
		for (int y = 0; y < reducedY; y++) {
			int quantizedLevel = min(depth - 1, static_cast<int>(lowResMap[x][y] * depth));
			lowResMap[x][x] = quantizedLevel / static_cast<float>(depth - 1);
		}
	}

	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			int srcX = x / blockSize;
			int srcY = y / blockSize;
			elevationMap[x][y] = lowResMap[srcX][srcY];
		}
	}

	for (int x = 0; x < width; ++x) {
		for (int y = 0; y < height; ++y) {
			int elevation = elevationMap[x][y];
			/*****************************************************

				Edge & Water Setter
				Encountering the edge of the Map, set the Tile to ground

			*****************************************************/

			bool isEdge = false;

			if (x == 0 || x == width - 1 || y == 0 || y == height - 1) {
				isEdge = true;
			}
			// Water assignment
			if (elevation == 0 && !isEdge) {
				myGridMap->set_cell_item(Vector3i(x, elevation, y), WATER, 0);
				continue;
			} else {
				/*****************************************************

					Elevation Change & Rotation Detection

				*****************************************************/

				int neighborElevations[4] = { elevation, elevation, elevation, elevation };

				for (int d = 0; d < 4; ++d) {
					int nx = x + dx[d];
					int ny = y + dy[d];

					if (nx >= 0 && ny >= 0 && nx < width && ny < height) {
						neighborElevations[d] = elevationMap[nx][ny];
					}
				}

				TileType tileType = GROUND;
				int rotationOrientation = 0;
				bool needsRamp = false, needsCliff = false, needsCorner = false;
				int numDirectionChanges = 0;
				int dominantDirection = -1;
				int maxElevationChange = 0;

				for (int d = 0; d < 4; ++d) {
					int diff = neighborElevations[d] - elevation;
					if (diff != 0)
						numDirectionChanges++;
					if (diff == 1 || diff == -1)
						needsRamp = true;
					if (abs(diff) > 1)
						needsCliff = true;
					if (abs(diff) > maxElevationChange) {
						maxElevationChange = abs(diff);
						dominantDirection = d;
					}
				}

				if (numDirectionChanges == 0) {
					tileType = GROUND;
				} else if (needsCliff) {
					tileType = CLIFF;
				} else if (needsRamp) {
					tileType = RAMP;
				}

				if (numDirectionChanges >= 2) {
					needsCorner = true;
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
					int highestNeighborElevation = elevation;
					int secondaryNeighborIndex = -1;

					for (int d = 0; d < 4; ++d) {
						if (neighborElevations[d] > highestNeighborElevation) {
							secondaryNeighborIndex = highestNeighborIndex;
							highestNeighborElevation = neighborElevations[d];
							highestNeighborIndex = d;
						}
					}

					bool isCorner = (secondaryNeighborIndex != -1 && abs(neighborElevations[secondaryNeighborIndex] - elevation) > 0);

					if (highestNeighborIndex != -1) {
						if (!isCorner) {
							switch (highestNeighborIndex) {
								case 0:
									rotationOrientation = 2;
									break; // North
								case 1:
									rotationOrientation = 3;
									break; // East
								case 2:
									rotationOrientation = 0;
									break; // South
								case 3:
									rotationOrientation = 1;
									break; // West
							}
						} else {
							if ((highestNeighborIndex == 0 && secondaryNeighborIndex == 1) ||
									(highestNeighborIndex == 1 && secondaryNeighborIndex == 0)) {
								rotationOrientation = 0;
							} else if ((highestNeighborIndex == 2 && secondaryNeighborIndex == 3) ||
									(highestNeighborIndex == 3 && secondaryNeighborIndex == 2)) {
								rotationOrientation = 3;
							} else if ((highestNeighborIndex == 0 && secondaryNeighborIndex == 3) ||
									(highestNeighborIndex == 3 && secondaryNeighborIndex == 0)) {
								rotationOrientation = 1;
							} else if ((highestNeighborIndex == 1 && secondaryNeighborIndex == 2) ||
									(highestNeighborIndex == 2 && secondaryNeighborIndex == 1)) {
								rotationOrientation = 2;
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
}

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "GridMap", "height", "width", "depth", "seed", "noiseType", "noiseOctaves", "jitter", "noiseFreq"), &TerrainGen::generate);
}