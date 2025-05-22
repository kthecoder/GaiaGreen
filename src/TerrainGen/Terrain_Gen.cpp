#include "Terrain_Gen.h"

using namespace godot;

TerrainGen::TerrainGen() {
	noise.instantiate();
}

TerrainGen::~TerrainGen() {
}

void TerrainGen::generate(GridMap *myGridMap, int height, int width, int depth, int seed, bool useFractal, int noiseOctaves, float noiseFreq) {
	UtilityFunctions::print("Begin Terrain Generation!");

	/*****************************************************

		Setup the Noise Function

	*****************************************************/

	noise->set_noise_type(FastNoiseLite::NoiseType::TYPE_SIMPLEX);
	noise->set_fractal_type(useFractal ? FastNoiseLite::FractalType::FRACTAL_FBM : FastNoiseLite::FractalType::FRACTAL_NONE);
	noise->set_seed(seed);
	noise->set_fractal_octaves(noiseOctaves);
	noise->set_fractal_lacunarity(2.0);
	noise->set_fractal_gain(0.5);
	noise->set_frequency(noiseFreq);

	/*****************************************************

		Function dependent Variables

	*****************************************************/

	vector<vector<vector<int>>> gridMap(
			depth, vector<vector<int>>(height, vector<int>(width, 0)));

	vector<vector<float>> heightMap(height, vector<float>(width, 0.0f));

	/*****************************************************

		Height Map Generation

	*****************************************************/

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			heightMap[y][x] = noise->get_noise_2d((float)x, (float)y) / 2.0f * 0.5f;
		}
	}

	/*****************************************************

		Tile Placement

	*****************************************************/

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			float noiseValue = heightMap[x][y];
			int elevation = round(((noiseValue + 1.0f) / 2.0f) * depth); // Normalize to depth

			/*****************************************************

				Edge & Water Setter
				Encountering the edge of the Map, set the Tile to ground

			*****************************************************/

			bool isEdge = false;

			if (x == 0 || x == width - 1 || y == 0 || y == height - 1) {
				isEdge = true;
			}
			// Water assignment
			if (elevation == 1 && !isEdge) {
				myGridMap->set_cell_item(Vector3i(x, elevation, y), WATER, 0);
				continue;
			}

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
					neighborElevations[d] = heightMap[ny][nx];
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

			/*
				The key rotations (Orientation) in Godot's Grid Map around the Y-axis are:
				- 0 → No rotation
				- 1 → 90° clockwise around Y
				- 2 → 180° around Y
				- 3 → 90° counterclockwise around Y
				- 10 → 180° around Y (alternative basis)
				- 11 → 90° counterclockwise around Y (alternative basis)
				- 9 → 90° clockwise around Y (alternative basis)

				Which can be set with : `myGridMap->set_cell_item(Vector3i(x, y, z), meshID, orientation);`
			*/

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

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "GridMap", "height", "width", "depth", "seed", "useFBM", "noiseOctaves", "noiseFreq"), &TerrainGen::generate);
}