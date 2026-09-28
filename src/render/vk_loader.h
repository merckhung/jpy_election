// Runtime Vulkan loader: dlopen()s / LoadLibrary()s the system Vulkan loader
// and resolves the entry points we use. Builds only need Vulkan *headers*
// (from the BCR); no link-time dependency on libvulkan / vulkan-1.lib.
#pragma once

#include <string>

#include <vulkan/vulkan.h>  // compiled with VK_NO_PROTOTYPES

namespace jpy::vk {

#define JPY_VK_GLOBAL_FUNCS(X)              \
  X(vkCreateInstance)                       \
  X(vkEnumerateInstanceExtensionProperties) \
  X(vkEnumerateInstanceLayerProperties)

#define JPY_VK_INSTANCE_FUNCS(X)                  \
  X(vkDestroyInstance)                            \
  X(vkEnumeratePhysicalDevices)                   \
  X(vkGetPhysicalDeviceProperties)                \
  X(vkGetPhysicalDeviceFeatures)                  \
  X(vkGetPhysicalDeviceQueueFamilyProperties)     \
  X(vkGetPhysicalDeviceMemoryProperties)          \
  X(vkGetPhysicalDeviceFormatProperties)          \
  X(vkEnumerateDeviceExtensionProperties)         \
  X(vkCreateDevice)                               \
  X(vkGetDeviceProcAddr)                          \
  X(vkDestroySurfaceKHR)                          \
  X(vkGetPhysicalDeviceSurfaceSupportKHR)         \
  X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)    \
  X(vkGetPhysicalDeviceSurfaceFormatsKHR)         \
  X(vkGetPhysicalDeviceSurfacePresentModesKHR)

#define JPY_VK_DEVICE_FUNCS(X)          \
  X(vkDestroyDevice)                    \
  X(vkGetDeviceQueue)                   \
  X(vkDeviceWaitIdle)                   \
  X(vkQueueSubmit)                      \
  X(vkQueueWaitIdle)                    \
  X(vkCreateSwapchainKHR)               \
  X(vkDestroySwapchainKHR)              \
  X(vkGetSwapchainImagesKHR)            \
  X(vkAcquireNextImageKHR)              \
  X(vkQueuePresentKHR)                  \
  X(vkCreateImage)                      \
  X(vkDestroyImage)                     \
  X(vkCreateImageView)                  \
  X(vkDestroyImageView)                 \
  X(vkCreateBuffer)                     \
  X(vkDestroyBuffer)                    \
  X(vkGetBufferMemoryRequirements)      \
  X(vkGetImageMemoryRequirements)       \
  X(vkAllocateMemory)                   \
  X(vkFreeMemory)                       \
  X(vkBindBufferMemory)                 \
  X(vkBindImageMemory)                  \
  X(vkMapMemory)                        \
  X(vkUnmapMemory)                      \
  X(vkCreateSampler)                    \
  X(vkDestroySampler)                   \
  X(vkCreateRenderPass)                 \
  X(vkDestroyRenderPass)                \
  X(vkCreateFramebuffer)                \
  X(vkDestroyFramebuffer)               \
  X(vkCreateShaderModule)               \
  X(vkDestroyShaderModule)              \
  X(vkCreatePipelineLayout)             \
  X(vkDestroyPipelineLayout)            \
  X(vkCreateGraphicsPipelines)          \
  X(vkDestroyPipeline)                  \
  X(vkCreateDescriptorSetLayout)        \
  X(vkDestroyDescriptorSetLayout)       \
  X(vkCreateDescriptorPool)             \
  X(vkDestroyDescriptorPool)            \
  X(vkAllocateDescriptorSets)           \
  X(vkFreeDescriptorSets)               \
  X(vkUpdateDescriptorSets)             \
  X(vkCreateCommandPool)                \
  X(vkDestroyCommandPool)               \
  X(vkAllocateCommandBuffers)           \
  X(vkFreeCommandBuffers)               \
  X(vkBeginCommandBuffer)               \
  X(vkEndCommandBuffer)                 \
  X(vkResetCommandBuffer)               \
  X(vkCreateFence)                      \
  X(vkDestroyFence)                     \
  X(vkWaitForFences)                    \
  X(vkResetFences)                      \
  X(vkCreateSemaphore)                  \
  X(vkDestroySemaphore)                 \
  X(vkCmdBeginRenderPass)               \
  X(vkCmdEndRenderPass)                 \
  X(vkCmdBindPipeline)                  \
  X(vkCmdBindDescriptorSets)            \
  X(vkCmdBindVertexBuffers)             \
  X(vkCmdBindIndexBuffer)               \
  X(vkCmdDraw)                          \
  X(vkCmdDrawIndexed)                   \
  X(vkCmdSetViewport)                   \
  X(vkCmdSetScissor)                    \
  X(vkCmdPushConstants)                 \
  X(vkCmdPipelineBarrier)               \
  X(vkCmdCopyBuffer)                    \
  X(vkCmdCopyBufferToImage)             \
  X(vkCmdCopyImageToBuffer)

#define JPY_VK_DECLARE(name) extern PFN_##name name;
JPY_VK_GLOBAL_FUNCS(JPY_VK_DECLARE)
JPY_VK_INSTANCE_FUNCS(JPY_VK_DECLARE)
JPY_VK_DEVICE_FUNCS(JPY_VK_DECLARE)
extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
#undef JPY_VK_DECLARE

// Loads the Vulkan loader (vulkan-1.dll / libvulkan) and global entry points.
// Returns false if unavailable.
bool LoadVulkanLoader();
// Unloads the loader and clears every entry point (so that a new ICD
// selection via environment variables takes effect on the next load).
void UnloadVulkanLoader();
// Windows: points VK_ICD_FILENAMES / VK_DRIVER_FILES at the bundled
// SwiftShader ICD (third_party/swiftshader, found next to the executable, in
// its runfiles or under `root`). Must be called before LoadVulkanLoader().
// Returns false if the ICD was not found (always false on other platforms).
bool ConfigureSwiftShaderIcd(const std::string& root);
void LoadInstanceFunctions(VkInstance instance);
void LoadDeviceFunctions(VkDevice device);

const char* ResultString(VkResult r);

}  // namespace jpy::vk
