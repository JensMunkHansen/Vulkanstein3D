#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <stb_image.h>

#include <sps/vulkan/gltf_loader.h>
#include <sps/vulkan/texture.h>

#include <spdlog/spdlog.h>

#include <filesystem>

namespace sps::vulkan
{

namespace
{

// Helper to read accessor data as floats
template <typename T>
void read_accessor_data(const cgltf_accessor* accessor, std::vector<T>& out, size_t components)
{
  out.resize(accessor->count * components);
  for (size_t i = 0; i < accessor->count; ++i)
  {
    cgltf_accessor_read_float(accessor, i, reinterpret_cast<float*>(&out[i * components]), components);
  }
}

// Helper to read accessor data as uint32_t indices
void read_index_data(const cgltf_accessor* accessor, std::vector<uint32_t>& out)
{
  out.resize(accessor->count);
  for (size_t i = 0; i < accessor->count; ++i)
  {
    out[i] = static_cast<uint32_t>(cgltf_accessor_read_index(accessor, i));
  }
}

} // anonymous namespace

std::unique_ptr<Mesh> load_gltf(const Device& device, const std::string& filepath)
{
  // Check file exists
  if (!std::filesystem::exists(filepath))
  {
    spdlog::error("glTF file not found: {}", filepath);
    return nullptr;
  }

  // Parse glTF file
  cgltf_options options = {};
  cgltf_data* data = nullptr;

  cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);
  if (result != cgltf_result_success)
  {
    spdlog::error("Failed to parse glTF file: {} (error {})", filepath, static_cast<int>(result));
    return nullptr;
  }

  // Load buffers (needed for binary data access)
  result = cgltf_load_buffers(&options, data, filepath.c_str());
  if (result != cgltf_result_success)
  {
    spdlog::error("Failed to load glTF buffers: {} (error {})", filepath, static_cast<int>(result));
    cgltf_free(data);
    return nullptr;
  }

