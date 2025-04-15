
# BasicRenderer

A hobby DX12 renderer, written to experiment with real-time rendering for undergraduate ISPs and my own interest

Feature development is driven purely by what I'm interested in at the moment.




## Graphical features

- Image-based lighting
- Normal mapping & contact-refinement parallax heightmaps
- Support for arbitrary numbers of point, spot, and directional lights using clustered lighting
- Shadow mapping for all light types
- Skinned meshes
- Order-independent transparency using a per-pixel linked-list

## Technical features

- Clustered forward shading
- A (basic) render graph for automatic resource transitions and queue synchronization
- Async-compute
- Compute-based skinning
- GPU-driven rendering with frustrum culling & ExecuteIndirect
- Meshlets & mesh shaders
- Flecs ECS for scene management
- A basic UI for feature toggles, importing new asset files, debug view selection, and scene graph introspection & modification

## Gallery

![Color example](images/color.png)

![Meshlets example](images/meshlets.png)

## Supported file formats
- Partial assimp loader implemented, https://github.com/assimp/assimp/blob/master/doc/Fileformats.md

## Third-party dependancies

- [nlohmann-json](https://github.com/nlohmann/json)
- [meshoptimizer](https://github.com/zeux/meshoptimizer)
- [spdlog](https://github.com/gabime/spdlog)
- [ImGui](https://github.com/ocornut/imgui)
- [assimp](https://github.com/assimp/assimp)
- [flecs](https://www.flecs.dev/flecs/)

## Notable sources for development ideas

[LearnOpenGL.com](https://learnopengl.com/)

[Erik Svjannson](https://www.youtube.com/watch?v=EtX7WnFhxtQ)'s talk on GPU-driven rendering and mesh shaders


