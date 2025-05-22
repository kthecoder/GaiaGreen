#include "Terrain_Gen.h"

using namespace godot;

TerrainGen::TerrainGen() {
	noise.instantiate();
}

TerrainGen::~TerrainGen() {
}

float TerrainGen::get_noise_value(float x, float y, float z) {
	return noise->get_noise_3d(x, y, z);
}

Array TerrainGen::generate(int height, int width, int noiseOctaves, float noiseFreq) {
	UtilityFunctions::print("Begin Terrain Generation!");

	/*
		Setup the Noise Function
	*/

	noise->set_noise_type(FastNoiseLite::NoiseType::TYPE_SIMPLEX);
	noise->set_fractal_type(FastNoiseLite::FractalType::FRACTAL_FBM);
	noise->set_fractal_octaves(noiseOctaves);
	noise->set_fractal_lacunarity(2.0);
	noise->set_fractal_gain(0.5);
	noise->set_frequency(noiseFreq);

	/*
		Function dependent Variables
	*/

	vector<vector<int>> gridMap(height, vector<int>(width, 0));

	/*
		The key rotations in Godot's Grid Map around the Y-axis are:
		- 0 → No rotation
		- 1 → 90° clockwise around Y
		- 2 → 180° around Y
		- 3 → 90° counterclockwise around Y
		- 10 → 180° around Y (alternative basis)
		- 11 → 90° counterclockwise around Y (alternative basis)
		- 9 → 90° clockwise around Y (alternative basis)

		Which can be set with : `gridmap.set_cell_item(x, y, z, item_index, rotation_index)`
	*/

	/*
		Return the Grid Map to Godot in a GDScript consumable manner
	*/

	Array terrainMap;
	for (size_t i = 0; i < gridMap.size(); i++) { // Standard i++ format
		TypedArray<int> grid_row;
		for (size_t j = 0; j < gridMap[i].size(); j++) { // Standard j++ format
			grid_row.append(gridMap[i][j]); // Append value from 2D vector
		}
		terrainMap.append(grid_row);
	}

	return terrainMap;
}

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate", "height", "width", "noiseOctaves", "noiseFreq"), &TerrainGen::generate);
}