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

	// TODO : Fix blocksize so it doesn't cause errors
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
	vector<vector<float>> rawNoise(widthx2, vector<float>(heightx2, 0));
	// Data Grid -> Simply an Elevation Map double the size of our Render Grid
	vector<vector<int>> heightMap(width * 2, vector<int>(height * 2, 0));
	// Render Grid -> Real size grid with final tile type values
	vector<vector<TileType>> tileMap(width, vector<TileType>(height, GROUND));
	// Render Grid's Elevation Values -> Real size grid with elevation ints
	vector<vector<int>> elevationMap(width, vector<int>(height, 0)); // Unfiltered Elevation | Includes 0 Elevations
	vector<vector<int>> elevationMapTiles(width, vector<int>(height, 0)); // Filtered Elevation | Used Elevation in Tile Map
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
				(Cardinal Neighbors Only)

	*****************************************************/
	int neighborRadius = 1; // 3x3 neighborhood (cardinal only)
	int threshold = 2; // Minimum matching neighbors to preserve center
	int iterations = 1; // Number of CA generations to apply

	vector<vector<int>> curr = heightMap;
	vector<vector<int>> next = heightMap;

	// Cardinal directions: N, S, E, W
	const vector<pair<int, int>> cardinalDirs = {
		{ 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 }
	};

	for (int it = 0; it < iterations; ++it) {
		bool anyChanged = false;

		for (int x = 0; x < widthx2; ++x) {
			for (int y = 0; y < heightx2; ++y) {
				const int center = curr[x][y];

				// Count neighbors matching the center value
				int matchCount = 0;
				for (const auto &[dx, dy] : cardinalDirs) {
					int nx = x + dx, ny = y + dy;
					if (nx < 0 || nx >= widthx2 || ny < 0 || ny >= heightx2)
						continue;
					if (curr[nx][ny] == center)
						++matchCount;
				}

				// Default: preserve current value
				int newVal = center;

				// Rule 2: If not enough matching neighbors, resolve via dominant neighbor value
				if (matchCount < threshold) {
					vector<int> freq(elevationMax + 1, 0);

					// Count frequency of each cardinal neighbor value
					for (size_t i = 0; i < cardinalDirs.size(); ++i) {
						int dx = cardinalDirs[i].first;
						int dy = cardinalDirs[i].second;
						int nx = x + dx, ny = y + dy;
						if (nx < 0 || nx >= widthx2 || ny < 0 || ny >= heightx2)
							continue;
						++freq[curr[nx][ny]];
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
					if (maxCount <= 1) {
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

		curr.swap(next);
		if (!anyChanged)
			break;
	}

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

	int maxRiverWidth = 1; // Max number of perpendicular cells (to river flow direction) to mark

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

	/*****************************************************

		Enforce Square Patterns

			Due to the Cellular Automata Filters, the
			square like patterns are pushed into rounded
			patterns.
			Ensure the pattern returns to a square pattern
			so that the isometric tiles are useable.

	*****************************************************/

	int enforcement = 2;

	for (int i = 0; i < enforcement; i++) {
		for (int x = 0; x < width - 1; x += 2) {
			for (int y = 0; y < height - 1; y += 2) {
				int a = heightMap[x][y];
				int b = heightMap[x + 1][y];
				int c = heightMap[x][y + 1];
				int d = heightMap[x + 1][y + 1];

				// If already uniform or already a clean vertical/horizontal split, do nothing.
				bool uniform = (a == b && a == c && a == d);
				bool vertical = (a == c && b == d && a != b);
				bool horizontal = (a == b && c == d && a != c);
				if (uniform || vertical || horizontal)
					continue;

				// Count frequency of each value manually
				int countA = 0, countB = 0, countC = 0, countD = 0;
				if (a == a)
					countA++;
				if (b == a)
					countA++;
				if (c == a)
					countA++;
				if (d == a)
					countA++;

				if (a != b) {
					if (b == b)
						countB++;
					if (a == b)
						countB++; // already counted
					if (c == b)
						countB++;
					if (d == b)
						countB++;
				}

				if (a != c && b != c) {
					if (c == c)
						countC++;
					if (a == c)
						countC++;
					if (b == c)
						countC++;
					if (d == c)
						countC++;
				}

				if (a != d && b != d && c != d) {
					if (d == d)
						countD++;
					if (a == d)
						countD++;
					if (b == d)
						countD++;
					if (c == d)
						countD++;
				}

				// Determine mode (most frequent value)
				int mode = a;
				int maxCount = countA;
				if (countB > maxCount) {
					mode = b;
					maxCount = countB;
				}
				if (countC > maxCount) {
					mode = c;
					maxCount = countC;
				}
				if (countD > maxCount) {
					mode = d;
					maxCount = countD;
				}

				// Snap all four cells to the mode
				heightMap[x][y] = mode;
				heightMap[x + 1][y] = mode;
				heightMap[x][y + 1] = mode;
				heightMap[x + 1][y + 1] = mode;
			}
		}
	}

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
			int n1 = heightMap[x][y];
			int n2 = heightMap[x + 1][y];
			int n3 = heightMap[x][y + 1];
			int n4 = heightMap[x + 1][y + 1];

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
			if (n1 == n2 && n2 == n3 && n3 == n4 && n1 > 0 && n2 > 0 && n3 > 0 && n4 > 0) {
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

			// Water's Corner WEST
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 0 | 1 |
			// +----+----+  +---+---+
			//
			if (n1 == 0 && n2 == 0 && n3 == 0 && n4 > 0) {
				tileMap[x][y] = WATER_CORNER;
			}
			// Water's Corner EAST
			// +----+----+  +---+---+
			// | n1 | n2 |  | 1 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 0 | 0 |
			// +----+----+  +---+---+
			//
			else if (n1 > 0 && n2 == 0 && n3 == 0 && n4 == 0) {
				tileMap[x][y] = WATER_CORNER;
			}
			// Water's Corner SOUTH
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 1 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 0 | 0 |
			// +----+----+  +---+---+
			//
			else if (n1 == 0 && n2 > 0 && n3 == 0 && n4 == 0) {
				tileMap[x][y] = WATER_CORNER;
			}
			// Water's Corner NORTH
			// +----+----+  +---+---+
			// | n1 | n2 |  | 0 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 0 |
			// +----+----+  +---+---+
			//
			else if (n1 == 0 && n2 == 0 && n3 > 0 && n4 == 0) {
				tileMap[x][y] = WATER_CORNER;
			}

			//-------------------------//
			// Cliff's & Ramp's Corner
			//-------------------------//

			// TODO : Ramp Corner needs a Ground Tile placed one elevation below it

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
			// | n1 | n2 |  | 0 | 0 |
			// +----+----+  +---+---+
			// | n3 | n4 |  | 1 | 0 |
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

	//
	// Phase 2 : Determine the Tile's Rotation
	//
	//	Tile Rotations are dependent on the surrounding tile types
	//
	//
	// +----+----+----+  +----+----+----+
	// | m1 | m2 | m3 |  | n1 | n2 | n3 |
	// +----+----+----+  +----+----+----+
	// | m4 | c  | m5 |  | n4 | c  | n5 |
	// +----+----+----+  +----+----+----+
	// | m6 | m7 | m8 |  | n6 | n7 | n8 |
	// +----+----+----+  +----+----+----+
	//
	// for (int x = 0; x < width; x++) {
	// 	for (int y = 0; y < height; y++) {
	// 		int c = elevationMap[y][x];
	// 		int n = myGridMap->get_cell_item(Vector3i(x, c, y));

	// 		if (n == WATER || n == GROUND) { // Water & Ground Don't need rotations
	// 			break;
	// 		}

	// 		auto safe_height = [&](int xx, int yy, int c_height) {
	// 			if (xx < 0 || yy < 0 || xx >= width || yy >= height)
	// 				return c_height; // out of bounds → pretend it's the center
	// 			return elevationMap[xx][yy];
	// 		};

	// 		int m1 = safe_height(x - 1, y - 1, c);
	// 		int m2 = safe_height(x - 1, y, c);
	// 		int m3 = safe_height(x - 1, y + 1, c);
	// 		int m4 = safe_height(x, y - 1, c);
	// 		int m5 = safe_height(x, y + 1, c);
	// 		int m6 = safe_height(x + 1, y - 1, c);
	// 		int m7 = safe_height(x + 1, y, c);
	// 		int m8 = safe_height(x + 1, y + 1, c);

	// 		// Empty Cell's return -1

	// 		int n1 = myGridMap->get_cell_item(Vector3i(x, m1, y));
	// 		int n2 = myGridMap->get_cell_item(Vector3i(x, m2, y));
	// 		int n3 = myGridMap->get_cell_item(Vector3i(x, m3, y));
	// 		int n4 = myGridMap->get_cell_item(Vector3i(x, m4, y));
	// 		int n5 = myGridMap->get_cell_item(Vector3i(x, m5, y));
	// 		int n6 = myGridMap->get_cell_item(Vector3i(x, m6, y));
	// 		int n7 = myGridMap->get_cell_item(Vector3i(x, m7, y));
	// 		int n8 = myGridMap->get_cell_item(Vector3i(x, m8, y));

	// 		// TODO : Determine rotation based on neighbors

	// 		myGridMap->set_cell_item(Vector3i(x, elevationMap[x][y], y), tileMap[x][y], NORTH);
	// 	}
	// }

	//
	// Phase 2 : Determine the Tile's Rotation
	// Rotations depend on surrounding tile types and elevations.
	// We point uphill toward the highest GROUND neighbor (cardinals only).
	//
	// for (int x = 0; x < width; x++) {
	// 	for (int y = 0; y < height; y++) {
	// 		// Current cell elevation and tile id
	// 		int c_height = elevationMapTiles[x][y];
	// 		int tile_id = myGridMap->get_cell_item(Vector3i(x, c_height, y));

	// 		// Skip tiles that don't need rotation
	// 		if (tile_id == WATER || tile_id == GROUND) {
	// 			continue;
	// 		}

	// 		// Safe elevation fetch that treats OOB as flat (center height)
	// 		auto safe_height = [&](int gx, int gy, int fallback_h) {
	// 			if (gx < 0 || gy < 0 || gx >= width || gy >= height) {
	// 				return fallback_h;
	// 			}
	// 			return elevationMap[gx][gy];
	// 		};

	// 		// Safe neighbor tile fetch; OOB → -1 (empty)
	// 		auto safe_tile_at = [&](int gx, int gy) {
	// 			if (gx < 0 || gy < 0 || gx >= width || gy >= height) {
	// 				return -1;
	// 			}
	// 			int h = elevationMap[gx][gy];
	// 			return myGridMap->get_cell_item(Vector3i(gx, h, gy));
	// 		};

	// 		int best_dir = NORTH;
	// 		float best_diff = -1e9f;

	// 		{
	// 			int nx = x + 0;
	// 			int ny = y - 1;
	// 			int nh = safe_height(nx, ny, c_height);
	// 			int nt = safe_tile_at(nx, ny);
	// 			if (nt == GROUND) {
	// 				float diff = float(nh) - float(c_height);
	// 				if (diff > best_diff) {
	// 					best_diff = diff;
	// 					best_dir = SOUTH;
	// 				}
	// 			}
	// 		}

	// 		{
	// 			int nx = x + 0;
	// 			int ny = y + 1;
	// 			int nh = safe_height(nx, ny, c_height);
	// 			int nt = safe_tile_at(nx, ny);
	// 			if (nt == GROUND) {
	// 				float diff = float(nh) - float(c_height);
	// 				if (diff > best_diff) {
	// 					best_diff = diff;
	// 					best_dir = NORTH;
	// 				}
	// 			}
	// 		}

	// 		{
	// 			int nx = x + 1;
	// 			int ny = y + 0;
	// 			int nh = safe_height(nx, ny, c_height);
	// 			int nt = safe_tile_at(nx, ny);
	// 			if (nt == GROUND) {
	// 				float diff = float(nh) - float(c_height);
	// 				if (diff > best_diff) {
	// 					best_diff = diff;
	// 					best_dir = WEST;
	// 				}
	// 			}
	// 		}

	// 		// WEST (-1, 0)
	// 		{
	// 			int nx = x - 1;
	// 			int ny = y + 0;
	// 			int nh = safe_height(nx, ny, c_height);
	// 			int nt = safe_tile_at(nx, ny);
	// 			if (nt == GROUND) {
	// 				float diff = float(nh) - float(c_height);
	// 				if (diff > best_diff) {
	// 					best_diff = diff;
	// 					best_dir = EAST;
	// 				}
	// 			}
	// 		}

	// 		// If no GROUND neighbor exists, optionally fall back to steepest slope regardless of type
	// 		if (best_diff <= -1e8f) {
	// 			// Evaluate all four directions using elevation only
	// 			{
	// 				int nx = x + 0, ny = y - 1;
	// 				int nh = safe_height(nx, ny, c_height);
	// 				float diff = float(nh) - float(c_height);
	// 				if (diff > best_diff) {
	// 					best_diff = diff;
	// 					best_dir = SOUTH;
	// 				}
	// 			}
	// 			{
	// 				int nx = x + 0, ny = y + 1;
	// 				int nh = safe_height(nx, ny, c_height);
	// 				float diff = float(nh) - float(c_height);
	// 				if (diff > best_diff) {
	// 					best_diff = diff;
	// 					best_dir = NORTH;
	// 				}
	// 			}
	// 			{
	// 				int nx = x + 1, ny = y + 0;
	// 				int nh = safe_height(nx, ny, c_height);
	// 				float diff = float(nh) - float(c_height);
	// 				if (diff > best_diff) {
	// 					best_diff = diff;
	// 					best_dir = WEST;
	// 				}
	// 			}
	// 			{
	// 				int nx = x - 1, ny = y + 0;
	// 				int nh = safe_height(nx, ny, c_height);
	// 				float diff = float(nh) - float(c_height);
	// 				if (diff > best_diff) {
	// 					best_diff = diff;
	// 					best_dir = EAST;
	// 				}
	// 			}
	// 		}

	// 		myGridMap->set_cell_item(Vector3i(x, c_height, y), tile_id, best_dir);
	// 	}
	// }

	//
	//
	//
	// Phase 2 : Determine the Tile's Rotation
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
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			int c_height = elevationMap[x][y];
			int tile_id = myGridMap->get_cell_item(Vector3i(x, c_height, y));
			if (tile_id == -1 || tile_id == WATER || tile_id == GROUND)
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

			int rotation_val = NORTH; // default face −Z

			//
			// Cardinal Neighbors
			//
			int sHeight = safe_height(x, y - 1, c_height);
			int sTile = safe_tile_at(x, y - 1); // m2

			int nHeight = safe_height(x, y + 1, c_height);
			int nTile = safe_tile_at(x, y + 1); // m7

			int eHeight = safe_height(x + 1, y, c_height);
			int eTile = safe_tile_at(x + 1, y); // m5

			int wHeight = safe_height(x - 1, y, c_height);
			int wTile = safe_tile_at(x - 1, y); // m4

			//
			// Diagonal Neighbors
			//
			int neHeight = safe_height(x + 1, y + 1, c_height);
			int neTile = safe_tile_at(x + 1, y + 1); // m3

			int seHeight = safe_height(x + 1, y - 1, c_height);
			int seTile = safe_tile_at(x + 1, y - 1); // m8

			int swHeight = safe_height(x - 1, y - 1, c_height);
			int swTile = safe_tile_at(x - 1, y - 1); // m6

			int nwHeight = safe_height(x - 1, y + 1, c_height);
			int nwTile = safe_tile_at(x - 1, y + 1); // m1

			// +----+----+  +----+----+
			// | n1 | n2 |  | g1 | r2 |
			// +----+----+  +----+----+
			// | n3 | n4 |  | r2 | g2 |
			// +----+----+  +----+----+
			//
			// ? : Above is an edge case that needs fixed
			// TODO : Replace the Ramps in this situation with CLiff edges using the orientation of the ramp's

			if (tile_id == RAMP || tile_id == CLIFF || tile_id == WATER_EDGE) {
				// +----+----+----+
				// | m1 | m2 | m3 |
				// +----+----+----+
				// | m4 | T  | m5 |
				// +----+----+----+
				// | m6 | m7 | m8 |
				// +----+----+----+

				// Cardinal's
				//
				//	N (m7), E (m5), S (m2), W (m4)
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
					rotation_val = NORTH;
				}

				// South is higher than North
				// Point to m2
				if (sHeight == nHeight + 1 && static_cast<TileType>(sTile) == GROUND) {
					rotation_val = SOUTH;
				}

				// East is higher than West
				// Point to m5
				if (eHeight == wHeight + 1 && static_cast<TileType>(eTile) == GROUND) {
					rotation_val = EAST;
				}

				// West is higher than East
				// Point to m4
				if (wHeight == eHeight + 1 && static_cast<TileType>(wTile) == GROUND) {
					rotation_val = WEST;
				}
			}

			// T = Target Cell
			// +----+----+----+
			// | m1 | m2 | m3 |
			// +----+----+----+
			// | m4 | T  | m5 |
			// +----+----+----+
			// | m6 | m7 | m8 |
			// +----+----+----+
			//
			// T should consider m2 + m5, m5 + m7, m7 + m4, and m4 + m2; for cliffs and ramps
			// Then it should find the highest elevation of ground piece at, m3, m8, m6, m1
			// Then it should decide the rotation by rotating the corner toward the higher elevation
			//
			// choose corner defined by edge tiles around T, aim toward the higher diagonal ground
			//
			else if (tile_id == RAMP_CORNER || tile_id == CLIFF_CORNER || tile_id == WATER_CORNER) {
				int ROT_CORNER_NE = WEST;
				int ROT_CORNER_SE = EAST;
				int ROT_CORNER_SW = SOUTH;
				int ROT_CORNER_NW = NORTH;

				auto pos = [](float v) { return v > 0.f ? v : 0.f; };

				// Decide which tile types count as "edge" for the current corner type
				auto is_edge_for_corner = [&](TileType t) -> bool {
					if (tile_id == WATER_CORNER) {
						return t == WATER_EDGE;
					} else if (tile_id == CLIFF_CORNER) {
						return t == CLIFF;
					} else {
						return t == RAMP;
					}
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

				// +----+----+----+
				// | m1 | m2 | m3 |
				// +----+----+----+
				// | m4 | T  | m5 |
				// +----+----+----+
				// | m6 | m7 | m8 |
				// +----+----+----+

				// Diagonal's
				//
				// NE (m3), SE (m8), SW (m6), NW (m1)
				// if -1, not ground tile
				vector<int> dElevation = { -1, -1, -1, -1 };
				if (neGround) {
					dElevation[0] = neHeight;
				}

				if (seGround) {
					dElevation[1] = seHeight;
				}

				if (swGround) {
					dElevation[2] = swHeight;
				}

				if (nwGround) {
					dElevation[3] = nwHeight;
				}

				if (!(nEdge && eEdge)) { // m2 + m5 | m3
					dElevation[0] = -1;
				}

				if (!(sEdge && eEdge)) { // m5 + m7 | m8
					dElevation[1] = -1;
				}

				if (!(sEdge && wEdge)) { // m4 + m7 | m6
					dElevation[2] = -1;
				}

				if (!(nEdge && wEdge)) { // m4 + m2 | m1
					dElevation[3] = -1;
				}

				int maxElevation = dElevation[0];
				int dEleIndex = 0;

				for (size_t i = 1; i < dElevation.size(); ++i) {
					if (dElevation[i] > maxElevation) {
						maxElevation = dElevation[i];
						dEleIndex = static_cast<int>(i);
					}
				}

				switch (dEleIndex) {
					case 0:
						rotation_val = EAST;
						break;
					case 1:
						rotation_val = SOUTH;
						break;
					case 2:
						rotation_val = WEST;
						break;
					case 3:
						rotation_val = NORTH;
						break;
					default:
						rotation_val = NORTH;
						break;
				}
			}

			if (tile_id == RAMP_CORNER) {
				// Place Ground under Ramp Tiles
				myGridMap->set_cell_item(Vector3i(x, c_height - 1, y), GROUND, rotation_val);
			}

			myGridMap->set_cell_item(Vector3i(x, c_height, y), tile_id, rotation_val);
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
	result["flatZonePoints"] = flatZonePoints;

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
	ClassDB::bind_method(D_METHOD("generate", "GridMap", "height", "width", "elevationMax", "seed", "openAreaMin", "noiseType", "waterRemoval", "cliffsThreshold", "noiseFreq"), &TerrainGen::generate);
}