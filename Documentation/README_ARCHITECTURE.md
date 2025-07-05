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

## Godot GDExtension

[This repository uses the Godot quickstart template for GDExtension development with Godot 4.0+.](https://github.com/godotengine/godot-cpp-template)

### Contents

- godot-cpp as a submodule (`godot-cpp/`)
- (`demo/`) Godot 4.4 Project that tests the Extension
- preconfigured source files for C++ development of the GDExtension (`src/`)
- setup to automatically generate `.xml` files in a `doc_classes/` directory to be parsed by Godot as [GDExtension built-in documentation](https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/gdextension_docs_system.html)

## Github

_Currently Commented Out for Base Development_

- GitHub Issues template (`.github/ISSUE_TEMPLATE.yml`)
- GitHub CI/CD workflows to publish your library packages when creating a release (`.github/workflows/builds.yml`)

This repository comes with a GitHub action that builds the GDExtension for cross-platform use. It triggers automatically for each pushed change. You can find and edit it in [builds.yml](.github/workflows/builds.yml).
After a workflow run is complete, you can find the file `godot-cpp-template.zip` on the `Actions` tab on GitHub.
