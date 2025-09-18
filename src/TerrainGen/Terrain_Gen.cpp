#include "Terrain_Gen.h"

using namespace godot;

TerrainGen::TerrainGen() {
	noise.instantiate();
}

TerrainGen::~TerrainGen() {
}

Dictionary TerrainGen::generate(
		GridMap *myGridMap,
		int height,
		int width,
		int elevationMax, // i.e. Depth/Height
		int seed,
		int provinceSize,
		int noiseType,
		double waterRemoval,
		float cliffThreshold,
		float noiseFreq) {
	/*****************************************************

		Setup

	*****************************************************/
	default_random_engine rng(seed);

	int dx[4] = { 1, -1, 0, 0 };
	int dy[4] = { 0, 0, 1, -1 };

	if (elevationMax != 0) {
		int remH = height % elevationMax;
		if (remH != 0)
			height += elevationMax - remH;

		int remW = width % elevationMax;
		if (remW != 0)
			width += elevationMax - remW;
	}

	//	Dual Grid System Setup
	//
	// 	  Produce a 2x Grid for the Data Grid
	//
	int widthx2 = width * 2;
	int heightx2 = height * 2;

	int reducedX = floor(widthx2 / elevationMax);
	int reducedY = floor(heightx2 / elevationMax);

	//
	// Large Object Placement Map
	//

	vector<OpenAreas> flatZones;
	vector<vector<bool>> isFlat(widthx2, vector<bool>(heightx2, false));

	//
	// Hydrology Path System
	//

	float totalFlow = 0.0f;
	int flowCount = 0;

	vector<vector<FlowCell>> flowMap(widthx2, vector<FlowCell>(heightx2));
	vector<vector<int>> flowAccumulation(widthx2, vector<int>(heightx2, 1));
	vector<vector<vector<pair<int, int>>>> inflowMap(widthx2, vector<vector<pair<int, int>>>(heightx2));
	vector<vector<bool>> walkableMap(widthx2, vector<bool>(heightx2, true)); // Default: walkable

	//
	// Poisson Disk Sampling
	//

	vector<Point> poissonSamples;

	//
	// Management Grids
	//
	//		Track: Low Res Map, Raw Noise, Elevation Map, Render Grid Tile Type
	//

	// Stores the Lower Resolution map for Noise Conversion
	vector<vector<int>> lowResMap(reducedX, vector<int>(reducedY, 0));
	// Raw Noise -> Stored for possible random value use
	vector<vector<float>> rawNoise(widthx2, vector<float>(heightx2, 0));
	// Data Grid -> Simply an Elevation Map double the size of our Render Grid
	vector<vector<int>> heightMap(width * 2, vector<int>(height * 2, 0));
	// Lock Grid -> Lock Cell's that are known to not change
	vector<vector<bool>> lockMap(width * 2, vector<bool>(height * 2, false));
	// Render Grid -> Real size grid with final tile type values
	vector<vector<TileType>> tileMap(width, vector<TileType>(height, GROUND));
	// Render Grid's Elevation Values -> Real size grid with elevation ints
	vector<vector<int>> elevationMap(width, vector<int>(height, 0)); // Unfiltered Elevation | Includes 0 Elevations
	vector<vector<int>> elevationMapTiles(width, vector<int>(height, 0)); // Filtered Elevation | Used Elevation in Tile Map
	// Each cell represents a quarter of the original tile
	vector<vector<bool>> placeableMap(widthx2, vector<bool>(heightx2, true));
	// Water Positions
	vector<pair<int, int>> waterPoints;

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

	generate_blocky_heightmap(
			noise,
			widthx2, heightx2,
			reducedX, reducedY,
			elevationMax,
			rawNoise,
			heightMap,
			lockMap,
			lowResMap,
			waterPoints,
			rng);

	/*****************************************************

		Lock Borders

			Outer Border's should remain untouched by later
			code.

			This ensures edges are quickly assigned, enabled
			the ability to pass edges to parallelization, if
			desired.

	*****************************************************/

	lockOuterBorder(lockMap);

	/*****************************************************

		Province Point Selection

			Large Object Placement points.
			Split the Map into chunks & ensure open area
			around chosen cell.

	*****************************************************/

	generate_province_points(
			width,
			height,
			provinceSize,
			heightMap,
			lockMap,
			widthx2,
			heightx2,
			flatZones,
			rng);

	/*****************************************************

		Water Generation

			Lakes & River Generation

	*****************************************************/
	generate_lakes(
			heightMap,
			lockMap,
			waterPoints,
			rng,
			0, // minLakes
			10, // maxLakes
			9, // minLakeSize
			200); // maxLakeSize

	generate_rivers(
			heightMap,
			lockMap,
			waterPoints,
			rng,
			0, // minRivers
			9, // maxRivers
			20, // minRiverSize
			200); // maxRiverSize

	/*****************************************************

		Hydrology Path Generation

			Using the RawNoise -> Downhill Flow, Flow Accumulation

			Define non-placeable cells for
			Poisson Disk Sampling

	*****************************************************/

	compute_flow_and_walkable_areas(
			heightMap,
			flowMap,
			inflowMap,
			flowAccumulation,
			walkableMap,
			widthx2,
			heightx2);

	/*****************************************************

		Smoothing

			After modifications to the base elevation
			Height map, ensure that the elevation changes
			are no greater than 1 int in any direction.

			This ensures that the map can have meaningful
			topology conveyed all within the height map.
			So that tile placement can accurately determine
			the correct tile type and rotation.

	*****************************************************/

	smooth_2xElevation_map(heightMap, lockMap);

	/*****************************************************

		Tile Placement

			Using a dual grid system, determine the correct
			tile's for each cell

	*****************************************************/

	//
	// Phase 1 : Determine the Tile Type
	//
	//	Tiles are determined based on the dual grid system
	//

	determine_tile_types(
			width,
			height,
			heightMap,
			rawNoise,
			elevationMap,
			elevationMapTiles,
			tileMap,
			walkableMap,
			cliffThreshold,
			myGridMap);

	//
	//
	// Phase 2 : Corrections & Rotations
	//
	//  Description: Determine the Tile's Rotation and correct tiles
	//
	// 	Models: Ramp Corner's/ Cliff Corner's / Water Corner's start with HIGH at (−Z, +X)
	//				i.e., NE corner.
	//			Ramps / Cliffs / Water Edge's point at -Z (North)
	//
	// T = Target Cell / Target Tile
	// +----+----+----+
	// | m1 | m2 | m3 |
	// +----+----+----+
	// | m4 | T  | m5 |
	// +----+----+----+
	// | m6 | m7 | m8 |
	// +----+----+----+
	//

	apply_tile_rotations_and_fixes(
			width,
			height,
			elevationMap,
			myGridMap);

	/*****************************************************

		Poisson Object Placement

			Use Walkable map to find non-walkable area's as placeable area's
			Use Cliffs & Ramp's Placement as non-placeable area's

	*****************************************************/

	generate_placeable_areas_and_samples(
			width,
			height,
			widthx2,
			heightx2,
			walkableMap,
			tileMap,
			placeableMap,
			poissonSamples,
			3.0f // Minimum Distance
	);

	/*****************************************************

		Return's

			Return a dictionary of values needed for object
			placement

			Return : Elevation Map
				Necessary for height of objects

			Return : Flat Zones
				Necessary for placing large stuctures

			Return : Poisson Points

	*****************************************************/

	Dictionary result;

	// Convert poissonSamples → Array<Vector3i>
	Array poissonPoints;
	for (size_t i = 0; i < poissonSamples.size(); ++i) {
		int px = poissonSamples[i].x;
		int py = poissonSamples[i].y;
		int ex = px / 2;
		int ey = py / 2;

		if (ex >= 0 && ex < width && ey >= 0 && ey < height) {
			int elevation = elevationMap[ex][ey];
			Vector3i point(px, elevation, py);
			poissonPoints.push_back(point);
		}
	}
	result["poissonPoints"] = poissonPoints;

	// Convert flatZones → Array<Dictionary>
	Array flatZonePoints;
	for (size_t i = 0; i < flatZones.size(); ++i) {
		int fx = flatZones[i].x;
		int fy = flatZones[i].y;
		int elevation = flatZones[i].elevation;

		Vector3i point(fx, elevation, fy);
		flatZonePoints.push_back(point);
	}
	result["flatZones"] = flatZonePoints;

	// Convert elevationMap → Array<Array<int>>
	Array elevationArray;
	for (size_t i = 0; i < elevationMap.size(); ++i) {
		Array inner;
		for (size_t j = 0; j < elevationMap[i].size(); ++j) {
			int val = elevationMap[i][j];
			inner.push_back(val);
		}
		elevationArray.push_back(inner);
	}
	result["elevationMap"] = elevationArray;

	return result;
}

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "GridMap", "height", "width", "elevationMax", "seed", "provinceSize", "noiseType", "waterRemoval", "cliffsThreshold", "noiseFreq"), &TerrainGen::generate);
}

