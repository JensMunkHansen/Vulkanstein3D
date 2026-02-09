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

## TODO: Refactor `app.cpp` (~2050 lines)

### 1. TOML Config → `AppConfig` struct (~230 lines)
Extract `load_toml_configuration_file()` into a `AppConfig parse_toml(path)` free function returning a plain struct. Decouples config format from initialization logic. Easiest win.

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
