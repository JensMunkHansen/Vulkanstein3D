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
- CPU-based processing (fast enough for 128x128 cubemaps)
- BRDF LUT: 128x128, 256 samples
- Irradiance map: 32x32 cubemap
- Prefiltered environment: 128x128 cubemap

## Future: Render Graph (Inexor-inspired)

Reference implementation: `~/github/Rendering/inexor/` (local clone)
- Header: `include/inexor/vulkan-renderer/render_graph.hpp`
- Source: `src/vulkan-renderer/render_graph.cpp`
- Example: `example-app/renderer.cpp`

Inexor uses a logical/physical split pattern with dependency-driven stage ordering.

### Motivation

`Application::record_draw_commands()` in `app.cpp` has grown complex — it now handles:
- Ray tracing path (image barriers, trace, blit to swapchain)
- Rasterization path with 2D debug mode
- Two-pass alpha draw (opaque+mask pass, then sorted blend pass)
- UI callback injection (ImGui)

Each of these is a logical stage that should be independently testable and composable.

### Plan

1. **Extract raster/RT paths into stages** — Move the current raster and ray tracing command recording out of `Application::record_draw_commands()` into separate stage objects. Each stage implements an `on_record(vk::CommandBuffer, uint32_t imageIndex)` callback. Inexor reference: `GraphicsStage` with `on_record` in `render_graph.hpp`.
2. **Create a `RenderGraph` class** — Owns render passes, pipelines, and framebuffers. Manages compilation (creating Vulkan objects from logical descriptions) and per-frame recording. The graph iterates stages in order, calling each stage's `on_record()`.
3. **Declarative resource creation** — Buffer and texture resources described logically (`BufferResource`, `TextureResource`), then realized into physical Vulkan objects during `compile()`.
4. **Keep it simpler than Inexor** — Skip the dependency DFS for now (fixed pipeline order). Focus on decoupling `Application` from raw Vulkan object management. Add compute stages and dependency ordering later if needed.

### Candidate Stages (current codebase)

| Stage | Pipeline | Render Pass | Notes |
|---|---|---|---|
| `RasterOpaqueStage` | `m_pipeline` | main | Draws OPAQUE + MASK primitives |
| `RasterBlendStage` | `m_blend_pipeline` | main (same subpass) | Sorted BLEND primitives, depth write off |
| `Debug2DStage` | `m_debug_2d_pipeline` | main | Fullscreen quad texture viewer |
| `RayTracingStage` | `m_rt_pipeline` | none (compute-like) | Trace + blit to swapchain |
| `UIStage` | (external) | main | ImGui callback |

### Inexor Limitations to Avoid
- No resource aliasing (every logical resource = 1 physical)
- Full memory barrier between stages (no fine-grained sync)
- Textures locked to swapchain dimensions
- Descriptor management leaks into user code
- Swapchain recreation destroys entire graph