void TerrainGen::generate_blocky_heightmap(
		godot::Ref<godot::FastNoiseLite> &noise,
		const int &widthx2, const int &heightx2,
		const int &reducedX, const int &reducedY,
		const int &elevationMax,
		vector<vector<float>> &rawNoise,
		vector<vector<int>> &myHeightMap,
		vector<vector<bool>> &myLockMap,
		vector<vector<int>> &lowResMap,
		vector<pair<int, int>> &waterPoints,
		default_random_engine &rng) {
	// Generate the Elevation Map
	for (int x = 0; x < widthx2; x++) {
		for (int y = 0; y < heightx2; y++) {
			float currentNoise = noise->get_noise_2d((float)x, (float)y);

			rawNoise[x][y] = currentNoise;
			// Normalize and scale noise to [0, elevationMax]
			float normalizedNoise = (currentNoise + 1.0f) / 2.0f;
			myHeightMap[x][y] = round(normalizedNoise * elevationMax);
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
			int sampleX = x * elevationMax;
			int sampleY = y * elevationMax;

			lowResMap[x][y] = myHeightMap[sampleX][sampleY];
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
			int srcX = x / elevationMax;
			int srcY = y / elevationMax;

			int elevationValue = lowResMap[srcX][srcY];

			uniform_real_distribution<float> dist(0.0f, 1.0f);
			float randomFactor = dist(rng);

			// Find all Water Grid Positions
			if (elevationValue == 0) {
				// Set all Water to Ground
				myHeightMap[x][y] = 1;
				waterPoints.emplace_back(x, y);
			}
			// If not Water, set to elevation Value
			else {
				myHeightMap[x][y] = elevationValue;
			}
		}
	}
}

void TerrainGen::smooth_2xElevation_map(
		vector<vector<int>> &myHeightMap,
		const vector<vector<bool>> &lockMap) {
	int width = static_cast<int>(myHeightMap.size());
	int height = static_cast<int>(myHeightMap[0].size());

	bool changed;
	do {
		changed = false;

		for (int x = 0; x < width; ++x) {
			for (int y = 0; y < height; ++y) {
				if (lockMap[x][y])
					continue; // skip locked cells

				int current = myHeightMap[x][y];

				// Check 4-neighbors (N,S,E,W). Could extend to 8 if desired.
				const int dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

				for (auto &d : dirs) {
					int nx = x + d[0];
					int ny = y + d[1];
					if (nx < 0 || ny < 0 || nx >= width || ny >= height)
						continue;

					int neighbor = myHeightMap[nx][ny];
					int diff = current - neighbor;

					if (diff > 1) {
						// Too high compared to neighbor → lower it
						myHeightMap[x][y] = neighbor + 1;
						changed = true;
					} else if (diff < -1) {
						// Too low compared to neighbor → raise it
						myHeightMap[x][y] = neighbor - 1;
						changed = true;
					}
				}
			}
		}
	} while (changed);
}

void TerrainGen::generate_province_points(
		int width,
		int height,
		int provinceSize,
		vector<vector<int>> &myHeightMap,
		vector<vector<bool>> &myLockMap,
		int widthx2,
		int heightx2,
		vector<OpenAreas> &flatZones,
		default_random_engine &rng) {
	if (provinceSize == 0) {
		UtilityFunctions::print("Gaia Green ERROR | Province Size cannot be 0");
		return;
	}

	const int borderMargin = 6;
	int effectiveWidth = width - (borderMargin * 2); // exclude 6 on each side
	int effectiveHeight = height - (borderMargin * 2);

	if (effectiveWidth <= 0 || effectiveHeight <= 0) {
		UtilityFunctions::print("Gaia Green ERROR | Map too small for border margin");
		return;
	}

	int sectorSize = static_cast<int>(floor(
			sqrt((effectiveWidth * effectiveHeight) / static_cast<double>(provinceSize))));

	if (sectorSize <= 0)
		sectorSize = 1;

	int sectorsAcross = effectiveWidth / sectorSize;
	int sectorsDown = effectiveHeight / sectorSize;
	int totalSectors = sectorsAcross * sectorsDown;

	// Create a list of all sector indices
	vector<int> sectorIndices(totalSectors);
	for (int i = 0; i < totalSectors; ++i) {
		sectorIndices[i] = i;
	}

	// Shuffle the sector indices
	shuffle(sectorIndices.begin(), sectorIndices.end(), rng);

	int flatCount = provinceSize;
	int sectorCursor = 0;

	// Randomly pick a point within each chunk
	while (flatCount > 0 && sectorCursor < totalSectors) {
		int index = sectorIndices[sectorCursor++];

		int sectorX = index % sectorsAcross;
		int sectorY = index / sectorsAcross;

		int sectorStartX = sectorX * sectorSize;
		int sectorStartY = sectorY * sectorSize;

		uniform_int_distribution<int> dist(0, sectorSize - 1);

		int offsetX = dist(rng);
		int offsetY = dist(rng);

		int xPos = sectorStartX + offsetX + borderMargin;
		int yPos = sectorStartY + offsetY + borderMargin;

		int centerX = xPos * 2;
		int centerY = yPos * 2;

		int provinceElevation = myHeightMap[centerX][centerY];
		if (provinceElevation == 0) {
			provinceElevation = 1;
		}

		OpenAreas provincePoint = { xPos, provinceElevation, yPos };

		if (find(flatZones.begin(), flatZones.end(), provincePoint) == flatZones.end()) {
			flatZones.push_back(provincePoint);
			flatCount--;

			myLockMap[xPos][yPos] = true; // Lock the Cell

			const int flatRadiusCells = 3;

			// Flatten the Area around the Random Point
			for (int dx = -flatRadiusCells; dx <= flatRadiusCells; ++dx) {
				for (int dy = -flatRadiusCells; dy <= flatRadiusCells; ++dy) {
					int x = centerX + dx;
					int y = centerY + dy;

					if (x >= 0 && y >= 0 && x < width && y < height) {
						myHeightMap[x][y] = provinceElevation;
						myLockMap[x][y] = true; // Lock the Surrounding Cell
					}
				}
			}
		}
	}
}

