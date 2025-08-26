#ifndef TERRAIN_GEN_H
#define TERRAIN_GEN_H

#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/grid_map.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <random>
#include <vector>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;
using namespace std;

class TerrainGen : public Node {
	GDCLASS(TerrainGen, Node);

protected:
	// Values for Godot's GridMap Rotation input
	int NORTH = 0; // No Rotation
	int EAST = 16; // 90° Y-axis rotation
	int SOUTH = 10; // 180° Y-axis rotation
	int WEST = 22; // 270° Y-axis rotation

	// Tile Definitions
	enum TileType {
		WATER, // Pure water tile, Connects to bottom of Water Edge tiles and other Water tiles
		WATER_CORNER, // Water corner with a section of ground at the top height, Connects to corners of water on one side and ground on the other
		WATER_EDGE, // Water with a section of ground at the top height, Connects to water and ground on opposite sides
		GROUND, // Pure ground tile, Simple tile that connects to ground, top of ramp, and top of cliffs
		RAMP, // Ground to Ground tile of 45 degree angle, Connects ground tiles for elevation changes
		RAMP_CORNER, // Connector of ramps and cliffs on its corners, Connects to Ground tiles and can be next to cliffs
		CLIFF, // Cliffs separate elevation and on its sides can connect to cliffs & ramps, Connects to ground tiles & Water Edge for elevation changes
		CLIFF_CORNER, // Connector of ramps and cliffs on its corners, Connects to Ground & Water edge tiles, can also be next to Ramps
		// +----+----+
		// | n1 | n2 |
		// +----+----+
		// | n3 | n4 |
		// +----+----+
		//
		// n1 = GROUND elevation 1, n2 = CLIFF_EDGE elevation 2, n3 = CLIFF_EDGE elevation 2, n4 = CLIFF_CORNER_INNER elevation 2
		CLIFF_CORNER_INNER, // Connector for when two cliff edge's connect inward
		WATER_CORNER_INNER // Connector for when two water edge's connect inward
	};

	Ref<FastNoiseLite> noise;

	static void _bind_methods();

public:
	TerrainGen();
	~TerrainGen();

	//Generate Terrain
	//Takes in a height & width for size of map on the X & Z axis
	Dictionary generate(
			GridMap *myGridMap,
			int height,
			int width,
			int elevationMax,
			int seed,
			int openAreaMin,
			int noiseType,
			double waterRemoval,
			float cliffThreshold = 0.2,
			float noiseFreq = 0.005);
};

#endif