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

4-phase execution with 7 stages in `sps/vulkan/stages/`:

| Phase | Stage | What it does |
|-------|-------|-------------|
| PrePass | `RayTracingStage` | Trace + blit (outside render pass) |
| ScenePass | `RasterOpaqueStage` | OPAQUE + MASK primitives → HDR target |
| ScenePass | `RasterBlendStage` | Sorted BLEND primitives, depth write off |
| Intermediate | `SSSBlurStage` | Separable 13-tap compute blur (H+V) |
| CompositePass | `CompositeStage` | Fullscreen triangle: HDR → exposure → tonemap → gamma → swapchain |
| CompositePass | `Debug2DStage` | Fullscreen quad texture viewer |
| CompositePass | `UIStage` | ImGui callback |

Scene renders to `m_hdrImage` (R16G16B16A16Sfloat). Composite samples it and writes to swapchain.
Fragment shader outputs raw linear HDR; `composite.frag` does exposure + tone mapping + gamma.

Stages registered in `finalize_setup()`, no-ops via `is_enabled()` + early returns.
`FrameContext` passes per-frame data. `CompositeStage` is self-contained (owns pipeline, descriptors, framebuffers). Other stages still receive non-owning handles from app.

### Resource ownership

| Resource | Owner | Consumers |
|----------|-------|-----------|
| Shared images (HDR, depth-stencil, MSAA) | `VulkanRenderer` | Stages via renderer getters |
| Render passes | App creates, graph brokers | Graph uses for begin/end; stages use for pipeline/framebuffer creation |
| Pipeline, descriptors, framebuffers | Self-contained stages (CompositeStage) or app (others, migrating) | The stage itself |

### Future Improvements

#### Near-term: Shared image registry in graph
Graph brokers shared images (HDR, DS, ping) so stages query the graph instead of receiving raw `vk::Image` handles at construction. Avoids stale handles during swapchain recreation.

#### Near-term: More self-contained stages
Migrate remaining stages to own their pipelines/descriptors following CompositeStage pattern:
- `Debug2DStage` — similar to CompositeStage, straightforward
- `SSSBlurStage` — compute pipeline + ping image + descriptors
- `RasterOpaqueStage` / `RasterBlendStage` — share a pipeline layout, need coordinated approach
- `RayTracingStage` — owns storage image, descriptor, acceleration structures, pipeline

#### Medium-term: Dynamic rendering (VK_KHR_dynamic_rendering)
- Eliminates `VkRenderPass` and `VkFramebuffer` — attachments specified inline with `vkCmdBeginRendering`
- Core in Vulkan 1.3; removes need for render pass registry
- Graph specifies attachment formats per phase, stages don't need render pass handles
- Simplifies swapchain recreation (no framebuffer rebuild)

#### Long-term: Full render graph
- Declarative resource creation (logical → physical during `compile()`)
- Automatic barrier insertion from resource usage declarations
- Automatic layout transitions (no manual `pipelineBarrier` in stages)
- Resource aliasing (reuse memory for non-overlapping lifetimes)
- Compute stages for IBL regeneration, post-processing

## Subsurface Scattering