void TerrainGen::generate_lakes(
		vector<vector<int>> &myHeightMap,
		vector<vector<bool>> &myLockMap,
		vector<pair<int, int>> &waterPoints,
		default_random_engine &rng,
		int minLakes = 0,
		int maxLakes = 10,
		int minLakeSize = 9,
		int maxLakeSize = 200) {
	try {
		uniform_int_distribution<int> lakeDist(minLakes, maxLakes);
		int numOfLakes = lakeDist(rng);

		for (int lakes = 0; lakes < numOfLakes; lakes++) {
			uniform_int_distribution<int> lakeSizeDist(minLakeSize, maxLakeSize);
			int sizeOfLakes = lakeSizeDist(rng);

			if (waterPoints.empty())
				break;

			// Pick a random starting point
			uniform_int_distribution<int> indexDist(0, static_cast<int>(waterPoints.size()) - 1);
			int randIndex = indexDist(rng);
			pair<int, int> currPos = waterPoints[randIndex];
			waterPoints.erase(waterPoints.begin() + randIndex);

			vector<pair<int, int>> localWater;
			localWater.insert(localWater.begin(), currPos);

			for (int j = 0; j < 1000; ++j) {
				pair<int, int> waterPos = localWater.front();

				uniform_int_distribution<int> binaryDist(0, 1);
				int basePos = binaryDist(rng);
				int secPos = basePos / 2;

				uniform_int_distribution<int> offsetDist(-1, 1);
				int thirdPos = (basePos % 2 == 0) ? 0 : offsetDist(rng);
				int fourthPos = (basePos % 2 == 0) ? 0 : offsetDist(rng);

				for (int k = waterPos.first - secPos + thirdPos;
						k <= waterPos.first - secPos + thirdPos + basePos; ++k) {
					for (int l = waterPos.second - secPos + fourthPos;
							l <= waterPos.second - secPos + fourthPos + basePos; ++l) {
						if (k >= 0 && k < static_cast<int>(myHeightMap.size()) &&
								l >= 0 && l < static_cast<int>(myHeightMap[0].size())) {
							if (!myLockMap[k][l]) {
								myHeightMap[k][l] = 0;
							}

							// Smooth surrounding area
							int radius = 8;
							for (int nk = -radius; nk <= radius; ++nk) {
								for (int nl = -radius; nl <= radius; ++nl) {
									if (nk == 0 && nl == 0)
										continue;

									int neighborX = k + nk;
									int neighborY = l + nl;

									if (neighborX >= 0 && neighborX < static_cast<int>(myHeightMap.size()) &&
											neighborY >= 0 && neighborY < static_cast<int>(myHeightMap[0].size())) {
										int &neighborElevation = myHeightMap[neighborX][neighborY];

										if (!myLockMap[neighborX][neighborY]) {
											int diff = myHeightMap[k][l] - neighborElevation;
											if (diff > 1)
												neighborElevation = myHeightMap[k][l] - 1;
											else if (diff < -1)
												neighborElevation = myHeightMap[k][l] + 1;
										}
									}
								}
							}
						}
					}
				}

				sizeOfLakes--;
				if (sizeOfLakes <= 0)
					break;

				pair<int, int> myNeighbor = waterPos;
				vector<int> cardinals = { 0, 1, 2, 3 };

				for (int m = 0; m < 4; ++m) {
					uniform_int_distribution<int> cardinalDist(0, static_cast<int>(cardinals.size()) - 1);
					int idx = cardinalDist(rng);
					int direction = cardinals[idx];
					cardinals.erase(cardinals.begin() + idx);

					switch (direction) {
						case 0:
							myNeighbor = { waterPos.first + 1, waterPos.second };
							break; // East
						case 1:
							myNeighbor = { waterPos.first - 1, waterPos.second };
							break; // West
						case 2:
							myNeighbor = { waterPos.first, waterPos.second + 1 };
							break; // South
						case 3:
							myNeighbor = { waterPos.first, waterPos.second - 1 };
							break; // North
					}

					if (find(waterPoints.begin(), waterPoints.end(), myNeighbor) == waterPoints.end()) {
						int lwSize = static_cast<int>(localWater.size());
						uniform_real_distribution<float> dist(0.0f, 1.0f);
						float randomValue = dist(rng);
						int index = min(lwSize, static_cast<int>(sqrt(randomValue) * lwSize));
						localWater.insert(localWater.begin() + index, myNeighbor);
					}
				}
			}
		}
	} catch (const exception &e) {
		UtilityFunctions::print("Gaia Green ERROR | Exception caught during Lake generation: " + String(e.what()));
	} catch (...) {
		UtilityFunctions::print("Gaia Green ERROR | Unknown error occurred during Lake generation.");
	}
}

