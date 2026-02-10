# Vulkanstein3D - Development Notes

## Hybrid GPU Issue (Intel + NVIDIA)

### Proposed Solution: Multi-Device Architecture
Use `VK_KHR_external_memory` to share rendered frames between GPUs:

1. Create two Vulkan devices (NVIDIA for rendering, Intel for presentation)
2. Render to an exportable image on NVIDIA
3. Import that image on Intel via external memory
4. Present from Intel's swapchain

Required extensions:
- `VK_KHR_external_memory`
- `VK_KHR_external_semaphore`
- `VK_KHR_external_fence`

### Relevant Code
- Device selection: `sps/vulkan/device.cpp`
- App initialization: `sps/vulkan/app.cpp`
- PRIME workaround (uncommitted): `device.cpp:100-117` - checks `getSurfaceFormatsKHR` to detect when NVIDIA claims presentation support but can't actually present

## IBL (Image-Based Lighting)

### Implementation
- GPU compute shader generation (4 shaders: equirect_to_cubemap, irradiance, prefilter_env, brdf_lut)
- Settings configurable via `[IBL]` section in TOML (resolution, sample counts)
- Defaults: 256x256 cubemap, 2048 irradiance samples, 2048 prefilter samples, 1024 BRDF samples
- BRDF LUT: 128x128, Smith-GGX correlated visibility (Khronos/Filament reference)
- Irradiance map: 32x32 cubemap, cosine-weighted hemisphere with LOD-filtered sampling
- Prefiltered environment: 256x256 cubemap, GGX importance sampling with mip chain
- Re-runs full compute pipeline on HDR environment switch (near-instant on GPU)

## Render Graph

Implemented with 5 fixed stages in `sps/vulkan/stages/`:
- `RasterOpaqueStage` — OPAQUE + MASK primitives
- `RasterBlendStage` — sorted BLEND primitives, depth write off
- `Debug2DStage` — fullscreen quad texture viewer
- `RayTracingStage` — trace + blit to swapchain
- `UIStage` — ImGui callback

Stages registered in `finalize_setup()`, no-ops via `is_enabled()` + early returns.
`FrameContext` passes per-frame data — stages don't own Vulkan resources.

### Future Improvements
- Stages could own their pipeline creation (currently in `app.cpp`)
- RT stage could own its resources (storage image, descriptor, acceleration structures)
- Declarative resource creation (logical → physical during `compile()`)
- Compute stages for IBL regeneration, post-processing

## Future: Subsurface Scattering (KHR_materials_subsurface)

`KHR_materials_subsurface` is a draft glTF extension (not yet ratified, [PR #1928](https://github.com/KhronosGroup/glTF/pull/1928)). Split out from `KHR_materials_volume` for independent scattering vs absorption control. Requires `KHR_materials_thickness`.

Based on Barré-Brisebois & Bouchard's screen-space approximation (GDC 2011) — a wrap-lighting trick, not full multi-scatter. Parameters: scale, distortion, power, color.

- [Spec draft](https://github.com/ux3d/glTF/tree/extensions/KHR_materials_subsurface/extensions/2.0/Khronos/KHR_materials_subsurface)
- [ScatteringSkull sample model](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/ScatteringSkull/README.md)

### Implementation Plan
1. Parse `KHR_materials_volume` (thickness, attenuation) and `KHR_materials_diffuse_transmission` from glTF — replaces standalone thickness maps
2. Add Barré-Brisebois back-lighting in the fragment shader using volume thickness + transmission factor
3. Screen-space irradiance blur pass (see TODO below)

### TODO: Screen-Space SSS Blur Pass
Add a post-lighting compute or fullscreen-quad pass that blurs diffuse lighting for subsurface scattering materials. Key constraints:
- Only blur pixels flagged as SSS materials (use a stencil bit or G-buffer flag to mask)
- Depth-modulated blur radius (nearby = wider blur, distant = tighter)
- Separable blur (horizontal + vertical) for performance — 2x7 samples instead of 7x7
- Kernel weights should approximate a diffusion profile (not a simple Gaussian)
- Would be a new render graph stage (`SSSBlurStage`) after the opaque lighting pass
- Reference: `~/github/Rendering/AdvancedVulkanDemos/em_assets/shaders/scene_subsurface_scattering/SubSurfaceScatteringIrradianceFrag.glsl`

## TODO: Wireframe Tube Impostors (VTK-style)
Render mesh edges as 3D tubes using geometry shader impostors, toggleable at runtime.

### Approach
1. **Edge extraction** (CPU, one-time): Build a unique edge list from the index buffer to avoid drawing shared edges twice. Store as a line-list vertex/index buffer.
2. **Geometry shader**: Input = line segments (edge endpoints). For each edge, emit a camera-facing quad (billboard) with width proportional to desired tube radius.
3. **Fragment shader**: Ray-cylinder intersection to shade each pixel as a 3D tube with proper lighting, specular highlights, and `gl_FragDepth` write for correct occlusion against the solid mesh.
4. **New render stage**: `WireframeStage` after `RasterOpaqueStage`, draws tube impostors. Toggled via ImGui checkbox.
5. **New pipeline**: Line-list input topology, geometry shader, dedicated fragment shader.

### Key details
- Screen-space or world-space tube radius (world-space is more VTK-like)
- Edge extraction runs once per model load, stored alongside the mesh
- Geometry shaders are fine for a debug/visualization overlay (not performance-critical)
- Could later upgrade to instanced quads + SSBO for better GPU performance

## TODO: Fetch glTF assets from Khronos at build time
Instead of storing model data (ScatteringSkull, DamagedHelmet, etc.) in the repo via LFS, use CMake `FetchContent` or a download script to pull them from the [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) repo. Same approach could work for HDR environments from [glTF-Sample-Environments](https://github.com/KhronosGroup/glTF-Sample-Environments). Keeps the repo leaner.

## TODO: Refactor `app.cpp` (~2050 lines)

### 1. ~~TOML Config → `AppConfig` struct~~ (DONE)
Extracted into `app_config.h/cpp`: `AppConfig parse_toml(path)` free function + `Application::apply_config()`.

### 2. Ray Tracing Resources → `RayTracingStage` or `RayTracingResources` (~250 lines)
Move `create_rt_storage_image()`, `create_rt_descriptor()`, `create_rt_pipeline()`, `build_acceleration_structures()` out of app. The stage already records commands but doesn't own its resources.

### 3. Command System → `CommandHandler` (~160 lines)
`poll_commands()` has both registry setup and file polling. Command lambdas capture `this` for 15+ operations. Could extract to a class that takes an `Application&` reference.

### 4. Screenshot System → `ScreenshotManager` (~80 lines)
`save_screenshot()`, `begin_screenshot_all()`, `tick_screenshot_all()` — self-contained state machine with its own member variables.

### 5. Pipeline Creation → Render Graph Stages (~120 lines)
`make_pipeline()`, `create_debug_2d_pipeline()`, `create_light_indicator()` — each stage could own its pipeline creation.

### 6. Vulkan Resource Creation → `FrameResources` (~150 lines)
`create_depth_resources()`, `create_msaa_color_resources()`, `create_uniform_buffer()` — swapchain-dependent resources, natural fit for a dedicated class or moving into `recreate_swapchain()`.