  // Extract filename for mesh name
  std::string mesh_name = std::filesystem::path(filepath).stem().string();

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  // Process all meshes (combine into one for now)
  for (size_t mesh_idx = 0; mesh_idx < data->meshes_count; ++mesh_idx)
  {
    const cgltf_mesh& mesh = data->meshes[mesh_idx];

    for (size_t prim_idx = 0; prim_idx < mesh.primitives_count; ++prim_idx)
    {
      const cgltf_primitive& primitive = mesh.primitives[prim_idx];

      // We only handle triangles
      if (primitive.type != cgltf_primitive_type_triangles)
      {
        spdlog::warn("Skipping non-triangle primitive in {}", filepath);
        continue;
      }

      // Find accessors for each attribute
      // glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes
      const cgltf_accessor* position_accessor = nullptr;
      const cgltf_accessor* normal_accessor = nullptr;
      const cgltf_accessor* texcoord_accessor = nullptr;
      const cgltf_accessor* color_accessor = nullptr;
      const cgltf_accessor* tangent_accessor = nullptr;

      for (size_t attr_idx = 0; attr_idx < primitive.attributes_count; ++attr_idx)
      {
        const cgltf_attribute& attr = primitive.attributes[attr_idx];

        switch (attr.type)
        {
          case cgltf_attribute_type_position:
            position_accessor = attr.data;
            break;
          case cgltf_attribute_type_normal:
            normal_accessor = attr.data;
            break;
          case cgltf_attribute_type_texcoord:
            if (attr.index == 0) // TEXCOORD_0
              texcoord_accessor = attr.data;
            break;
          case cgltf_attribute_type_color:
            if (attr.index == 0) // COLOR_0
              color_accessor = attr.data;
            break;
          case cgltf_attribute_type_tangent:
            tangent_accessor = attr.data;
            break;
          default:
            break;
        }
      }

      if (!position_accessor)
      {
        spdlog::warn("Primitive missing positions in {}", filepath);
        continue;
      }

      // Read vertex data
      std::vector<float> positions;
      std::vector<float> normals;
      std::vector<float> texcoords;
      std::vector<float> colors;
      std::vector<float> tangents;

      read_accessor_data(position_accessor, positions, 3);

      if (normal_accessor)
      {
        read_accessor_data(normal_accessor, normals, 3);
      }

      if (texcoord_accessor)
      {
        read_accessor_data(texcoord_accessor, texcoords, 2);
      }

      if (color_accessor)
      {
        // Color can be vec3 or vec4
        size_t color_components = (color_accessor->type == cgltf_type_vec4) ? 4 : 3;
        read_accessor_data(color_accessor, colors, color_components);
      }

      if (tangent_accessor)
      {
        // glTF TANGENT is always vec4 (xyz=tangent, w=handedness)
        read_accessor_data(tangent_accessor, tangents, 4);
      }

      // Build vertices
      uint32_t base_vertex = static_cast<uint32_t>(vertices.size());
      size_t num_verts = position_accessor->count;

      for (size_t i = 0; i < num_verts; ++i)
      {
        Vertex v;

        v.position = glm::vec3(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);

        if (!normals.empty())
        {
          v.normal = glm::vec3(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]);
        }
        else
        {
          v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        if (!texcoords.empty())
        {
          v.texCoord = glm::vec2(texcoords[i * 2 + 0], texcoords[i * 2 + 1]);
        }
        else
        {
          v.texCoord = glm::vec2(0.0f);
        }

        if (!colors.empty())
        {
          size_t color_components = (color_accessor->type == cgltf_type_vec4) ? 4 : 3;
          v.color = glm::vec3(colors[i * color_components + 0],
                              colors[i * color_components + 1],
                              colors[i * color_components + 2]);
        }
        else
        {
          v.color = glm::vec3(1.0f);
        }

        if (!tangents.empty())
        {
          // glTF TANGENT: vec4 where xyz=tangent direction, w=handedness (+1 or -1)
          v.tangent = glm::vec4(tangents[i * 4 + 0], tangents[i * 4 + 1],
                                tangents[i * 4 + 2], tangents[i * 4 + 3]);
        }
        else
        {
          // Default tangent along X axis with positive handedness
          v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        }

        vertices.push_back(v);
      }

      // Read indices
      if (primitive.indices)
      {
        std::vector<uint32_t> prim_indices;
        read_index_data(primitive.indices, prim_indices);

        // Offset indices by base vertex
        for (uint32_t idx : prim_indices)
        {
          indices.push_back(base_vertex + idx);
        }
      }
      else
      {
        // Generate sequential indices
        for (size_t i = 0; i < num_verts; ++i)
        {
          indices.push_back(base_vertex + static_cast<uint32_t>(i));
        }
      }
    }
  }

  cgltf_free(data);

  if (vertices.empty())
  {
    spdlog::error("No vertices loaded from glTF file: {}", filepath);
    return nullptr;
  }

  // Compute smooth vertex normals if not present
  bool has_valid_normals = false;
  for (const auto& v : vertices)
  {
    if (glm::length(v.normal) > 0.01f)
    {
      has_valid_normals = true;
      break;
    }
  }

  if (!has_valid_normals && !indices.empty())
  {
    // Reset normals to zero
    for (auto& v : vertices)
    {
      v.normal = glm::vec3(0.0f);
    }

    // Accumulate face normals at each vertex
    for (size_t i = 0; i < indices.size(); i += 3)
    {
      uint32_t i0 = indices[i + 0];
      uint32_t i1 = indices[i + 1];
      uint32_t i2 = indices[i + 2];

      glm::vec3 v0 = vertices[i0].position;
      glm::vec3 v1 = vertices[i1].position;
      glm::vec3 v2 = vertices[i2].position;

      // Compute face normal (cross product of edges)
      glm::vec3 edge1 = v1 - v0;
      glm::vec3 edge2 = v2 - v0;
      glm::vec3 faceNormal = glm::cross(edge1, edge2);

      // Add to each vertex (weighted by area)
      vertices[i0].normal += faceNormal;
      vertices[i1].normal += faceNormal;
      vertices[i2].normal += faceNormal;
    }

    // Normalize all vertex normals
    for (auto& v : vertices)
    {
      if (glm::length(v.normal) > 0.0001f)
      {
        v.normal = glm::normalize(v.normal);
      }
      else
      {
        v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
      }
    }

    spdlog::trace("Computed smooth vertex normals for glTF mesh");
  }