void TerrainGen::generate_rivers(
		vector<vector<int>> &myHeightMap,
		vector<vector<bool>> &myLockMap,
		vector<pair<int, int>> &waterPoints,
		default_random_engine &rng,
		int minRivers = 0,
		int maxRivers = 9,
		int minRiverSize = 20,
		int maxRiverSize = 200) {
	try {
		uniform_int_distribution<int> riverDist(minRivers, maxRivers);
		int numOfRivers = riverDist(rng);

		for (int rivers = 0; rivers < numOfRivers; rivers++) {
			if (waterPoints.empty())
				break;

			uniform_int_distribution<int> riverSizeDist(minRiverSize, maxRiverSize);
			int sizeOfRivers = riverSizeDist(rng);

			// Pick a random starting point
			uniform_int_distribution<int> indexDist(0, static_cast<int>(waterPoints.size()) - 1);
			int randIndex = indexDist(rng);
			pair<int, int> currPos = waterPoints[randIndex];
			waterPoints.erase(waterPoints.begin() + randIndex);

			vector<pair<int, int>> localWater;
			localWater.insert(localWater.begin(), currPos);

			queue<int> dirHistory;

			for (int j = 0; j < 1000; ++j) {
				pair<int, int> waterPos = localWater.front();

				uniform_real_distribution<double> dist(0.0, 1.0);
				int basePos = static_cast<int>(pow(dist(rng), 3) * 3);
				int secPos = basePos / 2;

				uniform_int_distribution<int> offsetDist(-1, 1);
				int thirdPos = (basePos % 2 == 0) ? 0 : offsetDist(rng);
				int fourthPos = (basePos % 2 == 0) ? 0 : offsetDist(rng);

				// Carve river cells
				for (int k = waterPos.first - secPos + thirdPos;
						k <= waterPos.first - secPos + thirdPos + basePos; ++k) {
					for (int l = waterPos.second - secPos + fourthPos;
							l <= waterPos.second - secPos + fourthPos + basePos; ++l) {
						if (k >= 0 && k < static_cast<int>(myHeightMap.size()) &&
								l >= 0 && l < static_cast<int>(myHeightMap[0].size())) {
							if (!myLockMap[k][l]) {
								myHeightMap[k][l] = 0;
							}

							// Smooth surrounding area
							int radius = 8;
							for (int nk = -radius; nk <= radius; ++nk) {
								for (int nl = -radius; nl <= radius; ++nl) {
									if (nk == 0 && nl == 0)
										continue;

									int neighborX = k + nk;
									int neighborY = l + nl;

									if (neighborX >= 0 && neighborX < static_cast<int>(myHeightMap.size()) &&
											neighborY >= 0 && neighborY < static_cast<int>(myHeightMap[0].size())) {
										int &neighborElevation = myHeightMap[neighborX][neighborY];

										if (!myLockMap[neighborX][neighborY]) {
											int diff = myHeightMap[k][l] - neighborElevation;
											if (diff > 1)
												neighborElevation = myHeightMap[k][l] - 1;
											else if (diff < -1)
												neighborElevation = myHeightMap[k][l] + 1;
										}
									}
								}
							}
						}
					}
				}

				sizeOfRivers--;
				if (sizeOfRivers <= 0)
					break;

				// Weighted direction choice to bias flow
				struct WeightedDir {
					double weight;
					int dir;
				};
				vector<WeightedDir> shareList;
				for (int d = 0; d < 4; ++d) {
					int count = 0;
					queue<int> temp = dirHistory;
					while (!temp.empty()) {
						if (temp.front() == d)
							count++;
						temp.pop();
					}
					double weight = 1.0 / (count + 1);
					shareList.push_back({ weight, d });
				}

				for (int m = 0; m < 4; ++m) {
					// Weighted random choice
					double totalWeight = 0;
					for (auto &wd : shareList)
						totalWeight += wd.weight;
					uniform_real_distribution<double> dist(0.0, totalWeight);
					double r = dist(rng);

					int chosenDir = -1;
					for (auto &wd : shareList) {
						r -= wd.weight;
						if (r <= 0) {
							chosenDir = wd.dir;
							break;
						}
					}

					// Remove chosen direction from list
					shareList.erase(
							remove_if(shareList.begin(), shareList.end(),
									[&](const WeightedDir &wd) { return wd.dir == chosenDir; }),
							shareList.end());

					pair<int, int> myNeighbor = waterPos;
					switch (chosenDir) {
						case 0:
							myNeighbor = { waterPos.first + 1, waterPos.second };
							break; // East
						case 1:
							myNeighbor = { waterPos.first - 1, waterPos.second };
							break; // West
						case 2:
							myNeighbor = { waterPos.first, waterPos.second + 1 };
							break; // South
						case 3:
							myNeighbor = { waterPos.first, waterPos.second - 1 };
							break; // North
					}

					if (find(waterPoints.begin(), waterPoints.end(), myNeighbor) == waterPoints.end()) {
						localWater.insert(localWater.begin(), myNeighbor);

						if (m == 3) {
							dirHistory.push(chosenDir);
							while (dirHistory.size() > 3)
								dirHistory.pop();
						}
					}
				}
			}
		}
	} catch (const exception &e) {
		UtilityFunctions::print("Gaia Green ERROR | Exception caught during River generation: " + String(e.what()));
	} catch (...) {
		UtilityFunctions::print("Gaia Green ERROR | Unknown error occurred during River generation.");
	}
}

