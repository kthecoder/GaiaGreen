extends TerrainGen
	
func _ready():
	var temp : Array = generate(5, 5, 2, 0.005);
	print(temp);