  spdlog::trace("Loaded glTF mesh '{}': {} vertices, {} indices",
    mesh_name, vertices.size(), indices.size());

  // Create mesh
  if (indices.empty())
  {
    return std::make_unique<Mesh>(device, mesh_name, vertices);
  }
  else
  {
    return std::make_unique<Mesh>(device, mesh_name, vertices, indices);
  }
}

namespace
{

/// @brief Extract base color texture from glTF material.
/// @param data The parsed glTF data.
/// @param device The Vulkan device wrapper.
/// @param base_path Directory containing the glTF file.
/// @return Texture if found, nullptr otherwise.
std::unique_ptr<Texture> extract_base_color_texture(
  cgltf_data* data, const Device& device, const std::filesystem::path& base_path)
{
  // Find first material with a base color texture
  for (size_t mat_idx = 0; mat_idx < data->materials_count; ++mat_idx)
  {
    const cgltf_material& material = data->materials[mat_idx];

    if (!material.has_pbr_metallic_roughness)
    {
      continue;
    }

    const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
    const cgltf_texture_view& tex_view = pbr.base_color_texture;

    if (!tex_view.texture)
    {
      continue;
    }

    const cgltf_texture* texture = tex_view.texture;
    if (!texture->image)
    {
      continue;
    }

    const cgltf_image* image = texture->image;

    // Check if image data is embedded in the buffer
    if (image->buffer_view)
    {
      // Embedded image data (common in .glb files)
      const cgltf_buffer_view* buffer_view = image->buffer_view;
      const uint8_t* buffer_data =
        static_cast<const uint8_t*>(buffer_view->buffer->data) + buffer_view->offset;
      size_t buffer_size = buffer_view->size;

      // Decode with stb_image
      int width, height, channels;
      stbi_uc* pixels =
        stbi_load_from_memory(buffer_data, static_cast<int>(buffer_size), &width, &height,
          &channels, STBI_rgb_alpha);

      if (!pixels)
      {
        spdlog::warn("Failed to decode embedded texture");
        continue;
      }

      std::string tex_name = image->name ? image->name : "embedded_texture";
      auto tex = std::make_unique<Texture>(device, tex_name, pixels,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));

      stbi_image_free(pixels);

      spdlog::info("Loaded embedded base color texture: {} ({}x{})", tex_name, width, height);
      return tex;
    }
    else if (image->uri)
    {
      // External image file
      std::string uri = image->uri;

      // Skip data URIs (base64 encoded)
      if (uri.rfind("data:", 0) == 0)
      {
        spdlog::warn("Data URI textures not supported yet");
        continue;
      }

      // Resolve relative path
      std::filesystem::path tex_path = base_path / uri;

      if (!std::filesystem::exists(tex_path))
      {
        spdlog::warn("Texture file not found: {}", tex_path.string());
        continue;
      }

      std::string tex_name = image->name ? image->name : tex_path.stem().string();

      try
      {
        auto tex = std::make_unique<Texture>(device, tex_name, tex_path.string());
        spdlog::info("Loaded base color texture: {} from {}", tex_name, tex_path.string());
        return tex;
      }
      catch (const std::exception& e)
      {
        spdlog::warn("Failed to load texture {}: {}", tex_path.string(), e.what());
        continue;
      }
    }
  }

  return nullptr;
}

