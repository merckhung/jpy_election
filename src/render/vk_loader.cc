#include "src/render/vk_loader.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace jpy::vk {

#define JPY_VK_DEFINE(name) PFN_##name name = nullptr;
JPY_VK_GLOBAL_FUNCS(JPY_VK_DEFINE)
JPY_VK_INSTANCE_FUNCS(JPY_VK_DEFINE)
JPY_VK_DEVICE_FUNCS(JPY_VK_DEFINE)
#undef JPY_VK_DEFINE
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

namespace {

#ifdef _WIN32
HMODULE g_lib = nullptr;
std::wstring g_swiftshader_dll;  // set by ConfigureSwiftShaderIcd()
#else
void* g_lib = nullptr;
#endif

void SetEnv(const char* name, const std::string& value) {
#ifdef _WIN32
  SetEnvironmentVariableA(name, value.c_str());
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

#ifdef _WIN32
std::filesystem::path ExePath() {
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
  if (n == 0 || n >= std::size(buf)) return {};
  return std::filesystem::path(std::wstring(buf, n));
}
#endif

}  // namespace

bool LoadVulkanLoader() {
  if (vkGetInstanceProcAddr) return true;
#ifdef _WIN32
  g_lib = LoadLibraryA("vulkan-1.dll");
  if (!g_lib && !g_swiftshader_dll.empty()) {
    // No Vulkan loader installed: talk to the SwiftShader ICD directly (it
    // exports the core entry points itself).
    g_lib = LoadLibraryW(g_swiftshader_dll.c_str());
  }
  if (!g_lib) return false;
  vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      reinterpret_cast<void*>(GetProcAddress(g_lib, "vkGetInstanceProcAddr")));
  if (!vkGetInstanceProcAddr) {
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        reinterpret_cast<void*>(GetProcAddress(g_lib, "vk_icdGetInstanceProcAddr")));
  }
#else
  const char* names[] = {"libvulkan.so.1", "libvulkan.so", "libvulkan.1.dylib",
                         "libMoltenVK.dylib"};
  for (const char* n : names) {
    g_lib = dlopen(n, RTLD_NOW | RTLD_LOCAL);
    if (g_lib) break;
  }
  if (!g_lib) return false;
  vkGetInstanceProcAddr =
      reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(g_lib, "vkGetInstanceProcAddr"));
#endif
  if (!vkGetInstanceProcAddr) return false;
#define JPY_VK_LOAD_GLOBAL(name) \
  name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(nullptr, #name));
  JPY_VK_GLOBAL_FUNCS(JPY_VK_LOAD_GLOBAL)
#undef JPY_VK_LOAD_GLOBAL
  return vkCreateInstance != nullptr;
}

void UnloadVulkanLoader() {
  if (!g_lib) return;
#ifdef _WIN32
  FreeLibrary(g_lib);
#else
  dlclose(g_lib);
#endif
  g_lib = nullptr;
  vkGetInstanceProcAddr = nullptr;
#define JPY_VK_RESET(name) name = nullptr;
  JPY_VK_GLOBAL_FUNCS(JPY_VK_RESET)
  JPY_VK_INSTANCE_FUNCS(JPY_VK_RESET)
  JPY_VK_DEVICE_FUNCS(JPY_VK_RESET)
#undef JPY_VK_RESET
}

bool ConfigureSwiftShaderIcd(const std::string& root) {
#ifdef _WIN32
  namespace fs = std::filesystem;
  const char* kRel = "third_party/swiftshader/vk_swiftshader_icd.json";
  std::vector<fs::path> candidates;
  const fs::path exe = ExePath();
  if (!exe.empty()) {
    // Bazel on Windows usually has no runfiles tree, only a manifest that maps
    // runfiles paths to their real locations.
    std::vector<fs::path> manifests = {fs::path(exe.wstring() + L".runfiles_manifest"),
                                       fs::path(exe.wstring() + L".runfiles") / "MANIFEST"};
    if (const char* m = std::getenv("RUNFILES_MANIFEST_FILE")) manifests.emplace_back(m);
    const std::string key = std::string("_main/") + kRel + " ";
    for (const fs::path& m : manifests) {
      std::ifstream in(m);
      for (std::string line; std::getline(in, line);) {
        if (line.compare(0, key.size(), key) == 0) {
          candidates.emplace_back(line.substr(key.size()));
          break;
        }
      }
    }
    const fs::path exe_dir = exe.parent_path();
    candidates.push_back(fs::path(exe.wstring() + L".runfiles") / "_main" / kRel);
    candidates.push_back(exe_dir / kRel);
    candidates.push_back(exe_dir / "swiftshader" / "vk_swiftshader_icd.json");
    candidates.push_back(exe_dir / "vk_swiftshader_icd.json");
  }
  if (const char* rf = std::getenv("RUNFILES_DIR")) {
    candidates.push_back(fs::path(rf) / "_main" / kRel);
  }
  if (!root.empty()) candidates.push_back(fs::path(root) / kRel);
  candidates.push_back(fs::path(kRel));
  for (const fs::path& c : candidates) {
    std::error_code ec;
    const fs::path p = fs::absolute(c, ec);
    if (ec || !fs::exists(p, ec)) continue;
    const std::string abs = p.string();
    SetEnv("VK_ICD_FILENAMES", abs);
    SetEnv("VK_DRIVER_FILES", abs);
    g_swiftshader_dll = (p.parent_path() / "vk_swiftshader.dll").wstring();
    std::fprintf(stderr, "jpy_election: using bundled SwiftShader ICD: %s\n", abs.c_str());
    return true;
  }
  return false;
#else
  (void)root;
  (void)&SetEnv;
  return false;
#endif
}

void LoadInstanceFunctions(VkInstance instance) {
#define JPY_VK_LOAD_INSTANCE(name) \
  name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));
  JPY_VK_INSTANCE_FUNCS(JPY_VK_LOAD_INSTANCE)
#undef JPY_VK_LOAD_INSTANCE
}

void LoadDeviceFunctions(VkDevice device) {
#define JPY_VK_LOAD_DEVICE(name) \
  name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));
  JPY_VK_DEVICE_FUNCS(JPY_VK_LOAD_DEVICE)
#undef JPY_VK_LOAD_DEVICE
}

const char* ResultString(VkResult r) {
  switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VK_ERROR_(other)";
  }
}

}  // namespace jpy::vk
