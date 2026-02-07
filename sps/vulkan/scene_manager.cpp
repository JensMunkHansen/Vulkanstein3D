#include <sps/vulkan/scene_manager.h>

#include <sps/vulkan/app.h>  // for UniformBufferObject (sizeof needed by DescriptorBuilder)
#include <sps/vulkan/descriptor_builder.h>
#include <sps/vulkan/device.h>
#include <sps/vulkan/ibl.h>
#include <sps/vulkan/mesh.h>
#include <sps/vulkan/ply_loader.h>
#include <sps/vulkan/texture.h>

#include <spdlog/spdlog.h>

namespace sps::vulkan
{

SceneManager::SceneManager(const Device& device)
  : m_device(device)
{
}

SceneManager::~SceneManager() = default;

void SceneManager::create_defaults(const std::string& hdr_file)
{
  // Create default 1x1 white texture for fallback
  const uint8_t white_pixel[] = { 255, 255, 255, 255 };
  m_defaultTexture = std::make_unique<Texture>(m_device, "default white", white_pixel, 1, 1);

  // Create default 1x1 flat normal texture (pointing up in tangent space: 0,0,1 -> RGB 128,128,255)
  const uint8_t flat_normal[] = { 128, 128, 255, 255 };
  m_defaultNormalTexture = std::make_unique<Texture>(m_device, "default normal", flat_normal, 1, 1);

  // Create default 1x1 metallic/roughness texture (non-metallic, medium roughness)
  // glTF format: G=roughness, B=metallic
  const uint8_t default_mr[] = { 255, 128, 0, 255 };
  m_defaultMetallicRoughness =
    std::make_unique<Texture>(m_device, "default metallic/roughness", default_mr, 1, 1);

  // Create default 1x1 emissive texture (black = no emission)
  const uint8_t default_emissive[] = { 0, 0, 0, 255 };
  m_defaultEmissive =
    std::make_unique<Texture>(m_device, "default emissive", default_emissive, 1, 1);

  // Create default 1x1 AO texture (white = no occlusion)
  const uint8_t default_ao[] = { 255, 255, 255, 255 };
  m_defaultAO = std::make_unique<Texture>(m_device, "default ao", default_ao, 1, 1);

  // Create IBL from HDR environment map
  try
  {
    if (!hdr_file.empty())
    {
      m_ibl = std::make_unique<IBL>(m_device, hdr_file, 128);
    }
    else
    {
      m_ibl = std::make_unique<IBL>(m_device);
    }
  }
  catch (const std::exception& e)
  {
    spdlog::warn("Failed to load HDR '{}': {} - using neutral environment", hdr_file, e.what());
    m_ibl = std::make_unique<IBL>(m_device);
  }
}

SceneManager::LoadResult SceneManager::load_initial_scene(
  const std::string& geometry_source,
  const std::string& gltf_file,
  const std::string& ply_file)
{
  LoadResult result;

  if (geometry_source == "gltf" && !gltf_file.empty())
  {
    GltfScene scene = load_gltf_scene(m_device, gltf_file);

    if (scene.mesh)
    {
      // Extract mesh BEFORE moving scene (scene.mesh becomes null after move)
      m_mesh = std::move(scene.mesh);
      m_bounds = scene.bounds;
      m_scene = std::move(scene);

      spdlog::info(
        "Loaded glTF scene from {}: {} vertices, {} indices, {} primitives, {} materials",
        gltf_file, m_mesh->vertex_count(), m_mesh->index_count(), m_scene->primitives.size(),
        m_scene->materials.size());

      result.success = true;
      result.bounds = m_bounds;
      return result;
    }

    spdlog::warn("Could not load glTF from {}, falling back to triangle", gltf_file);
  }
  else if (geometry_source == "ply" && !ply_file.empty())
  {
    m_mesh = load_ply(m_device, ply_file);

    if (m_mesh)
    {
      spdlog::info("Loaded PLY mesh from {}: {} vertices, {} indices", ply_file,
        m_mesh->vertex_count(), m_mesh->index_count());

      result.success = true;
      // PLY has no AABB computed — bounds stays invalid
      return result;
    }

    spdlog::warn("Could not load PLY from {}, falling back to triangle", ply_file);
  }

  // Default triangle
  std::vector<Vertex> vertices = {
    { { 0.0f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
    { { 0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
    { { -0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } }
  };

  m_mesh = std::make_unique<Mesh>(m_device, "default triangle", vertices);
  spdlog::trace("Created default triangle mesh");

  result.success = true;
  return result;
}

void SceneManager::create_descriptors(vk::Buffer uniform_buffer)
{
  // Choose textures: use loaded if available, otherwise defaults
  Texture* colorTex = m_baseColorTexture ? m_baseColorTexture.get() : m_defaultTexture.get();
  Texture* normalTex = m_normalTexture ? m_normalTexture.get() : m_defaultNormalTexture.get();
  Texture* mrTex = m_metallicRoughnessTexture ? m_metallicRoughnessTexture.get()
                                              : m_defaultMetallicRoughness.get();
  Texture* emissiveTex = m_emissiveTexture ? m_emissiveTexture.get() : m_defaultEmissive.get();
  Texture* aoTex = m_aoTexture ? m_aoTexture.get() : m_defaultAO.get();

  DescriptorBuilder builder(m_device);
  builder.add_uniform_buffer<UniformBufferObject>(uniform_buffer, 0,
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
  builder.add_combined_image_sampler(
    colorTex->image_view(), colorTex->sampler(), 1, vk::ShaderStageFlagBits::eFragment);
  builder.add_combined_image_sampler(
    normalTex->image_view(), normalTex->sampler(), 2, vk::ShaderStageFlagBits::eFragment);
  builder.add_combined_image_sampler(
    mrTex->image_view(), mrTex->sampler(), 3, vk::ShaderStageFlagBits::eFragment);
  builder.add_combined_image_sampler(
    emissiveTex->image_view(), emissiveTex->sampler(), 4, vk::ShaderStageFlagBits::eFragment);
  builder.add_combined_image_sampler(
    aoTex->image_view(), aoTex->sampler(), 5, vk::ShaderStageFlagBits::eFragment);

  // IBL textures (bindings 6, 7, 8)
  if (m_ibl)
  {
    builder.add_combined_image_sampler(
      m_ibl->brdf_lut_view(), m_ibl->brdf_lut_sampler(), 6, vk::ShaderStageFlagBits::eFragment);
    builder.add_combined_image_sampler(
      m_ibl->irradiance_view(), m_ibl->irradiance_sampler(), 7, vk::ShaderStageFlagBits::eFragment);
    builder.add_combined_image_sampler(m_ibl->prefiltered_view(), m_ibl->prefiltered_sampler(), 8,
      vk::ShaderStageFlagBits::eFragment);
  }

  m_descriptor = std::make_unique<ResourceDescriptor>(builder.build("camera descriptor"));
  spdlog::trace("Created descriptor with PBR texture bindings + IBL");

  // Create per-material descriptors for scene graph rendering
  if (m_scene && !m_scene->materials.empty())
  {
    m_material_descriptors.clear();

    for (size_t i = 0; i < m_scene->materials.size(); ++i)
    {
      const auto& mat = m_scene->materials[i];

      Texture* matColor =
        mat.baseColorTexture ? mat.baseColorTexture.get() : m_defaultTexture.get();
      Texture* matNormal =
        mat.normalTexture ? mat.normalTexture.get() : m_defaultNormalTexture.get();
      Texture* matMR = mat.metallicRoughnessTexture ? mat.metallicRoughnessTexture.get()
                                                    : m_defaultMetallicRoughness.get();
      Texture* matEmissive =
        mat.emissiveTexture ? mat.emissiveTexture.get() : m_defaultEmissive.get();
      Texture* matAO = mat.aoTexture ? mat.aoTexture.get() : m_defaultAO.get();

      DescriptorBuilder mat_builder(m_device);
      mat_builder.add_uniform_buffer<UniformBufferObject>(uniform_buffer, 0,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
      mat_builder.add_combined_image_sampler(
        matColor->image_view(), matColor->sampler(), 1, vk::ShaderStageFlagBits::eFragment);
      mat_builder.add_combined_image_sampler(
        matNormal->image_view(), matNormal->sampler(), 2, vk::ShaderStageFlagBits::eFragment);
      mat_builder.add_combined_image_sampler(
        matMR->image_view(), matMR->sampler(), 3, vk::ShaderStageFlagBits::eFragment);
      mat_builder.add_combined_image_sampler(
        matEmissive->image_view(), matEmissive->sampler(), 4, vk::ShaderStageFlagBits::eFragment);
      mat_builder.add_combined_image_sampler(
        matAO->image_view(), matAO->sampler(), 5, vk::ShaderStageFlagBits::eFragment);

      if (m_ibl)
      {
        mat_builder.add_combined_image_sampler(
          m_ibl->brdf_lut_view(), m_ibl->brdf_lut_sampler(), 6, vk::ShaderStageFlagBits::eFragment);
        mat_builder.add_combined_image_sampler(m_ibl->irradiance_view(),
          m_ibl->irradiance_sampler(), 7, vk::ShaderStageFlagBits::eFragment);
        mat_builder.add_combined_image_sampler(m_ibl->prefiltered_view(),
          m_ibl->prefiltered_sampler(), 8, vk::ShaderStageFlagBits::eFragment);
      }

      m_material_descriptors.push_back(
        std::make_unique<ResourceDescriptor>(mat_builder.build("material_" + std::to_string(i))));
    }

    spdlog::info("Created {} per-material descriptors", m_material_descriptors.size());
  }
}

SceneManager::LoadResult SceneManager::load_model(
  const std::string& path, vk::Buffer uniform_buffer)
{
  LoadResult result;

  spdlog::info("Loading model: {}", path);

  // Clear existing scene resources
  m_material_descriptors.clear();
  m_scene.reset();
  m_mesh.reset();
  m_bounds = AABB{};

  // Load new scene
  GltfScene scene = load_gltf_scene(m_device, path);
  if (!scene.mesh)
  {
    spdlog::error("Failed to load model: {}", path);
    return result;
  }

  // Extract mesh BEFORE moving scene (scene.mesh becomes null after move)
  m_mesh = std::move(scene.mesh);
  m_bounds = scene.bounds;
  m_scene = std::move(scene);

  spdlog::info("Loaded glTF scene: {} vertices, {} indices, {} primitives, {} materials",
    m_mesh->vertex_count(), m_mesh->index_count(), m_scene->primitives.size(),
    m_scene->materials.size());

  // Rebuild descriptors for new materials
  create_descriptors(uniform_buffer);

  result.success = true;
  result.bounds = m_bounds;
  return result;
}

const Mesh* SceneManager::mesh() const
{
  return m_mesh.get();
}

Mesh* SceneManager::mesh()
{
  return m_mesh.get();
}

const GltfScene* SceneManager::scene() const
{
  return m_scene ? &*m_scene : nullptr;
}

const ResourceDescriptor* SceneManager::default_descriptor() const
{
  return m_descriptor.get();
}

const std::vector<std::unique_ptr<ResourceDescriptor>>& SceneManager::material_descriptors() const
{
  return m_material_descriptors;
}

int SceneManager::material_count() const
{
  return static_cast<int>(m_material_descriptors.size());
}

const AABB& SceneManager::bounds() const
{
  return m_bounds;
}

const IBL* SceneManager::ibl() const
{
  return m_ibl.get();
}

float SceneManager::ibl_intensity() const
{
  return m_ibl ? m_ibl->intensity() : 1.0f;
}

void SceneManager::set_ibl_intensity(float v)
{
  if (m_ibl)
    m_ibl->set_intensity(v);
}

void SceneManager::load_hdr(const std::string& hdr_file, vk::Buffer uniform_buffer)
{
  spdlog::info("Loading HDR environment: {}", hdr_file);

  float old_intensity = ibl_intensity();

  try
  {
    if (!hdr_file.empty())
    {
      m_ibl = std::make_unique<IBL>(m_device, hdr_file, 128);
    }
    else
    {
      m_ibl = std::make_unique<IBL>(m_device);
    }
  }
  catch (const std::exception& e)
  {
    spdlog::warn("Failed to load HDR '{}': {} - using neutral environment", hdr_file, e.what());
    m_ibl = std::make_unique<IBL>(m_device);
  }

  m_ibl->set_intensity(old_intensity);

  // Rebuild descriptors with new IBL textures
  create_descriptors(uniform_buffer);
}

} // namespace sps::vulkan
