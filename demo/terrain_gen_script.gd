extends TerrainGen

enum NoiseType {
		VALUE = 5,
		VALUE_CUBIC = 4,
		PERLIN = 3,
		CELLULAR = 2,
		SIMPLEX = 0,
		SIMPLEX_SMOOTH = 1,
	};


func _ready():
	var grid_map = $"../GridMap"
	var seed_value = int(Time.get_unix_time_from_system()) % 1000000;
	grid_map.cell_size = Vector3(1, 0.25, 1);

	# Value Noise Based
	#generate(grid_map, 128, 128, 4, seed_value, NoiseType.VALUE, 0.79, 0.1);

	# Simplex Noise Based
	generate(grid_map, 128, 128, 4, seed_value, NoiseType.SIMPLEX, 0.3, 0.02);
