extends TerrainGen


func _ready():
	var seed_value = int(Time.get_unix_time_from_system()) % 1000000;
	generate(get_node("GridMap"), 5, 5, 3, seed_value, 2, 0.005);
