#ifndef TERRAIN_GEN_H
#define TERRAIN_GEN_H

#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/grid_map.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
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
	enum TileType {
		WATER, // Pure water tile, Connects to bottom of Water Edge tiles and other Water tiles
		WATER_CORNER, // Water corner with a section of ground at the top height, Connects to corners of water on one side and ground on the other
		WATER_EDGE, // Water with a section of ground at the top height, Connects to water and ground on opposite sides
		GROUND, // Pure ground tile, Simple tile that connects to ground, top of ramp, and top of cliffs
		RAMP, // Ground to Ground tile of 45 degree angle, Connects ground tiles for elevation changes
		RAMP_CORNER, // Connector of ramps and cliffs on its corners, Connects to Ground tiles and can be next to cliffs
		CLIFF, // Cliffs separate elevation and on its sides can connect to cliffs & ramps, Connects to ground tiles & Water Edge for elevation changes
		CLIFF_CORNER // Connector of ramps and cliffs on its corners, Connects to Ground & Water edge tiles, can also be next to Ramps
	};

	Ref<FastNoiseLite> noise;

	static void _bind_methods();

private:
	TileType isCornerTile(int x, int y, vector<vector<TileType>> &tileMap);

public:
	TerrainGen();
	~TerrainGen();

	//Generate Terrain
	//Takes in a height & width for size of map on the X & Z axis
	void generate(GridMap *myGridMap, int height, int width, int depth, int seed, int noiseType, double waterRemoval, float noiseFreq = 0.005);
};

#endif