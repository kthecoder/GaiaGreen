#include "Terrain_Gen.h"

using namespace godot;

TerrainGen::TerrainGen() {
	noise.instantiate();
}

TerrainGen::~TerrainGen() {
}

void TerrainGen::generate(GridMap *myGridMap, int height, int width, int depth, int seed, int noiseType, int noiseOctaves, float jitter, float noiseFreq) {
	UtilityFunctions::print("Begin Terrain Generation!");

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

	// Noise Map may be used later, not sure yet
	// vector<vector<float>> noiseMap(width, vector<float>(height, 0.0f));

	vector<vector<int>> elevationMap(width, vector<int>(height, 0));
	vector<vector<int>> elevationMapSmooth(width, vector<int>(height, 0));

	/*****************************************************

		Noise Map Generation & Smoothing

	*****************************************************/

	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			float currentNoise = noise->get_noise_2d((float)x, (float)y);
			// noiseMap[x][y] = currentNoise;
			elevationMap[x][y] = round(((currentNoise + 1.0f) / 2.0f) * depth);
		}
	}

	// Apply a Median Filter
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			vector<int> neighbors;

			for (int dx = -1; dx <= 1; dx++) {
				for (int dy = -1; dy <= 1; dy++) {
					int nx = x + dx;
					int ny = y + dy;

					// Ensure indices are valid before adding to the list
					if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
						neighbors.push_back(elevationMap[nx][ny]);
					}
				}
			}

			if (!neighbors.empty()) {
				sort(neighbors.begin(), neighbors.end());
				elevationMapSmooth[x][y] = max(0, static_cast<int>(neighbors[neighbors.size() / 2]));
			}
		}
	}

	// Apply the smoothed elevation map
	elevationMap = elevationMapSmooth;

	/*****************************************************

		Tile Placement

	*****************************************************/

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
				int dx[4] = { 1, -1, 0, 0 };
				int dy[4] = { 0, 0, 1, -1 };

				for (int d = 0; d < 4; ++d) {
					int nx = x + dx[d];
					int ny = y + dy[d];

					if (nx >= 0 && ny >= 0 && nx < width && ny < height) {
						neighborElevations[d] = elevationMap[nx][ny];
					}
				}

				// Elevation analysis
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

				// Assign terrain type
				if (numDirectionChanges == 0) {
					tileType = GROUND;
				} else if (needsCliff) {
					tileType = CLIFF;
				} else if (needsRamp) {
					tileType = RAMP;
				}

				// Corner detection: Two direction elevation change
				if (numDirectionChanges >= 2) {
					needsCorner = true;
					if (tileType == RAMP)
						tileType = RAMP_CORNER;
					if (tileType == CLIFF)
						tileType = CLIFF_CORNER;
				}

				// Water Edge & Corner detection
				if (tileType == WATER) {
					if (numDirectionChanges == 1) {
						tileType = WATER_EDGE;
					} else if (numDirectionChanges >= 2) {
						tileType = WATER_CORNER;
					}
				}

				// Assign rotation using **0, 90, 180, 270** only
				if (tileType == RAMP || tileType == CLIFF || tileType == RAMP_CORNER || tileType == CLIFF_CORNER) {
					if (!needsCorner) {
						switch (dominantDirection) {
							case 0:
								rotationOrientation = 0; // No Rotation
								break; // North
							case 1:
								rotationOrientation = 1; // 90 Degrees Clockwise
								break; // East
							case 2:
								rotationOrientation = 2; // 180 Degrees Clockwise
								break; // South
							case 3:
								rotationOrientation = 3; // 270 Degrees Clockwise
								break; // West
						}
					} else {
						// Corner cases use the same **0, 90, 180, 270** logic.
						if (neighborElevations[0] != elevation && neighborElevations[2] != elevation) {
							rotationOrientation = 1; // Vertical corner (North-South elevation change)
						} else if (neighborElevations[1] != elevation && neighborElevations[3] != elevation) {
							rotationOrientation = 2; // Horizontal corner (East-West elevation change)
						} else if (neighborElevations[0] != elevation && neighborElevations[1] != elevation) {
							rotationOrientation = 0; // Corner facing North-East
						} else if (neighborElevations[2] != elevation && neighborElevations[3] != elevation) {
							rotationOrientation = 3; // Corner facing South-West
						}
					}
				}

				/*****************************************************

					Grid Cell Setter

				*****************************************************/

				myGridMap->set_cell_item(Vector3i(x, elevation, y), tileType, rotationOrientation);
			}
		}
	}
}

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "GridMap", "height", "width", "depth", "seed", "noiseType", "noiseOctaves", "jitter", "noiseFreq"), &TerrainGen::generate);
}