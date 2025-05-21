#include "Terrain_Gen.h"

using namespace godot;

TerrainGen::TerrainGen() {
	noise.instantiate();
	noise->set_noise_type(FastNoiseLite::NoiseType::TYPE_SIMPLEX);
	noise->set_fractal_type(FastNoiseLite::FractalType::FRACTAL_FBM);
	noise->set_fractal_octaves(2); // Number of layers
	noise->set_fractal_lacunarity(2.0); // Frequency multiplier per octave
	noise->set_fractal_gain(0.5); // Amplitude multiplier per octave
	noise->set_frequency(0.01);
}

TerrainGen::~TerrainGen() {
}

float TerrainGen::get_noise_value(float x, float y) {
	return noise->get_noise_2d(x, y);
}

void TerrainGen::generate() {
	UtilityFunctions::print("Begin Terrain Generation!");
}

void TerrainGen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("generate"), &TerrainGen::generate);
}