/// @brief Extract normal texture from glTF material.
/// @param data The parsed glTF data.
/// @param device The Vulkan device wrapper.
/// @param base_path Directory containing the glTF file.
/// @return Texture if found, nullptr otherwise.
/// @see glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material-normaltextureinfo
std::unique_ptr<Texture> extract_normal_texture(
  cgltf_data* data, const Device& device, const std::filesystem::path& base_path)
{
  // Find first material with a normal texture
  for (size_t mat_idx = 0; mat_idx < data->materials_count; ++mat_idx)
  {
    const cgltf_material& material = data->materials[mat_idx];
    const cgltf_texture_view& tex_view = material.normal_texture;

    if (!tex_view.texture)
    {
      continue;
    }

    const cgltf_texture* texture = tex_view.texture;
    if (!texture->image)
    {
      continue;
    }

    const cgltf_image* image = texture->image;

    // Check if image data is embedded in the buffer
    if (image->buffer_view)
    {
      // Embedded image data (common in .glb files)
      const cgltf_buffer_view* buffer_view = image->buffer_view;
      const uint8_t* buffer_data =
        static_cast<const uint8_t*>(buffer_view->buffer->data) + buffer_view->offset;
      size_t buffer_size = buffer_view->size;

      // Decode with stb_image
      int width, height, channels;
      stbi_uc* pixels =
        stbi_load_from_memory(buffer_data, static_cast<int>(buffer_size), &width, &height,
          &channels, STBI_rgb_alpha);

      if (!pixels)
      {
        spdlog::warn("Failed to decode embedded normal texture");
        continue;
      }

      std::string tex_name = image->name ? image->name : "embedded_normal";
      auto tex = std::make_unique<Texture>(device, tex_name, pixels,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));

      stbi_image_free(pixels);

      spdlog::info("Loaded embedded normal texture: {} ({}x{})", tex_name, width, height);
      return tex;
    }
    else if (image->uri)
    {
      // External image file
      std::string uri = image->uri;

      // Skip data URIs (base64 encoded)
      if (uri.rfind("data:", 0) == 0)
      {
        spdlog::warn("Data URI textures not supported yet");
        continue;
      }

      // Resolve relative path
      std::filesystem::path tex_path = base_path / uri;

      if (!std::filesystem::exists(tex_path))
      {
        spdlog::warn("Normal texture file not found: {}", tex_path.string());
        continue;
      }

      std::string tex_name = image->name ? image->name : tex_path.stem().string();

      try
      {
        auto tex = std::make_unique<Texture>(device, tex_name, tex_path.string());
        spdlog::info("Loaded normal texture: {} from {}", tex_name, tex_path.string());
        return tex;
      }
      catch (const std::exception& e)
      {
        spdlog::warn("Failed to load normal texture {}: {}", tex_path.string(), e.what());
        continue;
      }
    }
  }

  return nullptr;
}

/// @brief Extract metallic/roughness texture from glTF material.
/// @param data The parsed glTF data.
/// @param device The Vulkan device wrapper.
/// @param base_path Directory containing the glTF file.
/// @return Texture if found, nullptr otherwise.
/// @see glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material-pbrmetallicroughness
/// Note: glTF stores roughness in G channel, metallic in B channel
std::unique_ptr<Texture> extract_metallic_roughness_texture(
  cgltf_data* data, const Device& device, const std::filesystem::path& base_path)
{
  // Find first material with a metallic/roughness texture
  for (size_t mat_idx = 0; mat_idx < data->materials_count; ++mat_idx)
  {
    const cgltf_material& material = data->materials[mat_idx];

    if (!material.has_pbr_metallic_roughness)
    {
      continue;
    }

    const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
    const cgltf_texture_view& tex_view = pbr.metallic_roughness_texture;

    if (!tex_view.texture)
    {
      continue;
    }

    const cgltf_texture* texture = tex_view.texture;
    if (!texture->image)
    {
      continue;
    }

    const cgltf_image* image = texture->image;

    // Check if image data is embedded in the buffer
    if (image->buffer_view)
    {
      // Embedded image data (common in .glb files)
      const cgltf_buffer_view* buffer_view = image->buffer_view;
      const uint8_t* buffer_data =
        static_cast<const uint8_t*>(buffer_view->buffer->data) + buffer_view->offset;
      size_t buffer_size = buffer_view->size;

      // Decode with stb_image
      int width, height, channels;
      stbi_uc* pixels =
        stbi_load_from_memory(buffer_data, static_cast<int>(buffer_size), &width, &height,
          &channels, STBI_rgb_alpha);

      if (!pixels)
      {
        spdlog::warn("Failed to decode embedded metallic/roughness texture");
        continue;
      }

      std::string tex_name = image->name ? image->name : "embedded_metallic_roughness";
      auto tex = std::make_unique<Texture>(device, tex_name, pixels,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));

      stbi_image_free(pixels);

      spdlog::info("Loaded embedded metallic/roughness texture: {} ({}x{})", tex_name, width, height);
      return tex;
    }
    else if (image->uri)
    {
      // External image file
      std::string uri = image->uri;

      // Skip data URIs (base64 encoded)
      if (uri.rfind("data:", 0) == 0)
      {
        spdlog::warn("Data URI textures not supported yet");
        continue;
      }

      // Resolve relative path
      std::filesystem::path tex_path = base_path / uri;

      if (!std::filesystem::exists(tex_path))
      {
        spdlog::warn("Metallic/roughness texture file not found: {}", tex_path.string());
        continue;
      }

      std::string tex_name = image->name ? image->name : tex_path.stem().string();

      try
      {
        auto tex = std::make_unique<Texture>(device, tex_name, tex_path.string());
        spdlog::info("Loaded metallic/roughness texture: {} from {}", tex_name, tex_path.string());
        return tex;
      }
      catch (const std::exception& e)
      {
        spdlog::warn("Failed to load metallic/roughness texture {}: {}", tex_path.string(), e.what());
        continue;
      }
    }
  }

  return nullptr;
}