void TerrainGen::compute_flow_and_walkable_areas(
		vector<vector<int>> &myHeightMap,
		vector<vector<FlowCell>> &flowMap,
		vector<vector<vector<pair<int, int>>>> &inflowMap,
		vector<vector<int>> &flowAccumulation,
		vector<vector<bool>> &walkableMap,
		int widthx2,
		int heightx2) {
	float totalFlow = 0.0f;
	int flowCount = 0;

	//
	// Phase 1 : Downhill Flow
	//
	for (int x = 1; x < widthx2 - 1; x++) {
		for (int y = 1; y < heightx2 - 1; y++) {
			float currentElevation = myHeightMap[x][y];
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
					float neighborElevation = myHeightMap[nx][ny];

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

	// Build inflow map
	for (int x = 1; x < widthx2 - 1; ++x) {
		for (int y = 1; y < heightx2 - 1; ++y) {
			int tx = flowMap[x][y].flowToX;
			int ty = flowMap[x][y].flowToY;
			if (tx != x || ty != y) {
				inflowMap[tx][ty].emplace_back(x, y);
			}
		}
	}

	// Accumulate flow
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
	int maxRiverWidth = 1; // Max perpendicular cells to mark

	for (int x = 1; x < widthx2 - 1; x++) {
		for (int y = 1; y < heightx2 - 1; y++) {
			float flow = flowAccumulation[x][y];
			if (flow < averageFlow)
				continue; // Only above-average flow cells

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
}

void TerrainGen::determine_tile_types(
		int width,
		int height,
		const vector<vector<int>> &myHeightMap,
		const vector<vector<float>> &rawNoise,
		vector<vector<int>> &elevationMap,
		vector<vector<int>> &elevationMapTiles,
		vector<vector<TileType>> &tileMap,
		const vector<vector<bool>> &walkableMap,
		float cliffThreshold,
		GridMap *myGridMap) {
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			// Neighbor's of Data Grid Map
			// +----+----+
			// | n1 | n2 |
			// +----+----+
			// | n3 | n4 |
			// +----+----+
			//
			// Get the surrounding DataGrid Neighbors overlap of the Render Grid
			int n1 = myHeightMap[x][y];
			int n2 = myHeightMap[x + 1][y];
			int n3 = myHeightMap[x][y + 1];
			int n4 = myHeightMap[x + 1][y + 1];

			// Determine Elevation Values

			int elevation = max({ n1, n2, n3, n4 });

			elevationMap[x][y] = elevation;

			if (elevation == 0) { // Water Tiles are considered the same elevation as Ground
				elevation = 1;
			}

			elevationMapTiles[x][y] = elevation;

			// Determine the Slope from Raw Noise
			//
			//	Use slope to determine Cliffs & Ramps
			//	relying on the raw noise as a decider
			//	for natural ramps and slopes
			//

			float r1 = rawNoise[x][y];
			float r2 = rawNoise[x + 1][y];
			float r3 = rawNoise[x][y + 1];
			float r4 = rawNoise[x + 1][y + 1];

			float slopeX = fabs(r2 - r1) + fabs(r4 - r3);
			float slopeY = fabs(r3 - r1) + fabs(r4 - r2);
			float totalSlope = slopeX + slopeY;
			float slope = totalSlope / 4.0f; // Normalize to 0 to 1

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
			if (n1 == n2 && n2 == n3 && n3 == n4 && n1 == n4 && n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0) {
				tileMap[x][y] = GROUND;
			}

			//-------------------------//
			// Water Edges
			//-------------------------//

			// Water's Edge East
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			if (n1 == 0 && n2 == 0 && n3 > 0 && n4 > 0) {
				tileMap[x][y] = WATER_EDGE;

			}
			// Water's Edge West
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 0 | 0 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 == 0 && n4 == 0) {
				tileMap[x][y] = WATER_EDGE;
			}
			// Water's Edge South
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 0 | 1 |
			// +----+----+  +---+---+
			//
			else if (n1 == 0 && n3 == 0 && n2 > 0 && n4 > 0) {
				tileMap[x][y] = WATER_EDGE;

			}
			// Water's Edge North
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 0 |
			// +----+----+  +---+---+
			//
			else if (n2 == 0 && n4 == 0 && n1 > 0 && n3 > 0) {
				tileMap[x][y] = WATER_EDGE;
			}

			//-------------------------//
			// Water Corners
			//-------------------------//

			// TODO : Water Corner's Logic is incorrect

			// Water's Corner WEST
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			if (n1 == 0 && n2 > 0 && n3 > 0 && n4 > 0) {
				tileMap[x][y] = WATER_CORNER;
			}
			// Water's Corner EAST
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 0 |
			// +----+----+  +---+---+
			//
			else if (n4 == 0 && n1 > 0 && n2 > 0 && n3 > 0) {
				tileMap[x][y] = WATER_CORNER;
			}
			// Water's Corner SOUTH
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 0 | 1 |
			// +----+----+  +---+---+
			//
			else if (n3 == 0 && n1 > 0 && n2 > 0 && n4 > 0) {
				tileMap[x][y] = WATER_CORNER;
			}
			// Water's Corner NORTH
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			else if (n2 == 0 && n1 > 0 && n3 > 0 && n4 > 0) {
				tileMap[x][y] = WATER_CORNER;
			}

			//-------------------------//
			// Cliff's & Ramp's Corner
			//-------------------------//

			// Corner EAST
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 2 |
			// +----+----+  +---+---+
			//
			if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n4 > n1 && n4 > n2 && n4 > n3) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || walkableMap[x][y]) {
					tileMap[x][y] = CLIFF_CORNER;
				} else {
					tileMap[x][y] = RAMP_CORNER;
				}
			}
			// Corner WEST
			// +----+----+  +---+---+
			// | n1 | n2 |  | 2 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 > n2 && n1 > n3 && n1 > n4) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || walkableMap[x][y]) {
					tileMap[x][y] = CLIFF_CORNER;
				} else {
					tileMap[x][y] = RAMP_CORNER;
				}
			}
			// Corner NORTH
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n2 > n1 && n2 > n3 && n2 > n4) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || walkableMap[x][y]) {
					tileMap[x][y] = CLIFF_CORNER;
				} else {
					tileMap[x][y] = RAMP_CORNER;
				}
			}
			// Corner SOUTH
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 2 | 1 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n3 > n1 && n3 > n2 && n3 > n4) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || walkableMap[x][y]) {
					tileMap[x][y] = CLIFF_CORNER;
				} else {
					tileMap[x][y] = RAMP_CORNER;
				}
			}

			//-------------------------//
			// Cliff's & Ramp's Edges
			//-------------------------//

			// Tile Start
			// No Rotation -> North
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 2 |
			// +----+----+  +---+---+

			// Cliff or Ramp Edge EAST
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 2 | 2 |
			// +----+----+  +---+---+
			//
			if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 < n3 && n1 < n4 && n2 < n4 && n2 < n3) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || walkableMap[x][y]) {
					tileMap[x][y] = CLIFF;
				} else {
					tileMap[x][y] = RAMP;
				}
			}
			//
			// Tile Start
			// No Rotation -> North
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 2 |
			// +----+----+  +---+---+
			//
			// Cliff's Edge WEST
			// +----+----+  +---+---+
			// | n1 | n2 |  | 2 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 > n3 && n1 > n4 && n2 > n4 && n2 > n3) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || walkableMap[x][y]) {
					tileMap[x][y] = CLIFF;
				} else {
					tileMap[x][y] = RAMP;
				}
			}
			//
			// Tile Start
			// No Rotation -> North
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 2 |
			// +----+----+  +---+---+
			//
			// Cliff's Edge SOUTH
			// +----+----+  +---+---+
			// | n1 | n2 |  | 2 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 2 | 1 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 < n2 && n1 < n4 && n3 < n2 && n3 < n4) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || walkableMap[x][y]) {
					tileMap[x][y] = CLIFF;
				} else {
					tileMap[x][y] = RAMP;
				}
			}
			//
			// Tile Start
			// No Rotation -> North
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 2 |
			// +----+----+  +---+---+
			//
			// Cliff's Edge NORTH
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 2 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 > n2 && n1 > n4 && n3 > n2 && n3 > n4) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || walkableMap[x][y]) {
					tileMap[x][y] = CLIFF;
				} else {
					tileMap[x][y] = RAMP;
				}
			}

			/*****************************************************

				Grid Map Cell Setter

			*****************************************************/

			myGridMap->set_cell_item(Vector3i(x, elevation, y), tileMap[x][y], NORTH);
		}
	}
}

void TerrainGen::print_surrounding_cells(const vector<vector<int>> &myHeightMap, int cx, int cy, int radius) {
	UtilityFunctions::print("C Elevation : ", myHeightMap[cx][cy]);
	for (int x = cx - radius; x <= cx + radius; ++x) {
		String row;
		for (int y = cy - radius; y <= cy + radius; ++y) {
			if (x == cx && y == cy) {
				row += "C "; // mark center
				continue;
			}
			if (y >= 0 && y < static_cast<int>(myHeightMap.size()) &&
					x >= 0 && x < static_cast<int>(myHeightMap[0].size())) {
				row += String::num_int64(myHeightMap[x][y]) + " ";
			} else {
				row += "X "; // out-of-bounds marker
			}
		}
		UtilityFunctions::print(row);
	}

	UtilityFunctions::print("\n\n");
}

