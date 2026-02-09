#include <sps/vulkan/config.h>

#include <spdlog/async.h>
#include <spdlog/cfg/argv.h>
#include <spdlog/spdlog.h>

#include <sps/vulkan/app.h>
#include <sps/vulkan/debug_constants.h>
#include <sps/vulkan/light.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <filesystem>
#include <string>

using namespace sps::vulkan;
using namespace sps::vulkan::debug;

static void check_vk_result(VkResult err)
{
  if (err != VK_SUCCESS)
  {
    spdlog::error("ImGui Vulkan error: {}", static_cast<int>(err));
  }
}

int main(int argc, char* argv[])
{
  spdlog::cfg::load_argv_levels(argc, argv);
  spdlog::init_thread_pool(8192, 2);
  spdlog::set_level(spdlog::level::trace);
  spdlog::info("Loading with ImGui");

  Application app(argc, argv);

  // Force rasterization mode (no ray tracing for now)
  app.use_raytracing() = false;

  // Setup ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.FontGlobalScale = 2.0f;  // Scale up fonts for 4K displays

  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForVulkan(app.glfw_window(), true);

  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = app.vk_instance();
  init_info.PhysicalDevice = app.vk_physical_device();
  init_info.Device = app.vk_device();
  init_info.QueueFamily = app.graphics_queue_family();
  init_info.Queue = app.vk_graphics_queue();
  init_info.PipelineCache = VK_NULL_HANDLE;
  init_info.DescriptorPool = VK_NULL_HANDLE; // We'll create one
  init_info.RenderPass = app.vk_renderpass();
  init_info.Subpass = 0;
  init_info.MinImageCount = 2;
  init_info.ImageCount = app.swapchain_image_count();
  init_info.MSAASamples = app.msaa_samples();
  init_info.Allocator = nullptr;
  init_info.CheckVkResultFn = check_vk_result;

  // Create descriptor pool for ImGui
  VkDescriptorPoolSize pool_sizes[] = {
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
  };
  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = pool_sizes;

  VkDescriptorPool imgui_pool;
  vkCreateDescriptorPool(app.vk_device(), &pool_info, nullptr, &imgui_pool);
  init_info.DescriptorPool = imgui_pool;

  ImGui_ImplVulkan_Init(&init_info);

  // Set up the render callback to draw ImGui
  app.set_ui_render_callback([](vk::CommandBuffer cmd) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
  });

  spdlog::info("ImGui initialized");

  // Main loop
  while (!app.should_close())
  {
    app.poll_events();
    app.poll_commands();  // Check for remote commands (commands.txt)
    app.update_frame();

    // Start ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Track current shader mode (shared across sections)
    static int current_shader = SHADER_PBR;

    // UI Panel — position top-left and start collapsed
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
    {
      bool vsync = app.vsync_enabled();
      if (ImGui::Checkbox("VSync (FIFO)", &vsync))
      {
        app.set_vsync(vsync);
      }
      ImGui::SameLine();
      ImGui::TextDisabled("(off = Immediate)");
    }

    if (!app.gltf_models().empty() && ImGui::CollapsingHeader("Models", ImGuiTreeNodeFlags_DefaultOpen))
    {
      const auto& models = app.gltf_models();
      int selected = app.current_model_index();

      // Build display name from current selection
      std::string preview = (selected >= 0 && selected < static_cast<int>(models.size()))
        ? std::filesystem::path(models[selected]).stem().string()
        : "(none)";

      if (ImGui::BeginCombo("Model", preview.c_str()))
      {
        for (int i = 0; i < static_cast<int>(models.size()); i++)
        {
          std::string label = std::filesystem::path(models[i]).stem().string();
          bool is_selected = (selected == i);
          if (ImGui::Selectable(label.c_str(), is_selected))
          {
            app.load_model(i);
          }
          if (is_selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }

    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
      if (current_shader == SHADER_PBR)
      {
        // PBR-specific controls
        ImGui::SliderFloat("Exposure", &app.exposure(), 0.1f, 5.0f);
        ImGui::Combo("Tone Mapping", &app.tonemap_mode(),
          Application::tonemap_names, IM_ARRAYSIZE(Application::tonemap_names));
        ImGui::SliderFloat("AO Strength", &app.ao_strength(), 0.0f, 1.0f);

        ImGui::Separator();
        ImGui::Checkbox("Normal Mapping", &app.use_normal_mapping());
        ImGui::Checkbox("Emissive", &app.use_emissive());
        ImGui::Checkbox("Ambient Occlusion", &app.use_ao());

        ImGui::Separator();
        ImGui::Checkbox("IBL Environment", &app.use_ibl());
        ImGui::SetItemTooltip("Use environment map for ambient lighting");

        if (app.use_ibl()) {
          // IBL intensity control (only when IBL is enabled)
          float ibl_intensity = app.ibl_intensity();
          if (ImGui::SliderFloat("IBL Intensity", &ibl_intensity, 0.0f, 3.0f)) {
            app.set_ibl_intensity(ibl_intensity);
          }

          // HDR environment selector
          if (!app.hdr_files().empty())
          {
            const auto& hdrs = app.hdr_files();
            int hdr_selected = app.current_hdr_index();
            std::string hdr_preview = (hdr_selected >= 0 && hdr_selected < static_cast<int>(hdrs.size()))
              ? std::filesystem::path(hdrs[hdr_selected]).stem().string()
              : "(none)";

            if (ImGui::BeginCombo("Environment", hdr_preview.c_str()))
            {
              for (int i = 0; i < static_cast<int>(hdrs.size()); i++)
              {
                std::string label = std::filesystem::path(hdrs[i]).stem().string();
                bool is_selected = (hdr_selected == i);
                if (ImGui::Selectable(label.c_str(), is_selected))
                {
                  app.load_hdr(i);
                }
                if (is_selected)
                  ImGui::SetItemDefaultFocus();
              }
              ImGui::EndCombo();
            }
          }
        } else {
          // Fake ambient controls (only when IBL is disabled)
          ImGui::SliderFloat("Metal Ambient", &app.metallic_ambient(), 0.0f, 1.0f);
          ImGui::SetItemTooltip("Fake IBL for metallic surfaces");
        }
      }
      else if (current_shader == SHADER_BLINN_PHONG)
      {
        // Blinn-Phong-specific controls
        ImGui::SliderFloat("Exposure", &app.exposure(), 0.1f, 5.0f);
        ImGui::SliderFloat("Shininess", &app.shininess(), 1.0f, 128.0f);
        ImGui::SliderFloat("Specular", &app.specular_strength(), 0.0f, 1.0f);

        ImGui::Separator();
        ImGui::Checkbox("Normal Mapping", &app.use_normal_mapping());
      }
      else
      {
        // Debug shaders - minimal controls
        ImGui::TextDisabled("Debug shader - no material controls");
      }
    }

    if (ImGui::CollapsingHeader("Light"))
    {
      Light& light = app.light();

      // Try to get position from PointLight
      if (auto* point_light = dynamic_cast<PointLight*>(&light))
      {
        glm::vec3 pos = point_light->position();
        if (ImGui::DragFloat3("Position", &pos.x, 0.1f, -10.0f, 10.0f))
        {
          point_light->set_position(pos);
        }
      }

      glm::vec3 color = light.color();
      if (ImGui::ColorEdit3("Color", &color.x))
      {
        light.set_color(color);
      }

      float intensity = light.intensity();
      if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 5.0f))
      {
        light.set_intensity(intensity);
      }

      glm::vec3 ambient = light.ambient();
      if (ImGui::ColorEdit3("Ambient", &ambient.x))
      {
        light.set_ambient(ambient);
      }
    }

    if (ImGui::CollapsingHeader("Camera"))
    {
      Camera& cam = app.camera();
      glm::vec3 pos = cam.position();
      if (ImGui::DragFloat3("Position", &pos.x, 0.1f))
      {
        cam.set_position(pos.x, pos.y, pos.z);
      }

      float fov = cam.view_angle();
      if (ImGui::SliderFloat("FOV", &fov, 10.0f, 120.0f))
      {
        cam.set_view_angle(fov);
      }
    }

    if (ImGui::CollapsingHeader("Shaders"))
    {
      // Only show 3D shaders (not 2D texture view)
      constexpr int shader_3d_count = SHADER_2D_TEXTURE;
      if (ImGui::Combo("Shader", &current_shader, shader_names, shader_3d_count))
      {
        app.reload_shaders(vertex_shaders[current_shader], fragment_shaders[current_shader]);
      }

      if (ImGui::Button("Reload Shaders"))
      {
        app.reload_shaders(vertex_shaders[current_shader], fragment_shaders[current_shader]);
      }
      ImGui::SameLine();
      ImGui::TextDisabled("(after editing .frag/.vert)");

      // Screenshot button
      ImGui::Separator();
      if (ImGui::Button("Save Screenshot"))
      {
        app.save_screenshot();
      }
      ImGui::SameLine();
      ImGui::TextDisabled("(saves to current directory)");
    }

    if (ImGui::CollapsingHeader("2D Debug"))
    {
      ImGui::Checkbox("2D Texture View", &app.debug_2d_mode());
      ImGui::SetItemTooltip("Display texture flat on screen (skips 3D rendering)");

      if (app.debug_2d_mode())
      {
        if (app.material_count() > 1)
        {
          int count = app.material_count();
          std::string preview = "Material " + std::to_string(app.debug_material_index());
          if (ImGui::BeginCombo("Material##debug2d", preview.c_str()))
          {
            for (int i = 0; i < count; i++)
            {
              std::string label = "Material " + std::to_string(i);
              bool selected = (app.debug_material_index() == i);
              if (ImGui::Selectable(label.c_str(), selected))
                app.debug_material_index() = i;
              if (selected)
                ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
          }
        }
        ImGui::Combo("Texture", &app.debug_texture_index(), texture_names, TEX_COUNT);
        ImGui::Combo("Channel", &app.debug_channel_mode(), channel_names, CHANNEL_COUNT);

        ImGui::Separator();
        ImGui::Text("Zoom: %.1fx", app.debug_2d_zoom());
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset"))
        {
          app.reset_debug_2d_view();
        }
        ImGui::TextDisabled("Scroll to zoom, drag to pan");
      }
    }

    ImGui::End();

    ImGui::Render();

    // Sync uniforms after ImGui has processed input changes
    app.sync_uniforms();

    // Render scene
    app.render();
    app.tick_screenshot_all();
    app.calculateFrameRate();
  }

  // Cleanup
  app.wait_idle();
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  vkDestroyDescriptorPool(app.vk_device(), imgui_pool, nullptr);

  spdlog::info("Window closed");
  return 0;
}
