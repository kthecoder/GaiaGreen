extends TerrainGen

var simple_cube = preload("res://Prefabs/SimpleCube.tscn")

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
	#generate(grid_map, 128, 128, 4, seed_value, 12, NoiseType.VALUE, 0.79, 0.1);

	# Simplex Noise Based
	var result: Dictionary = generate(grid_map, 128, 128, 4, seed_value, 12, NoiseType.SIMPLEX, 0.6, 0.005, 0.015);

	#
	#
	#	Validate the output of the Generation Result
	#
	#		Returns World Points for object placement
	#

	#var elevation_map: Array = result["elevationMap"]
	#print("Elevation at (0,0): ", elevation_map[0][0])

	var flat_zones: Array = result["flatZones"]
	
	for zone in flat_zones:
		var new_object = simple_cube.instantiate();
		var spawn_position = zone 
		new_object.global_transform.origin = Vector3(spawn_position) + Vector3(0, 0.27, 0)
		add_child(new_object)
#		
	## Access Poisson samples
	#var poisson_points: Array = result["poissonPoints"]
	#for point in poisson_points:
		#print("Poisson sample at: ", point)
