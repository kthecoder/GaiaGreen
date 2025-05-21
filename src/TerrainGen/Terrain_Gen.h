#ifndef HELLOWORLD_H
#define HELLOWORLD_H

#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

class TerrainGen : public Node {
	GDCLASS(TerrainGen, Node);

protected:
	Ref<FastNoiseLite> noise;

	float TerrainGen::get_noise_value(float x, float y);

	static void _bind_methods();

public:
	TerrainGen();
	~TerrainGen();

	void generate();
};

#endif