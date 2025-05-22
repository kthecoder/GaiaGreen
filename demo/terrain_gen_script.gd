extends TerrainGen


func _ready():
	var grid_map = $"../GridMap"
	var seed_value = int(Time.get_unix_time_from_system()) % 1000000;
	grid_map.cell_size = Vector3(1, 0.25, 1);
	generate(grid_map, 100, 100, 3, seed_value, false, 2, 0.03);
