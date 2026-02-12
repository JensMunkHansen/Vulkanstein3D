#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace sps::vulkan
{

enum class Phase;

/// How a stage intends to access a shared image.
/// Used by the render graph for automatic barrier insertion.
enum class AccessIntent
{
  Read,      // Sample or load (e.g. CompositeStage reading HDR)
  Write,     // Store only (e.g. a clear pass)
  ReadWrite  // Load + store (e.g. SSSBlurStage ping-ponging HDR)
};

/// Non-owning description of a shared image resource.
/// All handles are borrowed — the actual owner (typically VulkanRenderer) manages lifetime.
struct SharedImageEntry
{
  vk::Image image;
  vk::ImageView image_view;
  vk::Sampler sampler;   // May be VK_NULL_HANDLE if not applicable
  vk::Format format{ vk::Format::eUndefined };
};

/// A stage's declared access to a shared image.
/// Collected at stage construction time; the render graph uses these
/// to determine required barriers between phases.
struct AccessRecord
{
  std::string stage_name;
  Phase phase;
  AccessIntent intent;
};

/// String-keyed registry for shared images that multiple stages need to access.
///
/// Two responsibilities:
/// 1. **Handle lookup**: stages query current image handles via get().
/// 2. **Access declarations**: stages declare their intent via declare_access()
///    at construction time. The render graph later uses these declarations
///    to insert pipeline barriers between phases automatically.
///
/// Populated by the application (or whoever owns the images) before stage construction
/// and updated on swapchain resize. Stages query it to get current handles.
///
/// Typical entries: "hdr", "depth_stencil", "hdr_msaa".
class SharedImageRegistry
{
public:
  void set(const std::string& name, const SharedImageEntry& entry) { m_entries[name] = entry; }

  [[nodiscard]] const SharedImageEntry* get(const std::string& name) const
  {
    auto it = m_entries.find(name);
    return it != m_entries.end() ? &it->second : nullptr;
  }

  [[nodiscard]] bool contains(const std::string& name) const
  {
    return m_entries.find(name) != m_entries.end();
  }

  /// Declare that a stage accesses a shared image with a given intent.
  /// Called once at stage construction. Multiple stages may declare access
  /// to the same image — the render graph uses the full list to determine
  /// what barriers are needed between phases.
  void declare_access(const std::string& image_name, const std::string& stage_name,
    Phase phase, AccessIntent intent)
  {
    m_access[image_name].push_back({ stage_name, phase, intent });
  }

  /// All access declarations for a given image, in declaration order.
  /// Returns empty vector if no stage has declared access.
  [[nodiscard]] const std::vector<AccessRecord>& access_records(
    const std::string& image_name) const
  {
    static const std::vector<AccessRecord> empty;
    auto it = m_access.find(image_name);
    return it != m_access.end() ? it->second : empty;
  }

private:
  std::unordered_map<std::string, SharedImageEntry> m_entries;
  std::unordered_map<std::string, std::vector<AccessRecord>> m_access;
};

} // namespace sps::vulkan
