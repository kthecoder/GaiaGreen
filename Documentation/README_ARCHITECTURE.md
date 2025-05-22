# GaiaGreen

Godot Terrain Generator for 3D Tiled Maps

## Usage

Requires a C++ compiler such as MSVC or MSY2

### Building :

1. `scons use_mingw=yes` or just `scons`

## Debugging:

1. https://youtu.be/8WSIMTJWCBk?t=3624
1. `scons target=template_debug debug_symbols=yes`

Launch.json

```json
{
	"version": "0.2.0",
	"configurations": [
		{
			"type": "lldb",
			"request": "launch",
			"preLaunchTask": "build",
			"name": "Debug",
			"program": "<path to godot>/Godot 4.4.1.exe",
			"args": ["--path", "<path to demo project>/GaiaGreen/demo"],
			"cwd": "${workspaceFolder}"
		}
	]
}
```

Tasks.json:

```json
{
	"version": "2.0.0",
	"tasks": [
		{
			"label": "build",
			"type": "shell",
			"command": "scons -j12 target=template_debug debug_symbols=yes"
		}
	]
}
```