`KHR_materials_subsurface` is a draft glTF extension (not yet ratified, [PR #1928](https://github.com/KhronosGroup/glTF/pull/1928)). Split out from `KHR_materials_volume` for independent scattering vs absorption control.

- [Spec draft](https://github.com/ux3d/glTF/tree/extensions/KHR_materials_subsurface/extensions/2.0/Khronos/KHR_materials_subsurface)
- [ScatteringSkull sample model](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/ScatteringSkull/README.md)

### Implemented
1. **glTF parsing**: `KHR_materials_volume` (thickness, attenuation) and `KHR_materials_transmission`. Fallback: infer transmissionFactor=1.0 when has_volume + thickness>0 (cgltf lacks `KHR_materials_diffuse_transmission`).
2. **Barré-Brisebois back-lighting** (`fragment.frag`): `dot(V, -(L + N*distortion))^power` — visible when light is behind surface. Uses exponential thickness falloff `exp(-thickness * 1.5)`.
3. **Screen-space blur with per-channel diffusion** (`sss_blur.comp`, `SSSBlurStage`):
   - Separable 13-tap compute blur (H+V passes) in Intermediate phase
   - Per-channel blur widths: R=2.5, G=1.0, B=0.5 (ratio 5:2:1, matching skin scattering distances)
   - Red ~3.67mm, Green ~1.37mm, Blue ~0.68mm (Jimenez "Separable SSS", d'Eon/Luebke GPU Gems 3)
   - SSS masking via HDR alpha channel: fragment shader writes alpha=1.0 for SSS materials (`transmissionFactor > 0`), 0.0 for non-SSS; HDR clear alpha=0.0
   - Blur shader early-outs on alpha=0 (background + non-SSS objects passthrough)
   - Blur is uniform across SSS surface (lateral scattering, independent of thickness)
4. **DepthStencilAttachment** (`depth_stencil_attachment.h/cpp`): RAII class wrapping DS image with 3 aspect views (combined, depth-only, stencil-only). Format `eD32SfloatS8Uint`.
5. **Stencil pipeline** (partially working): Opaque pipeline writes stencil=1 for SSS materials via dynamic `setStencilReference()`. Blend pipeline has stencil disabled (SSS stencil survives behind glass). However, stencil-only ImageView sampling in compute shader returns 0 — using alpha masking instead.

### Known Issues
- **Stencil read not working**: `texelFetch(usampler2D, ...)` on stencil-only ImageView returns 0 for all pixels. The stencil write pipeline is in place (render pass ops, pipeline state, dynamic reference) but reading it in the compute shader fails. Workaround: alpha channel masking. Investigate: may need `VK_KHR_maintenance2` for separate stencil usage, or copy stencil to R8 image.
- **transmissionFactor is per-material scalar**: ScatteringSkull has T=1.0 on all materials, so the entire skull gets blurred uniformly. A per-pixel transmission texture would allow spatially-varying SSS within a single material.

### Future SSS Improvements
- **Per-pixel transmission texture**: Replace scalar `transmissionFactor` with a texture for spatially-varying SSS masking within a material
- **Depth-modulated blur radius**: Scale blur width by depth — nearby surfaces get wider blur, distant surfaces tighter. Prevents distant objects looking overly blurry.
- **Sum-of-Gaussians kernel**: Current kernel is a simple falloff. A proper diffusion profile uses sum-of-Gaussians fit (6 Gaussians per channel in Jimenez). Would improve physical accuracy.

### Relevant code
- Blur stage: `sps/vulkan/stages/sss_blur_stage.h/cpp`
- Blur shader: `sps/vulkan/shaders/sss_blur.comp`
- Fragment shader SSS: `sps/vulkan/shaders/fragment.frag` (Barré-Brisebois + alpha mask output)
- Depth-stencil: `sps/vulkan/depth_stencil_attachment.h/cpp`
- Debug: `sps/vulkan/shaders/debug_stencil.frag` (visualizes transmissionFactor per fragment)
- HDR image: `m_hdrImage` (R16G16B16A16Sfloat, eColorAttachment | eSampled | eStorage)
- Ping image: `m_blurPingImage` (same format, eStorage | eSampled)
- Push constants: 3x float blurWidth (R/G/B) + int direction = 16 bytes

## TODO: Incremental Scene Composition

Support adding new elements (implants, prosthetics, additional anatomy) to an existing scene at runtime. Objects are static once placed — no per-frame transform animation needed.

### Design Considerations

**Mesh management**: Each added element is a separate glTF/PLY loaded alongside the existing scene. Separate VBO/IBO per object (simpler lifetime, no mega-buffer compaction). Draw calls append to the existing primitive list.

**Materials and descriptors**: New objects bring their own materials and textures. Each gets its own descriptor set(s) following the existing per-material descriptor pattern. SSS properties (transmissionFactor, thickness) carry over naturally — new objects can be SSS or not.

**Scene graph**: Currently `GltfScene` holds a flat list of nodes with transforms. Multi-object scenes need either:
- A list of `GltfScene` objects (each self-contained, own transform hierarchy)
- Or merge into a single scene graph with a per-object root transform

The first approach is simpler and avoids index/buffer offset bookkeeping.

**Acceleration structures (RT)**: Each object gets its own BLAS (built once). TLAS rebuilt when objects are added. Since no motion, TLAS is static after each addition — no per-frame rebuild needed.

**Bounding box / camera**: Scene AABB is the union of all loaded objects. Camera reset uses the combined bounds.

**What we don't need**: Per-frame transform updates, skinning/animation, dynamic buffer suballocation, object removal (add-only is sufficient for now).

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

### 5. ~~Pipeline Creation → Render Graph Stages~~ (PARTIAL)
~~`create_composite_pipeline()`~~ — done: `CompositeStage` is self-contained (owns pipeline, descriptors, framebuffers).
Remaining: `make_pipeline()`, `create_debug_2d_pipeline()`, `create_light_indicator()`.

### 6. ~~Vulkan Resource Creation → `VulkanRenderer`~~ (DONE)
~~`create_depth_resources()`, `create_msaa_color_resources()`, `create_hdr_resources()`~~ — moved to `VulkanRenderer`.
Remaining: `create_uniform_buffer()` (app-specific, stays in app).
