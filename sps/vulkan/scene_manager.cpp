#include <sps/vulkan/scene_manager.h>

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
  m_defaultNormalTexture =
    std::make_unique<Texture>(m_device, "default normal", flat_normal, 1, 1, true);

  // Create default 1x1 metallic/roughness texture (white = pass-through for scalar factors)
  // glTF spec: final value = texture * factor, so default texture must be 1.0
  const uint8_t default_mr[] = { 255, 255, 255, 255 };
  m_defaultMetallicRoughness =
    std::make_unique<Texture>(m_device, "default metallic/roughness", default_mr, 1, 1, true);

  // Create default 1x1 emissive texture (black = no emission)
  const uint8_t default_emissive[] = { 0, 0, 0, 255 };
  m_defaultEmissive =
    std::make_unique<Texture>(m_device, "default emissive", default_emissive, 1, 1);

  // Create default 1x1 AO texture (white = no occlusion)
  const uint8_t default_ao[] = { 255, 255, 255, 255 };
  m_defaultAO = std::make_unique<Texture>(m_device, "default ao", default_ao, 1, 1, true);

  // Create default 1x1 iridescence texture (white = factor passthrough)
  const uint8_t default_iridescence[] = { 255, 255, 255, 255 };
  m_defaultIridescence =
    std::make_unique<Texture>(m_device, "default iridescence", default_iridescence, 1, 1, true);

  // Create default 1x1 iridescence thickness texture (white = max thickness passthrough)
  const uint8_t default_iridescence_thickness[] = { 255, 255, 255, 255 };
  m_defaultIridescenceThickness = std::make_unique<Texture>(
    m_device, "default iridescence thickness", default_iridescence_thickness, 1, 1, true);

  // Create default 1x1 thickness texture (white = factor passthrough)
  const uint8_t default_thickness[] = { 255, 255, 255, 255 };
  m_defaultThickness =
    std::make_unique<Texture>(m_device, "default thickness", default_thickness, 1, 1, true);

  // Create IBL from HDR environment map
  try
  {
    if (!hdr_file.empty())
    {
      m_ibl = std::make_unique<IBL>(m_device, hdr_file, m_ibl_settings);
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

void SceneManager::set_ibl_settings(const IBLSettings& settings)
{
  m_ibl_settings = settings;
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

MaterialTextureSet SceneManager::default_texture_set() const
{
  // Choose textures: use loaded if available, otherwise defaults
  const Texture* colorTex = m_baseColorTexture ? m_baseColorTexture.get() : m_defaultTexture.get();
  const Texture* normalTex = m_normalTexture ? m_normalTexture.get() : m_defaultNormalTexture.get();
  const Texture* mrTex = m_metallicRoughnessTexture ? m_metallicRoughnessTexture.get()
                                                    : m_defaultMetallicRoughness.get();
  const Texture* emissiveTex = m_emissiveTexture ? m_emissiveTexture.get() : m_defaultEmissive.get();
  const Texture* aoTex = m_aoTexture ? m_aoTexture.get() : m_defaultAO.get();

  MaterialTextureSet set{};
  set.textures[0] = { colorTex->image_view(), colorTex->sampler() };
  set.textures[1] = { normalTex->image_view(), normalTex->sampler() };
  set.textures[2] = { mrTex->image_view(), mrTex->sampler() };
  set.textures[3] = { emissiveTex->image_view(), emissiveTex->sampler() };
  set.textures[4] = { aoTex->image_view(), aoTex->sampler() };

  // IBL textures (bindings 6, 7, 8)
  if (m_ibl)
  {
    set.textures[5] = { m_ibl->brdf_lut_view(), m_ibl->brdf_lut_sampler() };
    set.textures[6] = { m_ibl->irradiance_view(), m_ibl->irradiance_sampler() };
    set.textures[7] = { m_ibl->prefiltered_view(), m_ibl->prefiltered_sampler() };
  }

  // Iridescence textures (bindings 9, 10)
  set.textures[8] = { m_defaultIridescence->image_view(), m_defaultIridescence->sampler() };
  set.textures[9] = { m_defaultIridescenceThickness->image_view(),
    m_defaultIridescenceThickness->sampler() };

  // Thickness texture (binding 11)
  set.textures[10] = { m_defaultThickness->image_view(), m_defaultThickness->sampler() };

  return set;
}

std::vector<MaterialTextureSet> SceneManager::material_texture_sets() const
{
  if (!m_scene || m_scene->materials.empty())
    return {};

  std::vector<MaterialTextureSet> sets;
  sets.reserve(m_scene->materials.size());

  for (const auto& mat : m_scene->materials)
  {
    const Texture* matColor =
      mat.baseColorTexture ? mat.baseColorTexture.get() : m_defaultTexture.get();
    const Texture* matNormal =
      mat.normalTexture ? mat.normalTexture.get() : m_defaultNormalTexture.get();
    const Texture* matMR = mat.metallicRoughnessTexture ? mat.metallicRoughnessTexture.get()
                                                        : m_defaultMetallicRoughness.get();
    const Texture* matEmissive =
      mat.emissiveTexture ? mat.emissiveTexture.get() : m_defaultEmissive.get();
    const Texture* matAO = mat.aoTexture ? mat.aoTexture.get() : m_defaultAO.get();

    MaterialTextureSet set{};
    set.textures[0] = { matColor->image_view(), matColor->sampler() };
    set.textures[1] = { matNormal->image_view(), matNormal->sampler() };
    set.textures[2] = { matMR->image_view(), matMR->sampler() };
    set.textures[3] = { matEmissive->image_view(), matEmissive->sampler() };
    set.textures[4] = { matAO->image_view(), matAO->sampler() };

    // IBL textures (bindings 6, 7, 8) — same for all materials
    if (m_ibl)
    {
      set.textures[5] = { m_ibl->brdf_lut_view(), m_ibl->brdf_lut_sampler() };
      set.textures[6] = { m_ibl->irradiance_view(), m_ibl->irradiance_sampler() };
      set.textures[7] = { m_ibl->prefiltered_view(), m_ibl->prefiltered_sampler() };
    }

    // Iridescence textures (bindings 9, 10)
    const Texture* matIridescence =
      mat.iridescenceTexture ? mat.iridescenceTexture.get() : m_defaultIridescence.get();
    const Texture* matIridescenceThickness = mat.iridescenceThicknessTexture
      ? mat.iridescenceThicknessTexture.get()
      : m_defaultIridescenceThickness.get();
    set.textures[8] = { matIridescence->image_view(), matIridescence->sampler() };
    set.textures[9] = { matIridescenceThickness->image_view(),
      matIridescenceThickness->sampler() };

    // Thickness texture (binding 11)
    const Texture* matThickness =
      mat.thicknessTexture ? mat.thicknessTexture.get() : m_defaultThickness.get();
    set.textures[10] = { matThickness->image_view(), matThickness->sampler() };

    sets.push_back(set);
  }

  spdlog::trace("Built {} material texture sets", sets.size());
  return sets;
}

SceneManager::LoadResult SceneManager::load_model(const std::string& path)
{
  LoadResult result;

  spdlog::info("Loading model: {}", path);

  // Clear existing scene resources
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

int SceneManager::material_count() const
{
  return m_scene ? static_cast<int>(m_scene->materials.size()) : 0;
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

void SceneManager::load_hdr(const std::string& hdr_file)
{
  spdlog::info("Loading HDR environment: {}", hdr_file);

  float old_intensity = ibl_intensity();

  try
  {
    if (!hdr_file.empty())
    {
      m_ibl = std::make_unique<IBL>(m_device, hdr_file, m_ibl_settings);
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
}

} // namespace sps::vulkan