/// @brief Extract emissive texture from glTF material.
/// @param data The parsed glTF data.
/// @param device The Vulkan device wrapper.
/// @param base_path Directory containing the glTF file.
/// @return Texture if found, nullptr otherwise.
/// @see glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material
std::unique_ptr<Texture> extract_emissive_texture(
  cgltf_data* data, const Device& device, const std::filesystem::path& base_path)
{
  // Find first material with an emissive texture
  for (size_t mat_idx = 0; mat_idx < data->materials_count; ++mat_idx)
  {
    const cgltf_material& material = data->materials[mat_idx];
    const cgltf_texture_view& tex_view = material.emissive_texture;

    if (!tex_view.texture)
    {
      continue;
    }

    const cgltf_texture* texture = tex_view.texture;
    if (!texture->image)
    {
      continue;
    }

    const cgltf_image* image = texture->image;

    // Check if image data is embedded in the buffer
    if (image->buffer_view)
    {
      // Embedded image data (common in .glb files)
      const cgltf_buffer_view* buffer_view = image->buffer_view;
      const uint8_t* buffer_data =
        static_cast<const uint8_t*>(buffer_view->buffer->data) + buffer_view->offset;
      size_t buffer_size = buffer_view->size;

      // Decode with stb_image
      int width, height, channels;
      stbi_uc* pixels =
        stbi_load_from_memory(buffer_data, static_cast<int>(buffer_size), &width, &height,
          &channels, STBI_rgb_alpha);

      if (!pixels)
      {
        spdlog::warn("Failed to decode embedded emissive texture");
        continue;
      }

      std::string tex_name = image->name ? image->name : "embedded_emissive";
      auto tex = std::make_unique<Texture>(device, tex_name, pixels,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));

      stbi_image_free(pixels);

      spdlog::info("Loaded embedded emissive texture: {} ({}x{})", tex_name, width, height);
      return tex;
    }
    else if (image->uri)
    {
      // External image file
      std::string uri = image->uri;

      // Skip data URIs (base64 encoded)
      if (uri.rfind("data:", 0) == 0)
      {
        spdlog::warn("Data URI textures not supported yet");
        continue;
      }

      // Resolve relative path
      std::filesystem::path tex_path = base_path / uri;

      if (!std::filesystem::exists(tex_path))
      {
        spdlog::warn("Emissive texture file not found: {}", tex_path.string());
        continue;
      }

      std::string tex_name = image->name ? image->name : tex_path.stem().string();

      try
      {
        auto tex = std::make_unique<Texture>(device, tex_name, tex_path.string());
        spdlog::info("Loaded emissive texture: {} from {}", tex_name, tex_path.string());
        return tex;
      }
      catch (const std::exception& e)
      {
        spdlog::warn("Failed to load emissive texture {}: {}", tex_path.string(), e.what());
        continue;
      }
    }
  }

  return nullptr;
}

