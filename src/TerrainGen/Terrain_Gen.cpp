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
		int openAreaMin,
		int noiseType,
		double waterRemoval,
		float cliffThreshold,
		float noiseFreq) {
	/*****************************************************

		Setup

	*****************************************************/
	int dx[4] = { 1, -1, 0, 0 };
	int dy[4] = { 0, 0, 1, -1 };

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
	vector<vector<bool>> isFlat(widthx2, vector<bool>(heightx2, false));

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
	// Poisson Disk Sampling
	//

	struct Point {
		int x, y;
	};

	vector<Point> poissonSamples;

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
	vector<vector<int>> heightMap(width * 2, vector<int>(height * 2, 0));
	// Render Grid -> Real size grid with final tile type values
	vector<vector<TileType>> tileMap(width, vector<TileType>(height, GROUND));
	// Render Grid's Elevation Values -> Real size grid with elevation ints
	vector<vector<int>> elevationMap(width, vector<int>(height, 0));
	// Each cell represents a quarter of the original tile
	vector<vector<bool>> placeableMap(widthx2, vector<bool>(heightx2, true));

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
			heightMap[x][y] = round(normalizedNoise * elevationMax);
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

			lowResMap[x][y] = heightMap[sampleX][sampleY];
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
				heightMap[x][y] = 1;
			} else {
				heightMap[x][y] = elevationValue;
			}
		}
	}

	/*****************************************************

		Cellular Automata Filter A

			Find flat 3x3 zones and expand

	*****************************************************/

	//
	// Phase One : Locate Natural 3x3 chunks (really 4x4)
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
			// n6 is our center cell
			// +-----+-----+-----+-----+
			// | n1  | n2  | n3  | n4  |
			// +-----+-----+-----+-----+
			// | n5  | n6  | n7  | n8  |
			// +-----+-----+-----+-----+
			// | n9  | n10 | n11 | n12 |
			// +-----+-----+-----+-----+
			// | n13 | n14 | n15 | n16 |
			// +-----+-----+-----+-----+
			for (int x = 1; x < widthx2 - 4; x += 4) {
				for (int y = 1; y < heightx2 - 4; y += 4) {
					int n6 = heightMap[x][y];
					bool allEqual = true;
					for (int dx = -1; dx <= 2 && allEqual; ++dx) {
						for (int dy = -1; dy <= 2; ++dy) {
							if (heightMap[x + dx][y + dy] != n6) {
								allEqual = false;
								break;
							}
						}
					}
					if (allEqual) {
						FlatChunks3x3 += 1;
						openAreaCount += 1;
						flatZones.push_back({ x, n6, y });
						for (int dx = -1; dx <= 2; ++dx) {
							for (int dy = -1; dy <= 2; ++dy) {
								isFlat[x + dx][y + dy] = true;
							}
						}
						if (FlatChunks3x3 >= openAreaMin)
							break;
					}
				}
			}

			// If None, deny entry back into if statement
			if (FlatChunks3x3 == 0)
				FlatChunks3x3 = -1;
		}

		if (openAreaCount >= openAreaMin)
			break;

		//
		// Phase Two : Expand using Cellular Automata
		//

		// CA Rule: If 5+ neighbors are flat, become flat
		for (int iter = 0; iter < 3; ++iter) {
			vector<vector<bool>> nextFlat = isFlat;

			for (int x = 1; x < widthx2 - 1; ++x) {
				for (int y = 1; y < heightx2 - 1; ++y) {
					int count = 0;
					for (int dx = -1; dx <= 1; ++dx) {
						for (int dy = -1; dy <= 1; ++dy) {
							if (dx == 0 && dy == 0)
								continue;
							if (isFlat[x + dx][y + dy])
								count++;
						}
					}

					if (count >= 5) {
						nextFlat[x][y] = true;
						heightMap[x][y] = heightMap[x][y]; // Optional: flatten to neighbor elevation
					}
				}
			}

			isFlat = nextFlat;
		}

		//
		// Phase Three : Scan for new Flat Zones
		//

		for (int x = 1; x < widthx2 - 4; x += 4) {
			for (int y = 1; y < heightx2 - 4; y += 4) {
				bool allFlat = true;

				for (int dx = 0; dx < 4 && allFlat; ++dx) {
					for (int dy = 0; dy < 4; ++dy) {
						if (!isFlat[x + dx][y + dy]) {
							allFlat = false;
							break;
						}
					}
				}

				if (allFlat) {
					openAreaCount++;
				} // center of 4×4 block
				if (flatZones.size() >= openAreaMin)
					break;
			}
		}
	}

	/*****************************************************

		Cellular Automata Filter B

			Patching / Removing Outliers

	*****************************************************/
	int neighborRadius = 1; // 3x3 neighborhood
	int threshold = 5; // Rule 2: minimum matching neighbors to preserve center
	int iterations = 3; // Number of CA generations to apply (default: 1)

	vector<vector<int>> curr = heightMap;
	vector<vector<int>> next = heightMap;

	for (int it = 0; it < iterations; ++it) {
		bool anyChanged = false;

		for (int x = 0; x < widthx2; ++x) {
			for (int y = 0; y < heightx2; ++y) {
				const int center = curr[x][y];

				// Count neighbors matching the center value
				int matchCount = 0;
				for (int dx = -neighborRadius; dx <= neighborRadius; ++dx) {
					for (int dy = -neighborRadius; dy <= neighborRadius; ++dy) {
						if (dx == 0 && dy == 0)
							continue;
						int nx = x + dx, ny = y + dy;
						if (nx < 0 || nx >= widthx2 || ny < 0 || ny >= heightx2)
							continue;
						if (curr[nx][ny] == center)
							++matchCount;
					}
				}

				// Default: preserve current value
				int newVal = center;

				// Rule 2: If not enough matching neighbors, resolve via dominant neighbor value
				if (matchCount < threshold) {
					// Frequency table over bounded elevation range [0..elevationMax]
					vector<int> freq(elevationMax + 1, 0);

					// Count frequency of each neighbor value
					for (int dx = -neighborRadius; dx <= neighborRadius; ++dx) {
						for (int dy = -neighborRadius; dy <= neighborRadius; ++dy) {
							if (dx == 0 && dy == 0)
								continue;
							int nx = x + dx, ny = y + dy;
							if (nx < 0 || nx >= widthx2 || ny < 0 || ny >= heightx2)
								continue;
							++freq[curr[nx][ny]];
						}
					}

					// Find dominant neighbor value
					int dominantVal = center;
					int maxCount = 0;
					for (int val = 0; val <= elevationMax; ++val) {
						if (freq[val] > maxCount) {
							maxCount = freq[val];
							dominantVal = val;
						}
					}

					// Rule 4: If center is isolated (few dominant neighbors), grow toward dominant
					if (maxCount <= 2) {
						newVal = min(dominantVal + 1, elevationMax);
					}
					// Rule 3: Otherwise, conform to dominant neighbor value
					else {
						newVal = dominantVal;
					}
				}

				next[x][y] = newVal;
				if (newVal != center)
					anyChanged = true;
			}
		}

		// Advance generation via double-buffering
		curr.swap(next);

		// Optional early exit if stable
		if (!anyChanged)
			break;
	}

	// Write back the final state
	heightMap = move(curr);

	/*****************************************************

		Cellular Automata Filter C

			Water Reduction

	*****************************************************/

	if (waterRemoval >= 10.0f) {
		// Clamp percentage
		if (waterRemoval > 100.0f)
			waterRemoval = 100.0f;
		if (waterRemoval < 0.0f)
			waterRemoval = 0.0f;

		vector<vector<bool>> visited(heightx2, vector<bool>(widthx2, false));

		for (int y = 0; y < heightx2; y++) {
			for (int x = 0; x < widthx2; x++) {
				if (heightMap[y][x] != 0 || visited[y][x])
					continue;

				// Manual stack flood fill
				vector<pair<int, int>> stack;
				vector<pair<int, int>> region;
				stack.push_back(make_pair(x, y));
				visited[y][x] = true;

				while (!stack.empty()) {
					pair<int, int> current = stack.back();
					stack.pop_back();
					int cx = current.first;
					int cy = current.second;
					region.push_back(current);

					for (int d = 0; d < 4; d++) {
						int nx = cx + dx[d];
						int ny = cy + dy[d];
						if (nx < 0 || ny < 0 || nx >= widthx2 || ny >= heightx2)
							continue;
						if (visited[ny][nx])
							continue;
						if (heightMap[ny][nx] != 0)
							continue;
						visited[ny][nx] = true;
						stack.push_back(make_pair(nx, ny));
					}
				}

				// Determine how many cells to flip
				int regionSize = region.size();
				int toFlip = (int)(regionSize * (waterRemoval / 100.0f));
				if (toFlip <= 0)
					continue;

				// Rank by proximity to edge
				vector<pair<int, pair<int, int>>> candidates;
				for (int i = 0; i < regionSize; i++) {
					int rx = region[i].first;
					int ry = region[i].second;
					int distX = min(rx, widthx2 - 1 - rx);
					int distY = min(ry, heightx2 - 1 - ry);
					int edgeDist = min(distX, distY);
					candidates.push_back(make_pair(edgeDist, region[i]));
				}

				// Bubble sort
				for (int i = 0; i < (int)candidates.size(); i++) {
					for (int j = i + 1; j < (int)candidates.size(); j++) {
						if (candidates[j].first < candidates[i].first) {
							pair<int, pair<int, int>> temp = candidates[i];
							candidates[i] = candidates[j];
							candidates[j] = temp;
						}
					}
				}

				// Flip top N water cells to land
				for (int i = 0; i < toFlip && i < (int)candidates.size(); i++) {
					int fx = candidates[i].second.first;
					int fy = candidates[i].second.second;
					heightMap[fy][fx] = 1;
				}
			}
		}
	}

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
	//		NOTICE : HeightMap.size() == elevationMap.size() * 2;

	for (int x = 1; x < widthx2 - 2; ++x) {
		for (int y = 1; y < heightx2 - 2; ++y) {
			// Check 2x2 block
			bool w1 = walkableMap[x][y];
			bool w2 = walkableMap[x][y + 1];
			bool w3 = walkableMap[x + 1][y];
			bool w4 = walkableMap[x + 1][y + 1];

			int walkableCount = w1 + w2 + w3 + w4;

			if (walkableCount >= 3) {
				// Majority walkable — flatten elevation
				float avgElevation =
						(heightMap[x][y] +
								heightMap[x][y + 1] +
								heightMap[x + 1][y] +
								heightMap[x + 1][y + 1]) /
						4.0f;

				heightMap[x][y] = avgElevation;
				heightMap[x][y + 1] = avgElevation;
				heightMap[x + 1][y] = avgElevation;
				heightMap[x + 1][y + 1] = avgElevation;
			}
		}
	}

	// Option B : Weighted Smoothing of Walkable Area's
	// for (int x = 1; x < widthx2 - 2; ++x) {
	// 	for (int y = 1; y < heightx2 - 2; ++y) {
	// 		// Check 2x2 block
	// 		bool w00 = walkableMap[x][y];
	// 		bool w01 = walkableMap[x][y + 1];
	// 		bool w10 = walkableMap[x + 1][y];
	// 		bool w11 = walkableMap[x + 1][y + 1];

	// 		int walkableCount = w00 + w01 + w10 + w11;

	// 		if (walkableCount >= 3) {
	// 			// Majority walkable — smooth elevation
	// 			float avgElevation =
	// 					(heightMap[x][y] +
	// 							heightMap[x][y + 1] +
	// 							heightMap[x + 1][y] +
	// 							heightMap[x + 1][y + 1]) /
	// 					4.0f;

	// 			// Blend each cell toward the average
	// 			float blendFactor = 0.5f; // 0.0 = no change, 1.0 = full overwrite

	// 			heightMap[x][y] = heightMap[x][y] * (1.0f - blendFactor) + avgElevation * blendFactor;
	// 			heightMap[x][y + 1] = heightMap[x][y + 1] * (1.0f - blendFactor) + avgElevation * blendFactor;
	// 			heightMap[x + 1][y] = heightMap[x + 1][y] * (1.0f - blendFactor) + avgElevation * blendFactor;
	// 			heightMap[x + 1][y + 1] = heightMap[x + 1][y + 1] * (1.0f - blendFactor) + avgElevation * blendFactor;
	// 		}
	// 	}
	// }

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
			int n1 = heightMap[y][x];
			int n2 = heightMap[y][x + 1];
			int n3 = heightMap[y + 1][x];
			int n4 = heightMap[y + 1][x + 1];

			// Determine the Elevation Value
			//
			//	Elevation of tile is the max of the neighbors elevation,
			//	assuming that the random noise is consistent in its spread
			//
			int elevation = max({ n1, n2, n3, n4 });

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

			// Corner North
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 2 |
			// +----+----+  +---+---+
			//
			if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n4 > n1 && n4 > n2 && n4 > n3) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || !walkableMap[x][y]) {
					tileMap[x][y] = CLIFF_CORNER;
				} else {
					tileMap[x][y] = RAMP_CORNER;
				}
				tilesRotation = NORTH;
			}
			// Corner South
			// +----+----+  +---+---+
			// | n1 | n2 |  | 2 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 > n2 && n1 > n3 && n1 > n4) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || !walkableMap[x][y]) {
					tileMap[x][y] = CLIFF_CORNER;
				} else {
					tileMap[x][y] = RAMP_CORNER;
				}
				tilesRotation = SOUTH;
			}
			// Corner East
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n2 > n1 && n2 > n3 && n2 > n4) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || !walkableMap[x][y]) {
					tileMap[x][y] = CLIFF_CORNER;
				} else {
					tileMap[x][y] = RAMP_CORNER;
				}
				tilesRotation = EAST;
			}
			// Corner East
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 0 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n3 > n1 && n3 > n2 && n3 > n4) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || !walkableMap[x][y]) {
					tileMap[x][y] = CLIFF_CORNER;
				} else {
					tileMap[x][y] = RAMP_CORNER;
				}
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
			if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 < n3 && n1 < n4 && n2 < n4 && n2 < n3) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || !walkableMap[x][y]) {
					tileMap[x][y] = CLIFF;
				} else {
					tileMap[x][y] = RAMP;
				}
				tilesRotation = NORTH;
			}
			// Cliff's Edge South
			// +----+----+  +---+---+
			// | n1 | n2 |  | 2 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 1 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 > n3 && n1 > n4 && n2 > n4 && n2 > n3) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || !walkableMap[x][y]) {
					tileMap[x][y] = CLIFF;
				} else {
					tileMap[x][y] = RAMP;
				}
				tilesRotation = SOUTH;
			}
			// Cliff's Edge East
			// +----+----+  +---+---+
			// | n1 | n2 |  | 2 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 2 | 1 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 < n2 && n1 < n4 && n3 < n2 && n3 < n4) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || !walkableMap[x][y]) {
					tileMap[x][y] = CLIFF;
				} else {
					tileMap[x][y] = RAMP;
				}
				tilesRotation = EAST;
			}
			// Cliff's Edge West
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 2 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 2 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0 && n1 > n2 && n1 > n4 && n3 > n2 && n3 > n4) {
				// If Not-Walkable, Valid Cliff
				if (slope < cliffThreshold || !walkableMap[x][y]) {
					tileMap[x][y] = CLIFF;
				} else {
					tileMap[x][y] = RAMP;
				}
				tilesRotation = WEST;
			}

			/*****************************************************

				Grid Map Cell Setter

			*****************************************************/
			elevationMap[x][y] = elevation;
			myGridMap->set_cell_item(Vector3i(x, elevation, y), tileMap[x][y], tilesRotation);
		}
	}

	/*****************************************************

		Open Area Finder

			Find areas that have 3x3 flat areas
			Used for placing Large Structures

	*****************************************************/
	// Empty FlatZones
	flatZones.clear();

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

			Use Walkable map to find non-walkable area's as placeable area's
			Use Cliffs & Ramp's Placement as non-placeable area's

	*****************************************************/

	// Generate Placeable Area's

	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			bool isNonWalkable = !walkableMap[x][y];
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

	float minDistance = 3.0f; // Minimum spacing between objects

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

	//TODO : Return data needed for object placement's
	//TODO : Return flatZones/openArea's
	//TODO : Return poisson disk sampling results

	Dictionary result;

	// Convert poissonSamples → Array<Vector3i>
	Array poissonPoints;
	for (const Point &p : poissonSamples) {
		int elevation = elevationMap[p.x][p.y]; // Assuming elevationMap is [width][height]
		poissonPoints.push_back(Vector3i(p.x, elevation, p.y));
	}
	result["poissonPoints"] = poissonPoints;

	// Convert flatZones → Array<Dictionary>
	Array flatZonePoints;
	for (const Flat3x3s &zone : flatZones) {
		flatZonePoints.push_back(Vector3i(zone.x, zone.elevation, zone.y));
	}
	result["flatZonePoints"] = flatZonePoints;

	// Convert elevationMap → Array<Array<int>>
	Array elevationArray;
	for (const auto &row : elevationMap) {
		Array inner;
		for (int val : row) {
			inner.push_back(val);
		}
		elevationArray.push_back(inner);
	}
	result["elevationMap"] = elevationArray;

	return result;
}

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "GridMap", "height", "width", "elevationMax", "seed", "noiseType", "waterRemoval", "cliffsThreshold", "noiseFreq"), &TerrainGen::generate);
}