extends TerrainGen

enum NoiseType {
		TYPE_VALUE = 5,
		TYPE_VALUE_CUBIC = 4,
		TYPE_PERLIN = 3,
		TYPE_CELLULAR = 2,
		TYPE_SIMPLEX = 0,
		TYPE_SIMPLEX_SMOOTH = 1,
	};


func _ready():
	var grid_map = $"../GridMap"
	var seed_value = int(Time.get_unix_time_from_system()) % 1000000;
	grid_map.cell_size = Vector3(1, 0.25, 1);
	generate(grid_map, 100, 100, 3, seed_value, NoiseType.TYPE_SIMPLEX, 2, 0.79, 0.03);