/// @brief Extract ambient occlusion texture from glTF material.
/// @param data The parsed glTF data.
/// @param device The Vulkan device wrapper.
/// @param base_path Directory containing the glTF file.
/// @return Texture if found, nullptr otherwise.
/// @see glTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material-occlusiontextureinfo
/// Note: glTF stores AO in the R channel
std::unique_ptr<Texture> extract_ao_texture(
  cgltf_data* data, const Device& device, const std::filesystem::path& base_path)
{
  // Find first material with an occlusion texture
  for (size_t mat_idx = 0; mat_idx < data->materials_count; ++mat_idx)
  {
    const cgltf_material& material = data->materials[mat_idx];
    const cgltf_texture_view& tex_view = material.occlusion_texture;

    if (!tex_view.texture)
    {
      continue;
    }

    const cgltf_texture* texture = tex_view.texture;
    if (!texture->image)
    {
      continue;
    }

    const cgltf_image* image = texture->image;

    // Check if image data is embedded in the buffer
    if (image->buffer_view)
    {
      // Embedded image data (common in .glb files)
      const cgltf_buffer_view* buffer_view = image->buffer_view;
      const uint8_t* buffer_data =
        static_cast<const uint8_t*>(buffer_view->buffer->data) + buffer_view->offset;
      size_t buffer_size = buffer_view->size;

      // Decode with stb_image
      int width, height, channels;
      stbi_uc* pixels =
        stbi_load_from_memory(buffer_data, static_cast<int>(buffer_size), &width, &height,
          &channels, STBI_rgb_alpha);

      if (!pixels)
      {
        spdlog::warn("Failed to decode embedded AO texture");
        continue;
      }

      std::string tex_name = image->name ? image->name : "embedded_ao";
      auto tex = std::make_unique<Texture>(device, tex_name, pixels,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));

      stbi_image_free(pixels);

      spdlog::info("Loaded embedded AO texture: {} ({}x{})", tex_name, width, height);
      return tex;
    }
    else if (image->uri)
    {
      // External image file
      std::string uri = image->uri;

      // Skip data URIs (base64 encoded)
      if (uri.rfind("data:", 0) == 0)
      {
        spdlog::warn("Data URI textures not supported yet");
        continue;
      }

      // Resolve relative path
      std::filesystem::path tex_path = base_path / uri;

      if (!std::filesystem::exists(tex_path))
      {
        spdlog::warn("AO texture file not found: {}", tex_path.string());
        continue;
      }

      std::string tex_name = image->name ? image->name : tex_path.stem().string();

      try
      {
        auto tex = std::make_unique<Texture>(device, tex_name, tex_path.string());
        spdlog::info("Loaded AO texture: {} from {}", tex_name, tex_path.string());
        return tex;
      }
      catch (const std::exception& e)
      {
        spdlog::warn("Failed to load AO texture {}: {}", tex_path.string(), e.what());
        continue;
      }
    }
  }

  return nullptr;
}

} // anonymous namespace

GltfModel load_gltf_model(const Device& device, const std::string& filepath)
{
  GltfModel model;

  // Check file exists
  if (!std::filesystem::exists(filepath))
  {
    spdlog::error("glTF file not found: {}", filepath);
    return model;
  }

  std::filesystem::path file_path(filepath);
  std::filesystem::path base_path = file_path.parent_path();

  // Parse glTF file
  cgltf_options options = {};
  cgltf_data* data = nullptr;

  cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);
  if (result != cgltf_result_success)
  {
    spdlog::error("Failed to parse glTF file: {} (error {})", filepath, static_cast<int>(result));
    return model;
  }

  // Load buffers (needed for binary data access)
  result = cgltf_load_buffers(&options, data, filepath.c_str());
  if (result != cgltf_result_success)
  {
    spdlog::error("Failed to load glTF buffers: {} (error {})", filepath, static_cast<int>(result));
    cgltf_free(data);
    return model;
  }

  // Extract textures first (before we free cgltf_data)
  model.baseColorTexture = extract_base_color_texture(data, device, base_path);
  model.normalTexture = extract_normal_texture(data, device, base_path);
  model.metallicRoughnessTexture = extract_metallic_roughness_texture(data, device, base_path);
  model.emissiveTexture = extract_emissive_texture(data, device, base_path);
  model.aoTexture = extract_ao_texture(data, device, base_path);

  cgltf_free(data);

  // Load mesh using existing function
  model.mesh = load_gltf(device, filepath);

  return model;
}

} // namespace sps::vulkan