void TerrainGen::apply_tile_rotations_and_fixes(
		int width,
		int height,
		const vector<vector<int>> &elevationMap,
		GridMap *myGridMap) {
	// TODO : Water Corner's are not turning correctly
	// TODO : Water tiles are being placed at higher elevation, may be fixed by smoothing lake/river generations

	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			int t_height = elevationMap[x][y];
			int tile_id = myGridMap->get_cell_item(Vector3i(x, t_height, y));
			if (tile_id == -1 || tile_id == GROUND)
				continue;

			auto in_bounds = [&](int gx, int gy) {
				return gx >= 0 && gy >= 0 && gx < width && gy < height;
			};
			auto safe_height = [&](int gx, int gy, int fallback_h) {
				return in_bounds(gx, gy) ? elevationMap[gx][gy] : fallback_h;
			};
			auto safe_tile_at = [&](int gx, int gy) {
				if (!in_bounds(gx, gy))
					return -1;
				int h = elevationMap[gx][gy];
				return myGridMap->get_cell_item(Vector3i(gx, h, gy));
			};

			auto isWaterLike = [&](TileType t) {
				return t == WATER || t == WATER_EDGE || t == WATER_CORNER || t == WATER_CORNER_INNER;
			};

			int rotation_val = NORTH; // default face −Z

			//
			// +--------------------+---------------+--------------------+
			// | NW (x - 1 , y - 1) | N (x , y - 1) | NE (x + 1, y - 1)  |
			// +--------------------+---------------+--------------------+
			// | W (x - 1 , y)      | T (x,y)       | E (x + 1) , y      |
			// +--------------------+---------------+--------------------+
			// | SW (x - 1 , y + 1) | S (x , y + 1) | SE (x + 1 , y + 1) |
			// +--------------------+---------------+--------------------+
			//
			// +----+----+----+ +----+----+----+ +----+----+----+
			// | m1 | m2 | m3 | | NW | N  | NE | |    | -Z |    |
			// +----+----+----+ +----+----+----+ +----+----+----+
			// | m4 | T  | m5 | | W  | T  | E  | | -X |    | X  |
			// +----+----+----+ +----+----+----+ +----+----+----+
			// | m6 | m7 | m8 | | SW | S  | SE | |    | Z  |    |
			// +----+----+----+ +----+----+----+ +----+----+----+
			//
			// Note : Cliff Corners point their ground toward game world : South East
			// Note : Ramp Corners point their high ground toward game world :

			//
			// Cardinal Neighbors
			//
			//

			int nHeight = safe_height(x, y - 1, t_height);
			int nTile = safe_tile_at(x, y - 1); // m2

			int eHeight = safe_height(x + 1, y, t_height);
			int eTile = safe_tile_at(x + 1, y); // m5

			int sHeight = safe_height(x, y + 1, t_height);
			int sTile = safe_tile_at(x, y + 1); // m7

			int wHeight = safe_height(x - 1, y, t_height);
			int wTile = safe_tile_at(x - 1, y); // m4

			//
			// Diagonal Neighbors
			//

			int neHeight = safe_height(x + 1, y - 1, t_height);
			int neTile = safe_tile_at(x + 1, y - 1); // m3

			int seHeight = safe_height(x + 1, y + 1, t_height);
			int seTile = safe_tile_at(x + 1, y + 1); // m8

			int swHeight = safe_height(x - 1, y + 1, t_height);
			int swTile = safe_tile_at(x - 1, y + 1); // m6

			int nwHeight = safe_height(x - 1, y - 1, t_height);
			int nwTile = safe_tile_at(x - 1, y - 1); // m1

			//
			// Water Tiles
			//

			bool nWater = isWaterLike(static_cast<TileType>(nTile));
			bool sWater = isWaterLike(static_cast<TileType>(sTile));
			bool eWater = isWaterLike(static_cast<TileType>(eTile));
			bool wWater = isWaterLike(static_cast<TileType>(wTile));
			bool neWater = isWaterLike(static_cast<TileType>(neTile));
			bool nwWater = isWaterLike(static_cast<TileType>(nwTile));
			bool seWater = isWaterLike(static_cast<TileType>(seTile));
			bool swWater = isWaterLike(static_cast<TileType>(swTile));

			if (isWaterLike(static_cast<TileType>(tile_id))) {
				int waterNeighbors =
						nWater + sWater + eWater + wWater +
						neWater + nwWater + seWater + swWater;

				// Fully surrounded by water
				if (waterNeighbors == 8) {
					tile_id = WATER;
					rotation_val = 0;
				}
			}

			if (tile_id == RAMP || tile_id == CLIFF || tile_id == WATER_EDGE) {
				// +----+----+----+ +----+----+----+
				// | m1 | m2 | m3 | | NW | N  | NE |
				// +----+----+----+ +----+----+----+
				// | m4 | T  | m5 | | W  | T  | E  |
				// +----+----+----+ +----+----+----+
				// | m6 | m7 | m8 | | SW | S  | SE |
				// +----+----+----+ +----+----+----+

				// Cardinal's
				//
				//	N (m2), E (m5), S (m7), W (m4)
				//
				// 	Only two possible combinations an edge piece can be placed
				//	Since an edge piece must connect from lower elevation to
				//	higher elevation.
				//	Thus, m2 + m7 vs m4 + m5
				//
				//	Of those two combinations, there is two ways to rotate
				//

				// North is higher than South
				// Point to m7
				if (nHeight == sHeight + 1 && static_cast<TileType>(nTile) == GROUND) {
					rotation_val = EAST;
				}

				// South is higher than North
				// Point to m2
				if (sHeight == nHeight + 1 && static_cast<TileType>(sTile) == GROUND) {
					rotation_val = WEST;
				}

				// East is higher than West
				// Point to m5
				if (eHeight == wHeight + 1 && static_cast<TileType>(eTile) == GROUND) {
					rotation_val = NORTH;
				}

				// West is higher than East
				// Point to m4
				if (wHeight == eHeight + 1 && static_cast<TileType>(wTile) == GROUND) {
					rotation_val = SOUTH;
				}
			}

			// T = Target Cell
			// +----+----+----+ +----+----+----+
			// | m1 | m2 | m3 | | NW | N  | NE |
			// +----+----+----+ +----+----+----+
			// | m4 | T  | m5 | | W  | T  | E  |
			// +----+----+----+ +----+----+----+
			// | m6 | m7 | m8 | | SW | S  | SE |
			// +----+----+----+ +----+----+----+
			//
			// T should consider m2 + m5, m5 + m7, m7 + m4, and m4 + m2; for cliffs and ramps
			// Then it should find the highest elevation of ground piece at, m3, m8, m6, m1
			// Then it should decide the rotation by rotating the corner toward the higher elevation
			//
			// choose corner defined by edge tiles around T, aim toward the higher diagonal ground
			//
			else if (tile_id == RAMP_CORNER || tile_id == CLIFF_CORNER || tile_id == WATER_CORNER) {
				auto pos = [](float v) { return v > 0.f ? v : 0.f; };

				// Decide which tile types count as "edge" for the current corner type
				auto is_edge_for_corner = [&](TileType t) -> bool {
					return t == CLIFF || t == RAMP || t == WATER_EDGE;
					// if (tile_id == WATER_CORNER) {
					// 	return t == WATER_EDGE;
					// } else if (tile_id == CLIFF_CORNER) {
					// 	return t == CLIFF;
					// } else {
					// 	return t == RAMP;
					// }
				};

				// Convenience flags for cardinal neighbors
				const bool nEdge = is_edge_for_corner(static_cast<TileType>(nTile));
				const bool eEdge = is_edge_for_corner(static_cast<TileType>(eTile));
				const bool sEdge = is_edge_for_corner(static_cast<TileType>(sTile));
				const bool wEdge = is_edge_for_corner(static_cast<TileType>(wTile));

				// Diagonals must be pure ground for the “highest ground diagonal” rule
				const bool neGround = (static_cast<TileType>(neTile) == GROUND);
				const bool seGround = (static_cast<TileType>(seTile) == GROUND);
				const bool swGround = (static_cast<TileType>(swTile) == GROUND);
				const bool nwGround = (static_cast<TileType>(nwTile) == GROUND);

				// +----+----+----+ +----+----+----+
				// | m1 | m2 | m3 | | NW | N  | NE |
				// +----+----+----+ +----+----+----+
				// | m4 | T  | m5 | | W  | T  | E  |
				// +----+----+----+ +----+----+----+
				// | m6 | m7 | m8 | | SW | S  | SE |
				// +----+----+----+ +----+----+----+
				//
				// Diagonal's
				//
				// Corner's Higher Elevation Point : South West / m6
				// NE (m3), SE (m8), SW (m6), NW (m1)
				// if -1, not ground tile
				//
				// Corner Edge Tiles Start pointing (-x,+z) SW
				// No clue why different tiles are requiring different ROT/Orientation values

				// North East
				if (nEdge && eEdge && !sEdge && !wEdge) { // m2 + m5 | NOT m3
					if (tile_id == CLIFF_CORNER)
						rotation_val = NORTH;
					if (tile_id == RAMP_CORNER)
						rotation_val = NORTH;
					if (tile_id == WATER_CORNER)
						rotation_val = NORTH;
				}

				// South East
				if (sEdge && eEdge && !nEdge && !wEdge) { // m5 + m7 | NOT m8
					if (tile_id == CLIFF_CORNER)
						rotation_val = EAST;
					if (tile_id == RAMP_CORNER)
						rotation_val = EAST;
					if (tile_id == WATER_CORNER)
						rotation_val = EAST;
				}

				// South West
				if (sEdge && wEdge && !nEdge && !eEdge) { // m4 + m7 | NOT m6
					if (tile_id == CLIFF_CORNER)
						rotation_val = SOUTH;
					if (tile_id == RAMP_CORNER)
						rotation_val = SOUTH;
					if (tile_id == WATER_CORNER)
						rotation_val = SOUTH;
				}

				// North West
				if (nEdge && wEdge && !sEdge && !eEdge) { // m4 + m2 | NOT m1
					if (tile_id == CLIFF_CORNER)
						rotation_val = WEST;
					if (tile_id == RAMP_CORNER)
						rotation_val = WEST;
					if (tile_id == WATER_CORNER)
						rotation_val = WEST;
				}
			}

			if (tile_id == RAMP_CORNER) {
				// Place Ground under Ramp Tiles
				myGridMap->set_cell_item(Vector3i(x, t_height - 1, y), GROUND, rotation_val);
			}

			myGridMap->set_cell_item(Vector3i(x, t_height, y), tile_id, rotation_val);

			// ? : Below is 2 Edge case's that need fixed
			// TODO : Fix these Edge Case's

			/*****************************************************

				EDGE CASE FIX :

					Floating Ground Tile Fix

			*****************************************************/

			// EDGE CASE : Higher Ground has to connect to Cliffs 1
			//
			// +--------------------+---------------+--------------------+
			// | NW (x - 1 , y - 1) | N (x , y - 1) | NE (x + 1, y - 1)  |
			// +--------------------+---------------+--------------------+
			// | W (x - 1 , y)      | T (x,y)       | E (x + 1) , y      |
			// +--------------------+---------------+--------------------+
			// | SW (x - 1 , y + 1) | S (x , y + 1) | SE (x + 1 , y + 1) |
			// +--------------------+---------------+--------------------+
			//
			// +----+----+----+ +----+----+----+
			// | m1 | m2 | m3 | | NW | N  | NE |
			// +----+----+----+ +----+----+----+
			// | m4 | T  | m5 | | W  | T  | E  |
			// +----+----+----+ +----+----+----+
			// | m6 | m7 | m8 | | SW | S  | SE |
			// +----+----+----+ +----+----+----+
			//
			// +----+----+----+ +----+----+----+ +----+----+----+
			// | m1 | m2 | m3 | | G  | R  | X  | | R  | G  | X  |
			// +----+----+----+ +----+----+----+ +----+----+----+
			// | m4 | T  | m5 | | R  | G  | X  | | G  | R  | X  |
			// +----+----+----+ +----+----+----+ +----+----+----+
			// | m6 | m7 | m8 | | X  | X  | X  | | X  | X  | X  |
			// +----+----+----+ +----+----+----+ +----+----+----+
			//

			if ((nwTile == GROUND && nTile == RAMP && wTile == RAMP && tile_id == GROUND)) {
				// Set Floating Ground to Cliff Corner
				if (nwHeight > t_height) {
					myGridMap->set_cell_item(Vector3i(x - 1, nwHeight, y - 1), CLIFF_CORNER_INNER, SOUTH); // m1
				} else {
					myGridMap->set_cell_item(Vector3i(x, t_height, y), CLIFF_CORNER_INNER, NORTH); // T
				}

				// Get Orientation (Rotation) Value
				int rot1 = myGridMap->get_cell_item_orientation(Vector3i(x, nHeight, y - 1)); // m2
				int rot2 = myGridMap->get_cell_item_orientation(Vector3i(x - 1, wHeight, y)); // m4

				// Set Ramps to Cliffs
				myGridMap->set_cell_item(Vector3i(x, nHeight, y - 1), CLIFF, rot1); // m2
				myGridMap->set_cell_item(Vector3i(x - 1, wHeight, y), CLIFF, rot2); // m4
			}

			if ((nwTile == RAMP && nTile == GROUND && wTile == GROUND && tile_id == RAMP)) {
				// Set Floating Ground to Cliff Corner
				if (nHeight > wHeight) {
					myGridMap->set_cell_item(Vector3i(x, nHeight, y - 1), CLIFF_CORNER_INNER, SOUTH); // m2
				} else {
					myGridMap->set_cell_item(Vector3i(x - 1, wHeight, y), CLIFF_CORNER_INNER, NORTH); // m4
				}

				// Get Orientation (Rotation) Value
				int rot1 = myGridMap->get_cell_item_orientation(Vector3i(x - 1, nwHeight, y - 1)); // m1
				int rot2 = myGridMap->get_cell_item_orientation(Vector3i(x, t_height, y)); // T

				// Set Ramps to Cliffs
				myGridMap->set_cell_item(Vector3i(x - 1, nwHeight, y - 1), CLIFF, rot1); // m1
				myGridMap->set_cell_item(Vector3i(x, t_height, y), CLIFF, rot2); // T
			}

			// EDGE CASE : Higher Ground has to connect to Cliffs 2
			//
			// +--------------------+---------------+--------------------+
			// | NW (x - 1 , y - 1) | N (x , y - 1) | NE (x + 1, y - 1)  |
			// +--------------------+---------------+--------------------+
			// | W (x - 1 , y)      | T (x,y)       | E (x + 1) , y      |
			// +--------------------+---------------+--------------------+
			// | SW (x - 1 , y + 1) | S (x , y + 1) | SE (x + 1 , y + 1) |
			// +--------------------+---------------+--------------------+
			//
			// +----+----+----+ +----+----+----+
			// | m1 | m2 | m3 | | NW | N  | NE |
			// +----+----+----+ +----+----+----+
			// | m4 | T  | m5 | | W  | T  | E  |
			// +----+----+----+ +----+----+----+
			// | m6 | m7 | m8 | | SW | S  | SE |
			// +----+----+----+ +----+----+----+
			//
			// +----+----+----+ +----+----+----+ +----+----+----+
			// | m1 | m2 | m3 | | X  | G  | R  | | X  | R  | G  |
			// +----+----+----+ +----+----+----+ +----+----+----+
			// | m4 | T  | m5 | | X  | R  | G  | | X  | G  | R  |
			// +----+----+----+ +----+----+----+ +----+----+----+
			// | m6 | m7 | m8 | | X  | X  | X  | | X  | X  | X  |
			// +----+----+----+ +----+----+----+ +----+----+----+

			if ((neTile == RAMP && nTile == GROUND && eTile == GROUND && tile_id == RAMP)) {
				// Set Floating Ground to Cliff Corner
				if (nHeight > eHeight) {
					myGridMap->set_cell_item(Vector3i(x, nHeight, y - 1), CLIFF_CORNER_INNER, SOUTH); // m2
				} else {
					myGridMap->set_cell_item(Vector3i(x + 1, eHeight, y), CLIFF_CORNER_INNER, NORTH); // m5
				}

				// Get Orientation (Rotation) Value
				int rot1 = myGridMap->get_cell_item_orientation(Vector3i(x + 1, neHeight, y - 1)); // m3
				int rot2 = myGridMap->get_cell_item_orientation(Vector3i(x, t_height, y)); // T

				// Set Ramps to Cliffs
				myGridMap->set_cell_item(Vector3i(x + 1, neHeight, y - 1), CLIFF, rot1); // m3
				myGridMap->set_cell_item(Vector3i(x, t_height, y), CLIFF, rot2); // T
			}

			if ((neTile == GROUND && nTile == RAMP && eTile == RAMP && tile_id == GROUND)) {
				// Set Floating Ground to Cliff Corner
				if (neHeight > t_height) {
					myGridMap->set_cell_item(Vector3i(x + 1, neHeight, y - 1), CLIFF_CORNER_INNER, NORTH); // m3
				} else {
					myGridMap->set_cell_item(Vector3i(x, t_height, y), CLIFF_CORNER_INNER, SOUTH); // T
				}

				// Set Ramp's to Cliffs
				int rot1 = myGridMap->get_cell_item_orientation(Vector3i(x, nHeight, y - 1)); // m2
				int rot2 = myGridMap->get_cell_item_orientation(Vector3i(x + 1, eHeight, y)); // m5

				myGridMap->set_cell_item(Vector3i(x, nHeight, y - 1), CLIFF, rot1); // m2
				myGridMap->set_cell_item(Vector3i(x + 1, eHeight, y), CLIFF, rot2); // m5
			}

			/*****************************************************

				EDGE CASE FIX :



			*****************************************************/

			// EDGE CASE : Higher Ground has to connect to Cliffs
			//
			// +----+----+  +----+----+ +----+----+ +----+----+ +----+----+    +----+----+
			// | n1 | n2 |  | g1 | c2 | | r2 | g1 | | g2 | r2 | | c2 | g2 |    | g1 | c2 |
			// +----+----+  +----+----+ +----+----+ +----+----+ +----+----+ -> +----+----+
			// | n3 | n4 |  | r2 | g2 | | g2 | c2 | | c2 | g1 | | g1 | r2 |    | r2 | c2 |
			// +----+----+  +----+----+ +----+----+ +----+----+ +----+----+    +----+----+
			//

			// EDGE CASE : Ramps surround a Ramp tile
			// Two higher ramps and two lower ramps with a ramp tile in the center
		}
	}
}

