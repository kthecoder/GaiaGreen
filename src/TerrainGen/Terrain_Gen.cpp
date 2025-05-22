#include "Terrain_Gen.h"

using namespace godot;

TerrainGen::TerrainGen() {
	noise.instantiate();
}

TerrainGen::~TerrainGen() {
}

void TerrainGen::generate(GridMap *myGridMap, int height, int width, int depth, int seed, int noiseOctaves, float noiseFreq) {
	UtilityFunctions::print("Begin Terrain Generation!");

	/*
		Setup the Noise Function
	*/

	noise->set_noise_type(FastNoiseLite::NoiseType::TYPE_SIMPLEX);
	noise->set_fractal_type(FastNoiseLite::FractalType::FRACTAL_FBM);
	noise->set_seed(seed);
	noise->set_fractal_octaves(noiseOctaves);
	noise->set_fractal_lacunarity(2.0);
	noise->set_fractal_gain(0.5);
	noise->set_frequency(noiseFreq);

	/*
		Function dependent Variables
	*/

	vector<vector<vector<int>>> gridMap(
			depth, vector<vector<int>>(height, vector<int>(width, 0)));

	vector<vector<float>> heightMap(height, vector<float>(width, 0.0f));

	/*
		Height Map Generation
	*/

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float n = noise->get_noise_2d((float)x, (float)y);
			heightMap[y][x] = round(((n + 1.0f) / 2.0f) * (depth - 1));
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

	/*
		Return the Grid Map to Godot in a GDScript consumable manner
	*/

	// Array terrainMap;
	// for (size_t z = 0; z < depth; z++) { // Iterate over depth (Z-axis)
	// 	Array grid_layer;
	// 	for (size_t y = 0; y < height; y++) { // Iterate over height (Y-axis)
	// 		TypedArray<int> grid_row;
	// 		for (size_t x = 0; x < width; x++) { // Iterate over width (X-axis)
	// 			grid_row.append(gridMap[z][y][x]); // Append voxel values
	// 		}
	// 		grid_layer.append(grid_row);
	// 	}
	// 	terrainMap.append(grid_layer);
	// }
}

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "Grid Map"
											  "height",
								 "width", "depth", "seed", "noiseOctaves", "noiseFreq"),
			&TerrainGen::generate);
}