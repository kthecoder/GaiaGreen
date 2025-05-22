#ifndef HELLOWORLD_H
#define HELLOWORLD_H

#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/grid_map.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <vector>

using namespace godot;
using namespace std;

class TerrainGen : public Node {
	GDCLASS(TerrainGen, Node);

protected:
	Ref<FastNoiseLite> noise;

	static void _bind_methods();

public:
	TerrainGen();
	~TerrainGen();

	//Generate Terrain
	//Takes in a height & width for size of map on the X & Z axis
	void generate(GridMap *myGridMap, int height, int width, int depth, int seed, int noiseOctaves = 2, float noiseFreq = 0.005);
};

#endif