void TerrainGen::generate_placeable_areas_and_samples(
		int width,
		int height,
		int widthx2,
		int heightx2,
		const vector<vector<bool>> &walkableMap,
		const vector<vector<TileType>> &tileMap,
		vector<vector<bool>> &placeableMap,
		vector<Point> &poissonSamples,
		float minDistance = 3.0f) {
	// Generate Placeable Area's

	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			bool isNonWalkable = walkableMap[x][y];
			bool isNotCliffOrRamp = tileMap[x][y] != TileType::CLIFF &&
					tileMap[x][y] != TileType::RAMP &&
					tileMap[x][y] != TileType::CLIFF_CORNER &&
					tileMap[x][y] != TileType::RAMP_CORNER;

			// Only mark subcells as placeable if both conditions are true
			bool isPlaceable = isNonWalkable && isNotCliffOrRamp;

			placeableMap[x * 2][y * 2] = isPlaceable;
			placeableMap[x * 2 + 1][y * 2] = isPlaceable;
			placeableMap[x * 2][y * 2 + 1] = isPlaceable;
			placeableMap[x * 2 + 1][y * 2 + 1] = isPlaceable;
		}
	}

	// Poisson Disk Sampling

	for (int x = 0; x < widthx2; x++) {
		for (int y = 0; y < heightx2; y++) {
			if (!placeableMap[x][y])
				continue;

			bool tooClose = false;
			for (const Point &p : poissonSamples) {
				float dx = p.x - x;
				float dy = p.y - y;
				if (sqrt(dx * dx + dy * dy) < minDistance) {
					tooClose = true;
					break;
				}
			}

			if (!tooClose) {
				poissonSamples.push_back({ x, y });
			}
		}
	}
}

// Lock the outer 2-cell border of the grid
void TerrainGen::lockOuterBorder(vector<vector<bool>> &myLockMap) {
	int maxX = static_cast<int>(myLockMap.size());
	int maxY = static_cast<int>(myLockMap[0].size());

	// Top and bottom borders (rows 0,1 and maxX-2,maxX-1)
	for (int x = 0; x < maxX; ++x) {
		for (int y = 0; y < 2; ++y) {
			myLockMap[x][y] = true; // top 2 rows
			myLockMap[x][maxY - 1 - y] = true; // bottom 2 rows
		}
	}

	// Left and right borders (cols 0,1 and maxY-2,maxY-1)
	for (int y = 0; y < maxY; ++y) {
		for (int x = 0; x < 2; ++x) {
			myLockMap[x][y] = true; // left 2 cols
			myLockMap[maxX - 1 - x][y] = true; // right 2 cols
		}
	}
}