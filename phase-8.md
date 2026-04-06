# Phase 8: Vulkan Validation Layers

This phase integrates Vulkan Validation Layers (`VK_LAYER_KHRONOS_validation`) directly into the core `ModernVulkanBackend` to natively detect runtime API synchronization faults, uninitialized memory reads, and invalid layout transitions.

## Checklist

- [x] **1. Configuration Parsing**
  - [x] Implement conditional compilation flag `#ifdef NDEBUG` setting a constant `enableValidationLayers = false`.
  - [x] Add `VK_LAYER_KHRONOS_validation` to the requested instance layers.

- [x] **2. Driver Availability Verification**
  - [x] Implement `checkValidationLayerSupport()` returning `true` only if `vkEnumerateInstanceLayerProperties` lists Khronos validation natively installed.

- [x] **3. Debug Utils Messenger (`VK_EXT_debug_utils`)**
  - [x] Inject `VK_EXT_DEBUG_UTILS_EXTENSION_NAME` into the `vkCreateInstance` extensions specifically when in Debug mode.
  - [x] Define the canonical `debugCallback` signature parsing Vulkan validation severities and messages seamlessly to `std::cerr`.
  - [x] Initialize `VkDebugUtilsMessengerEXT` parsing warning/error bits accurately.

- [x] **4. Extension Proxy Methods**
  - [x] Load the unexposed extension proxy functions dynamically (`vkCreateDebugUtilsMessengerEXT` and `vkDestroyDebugUtilsMessengerEXT`).
  - [x] Configure `ModernVulkanBackend::~ModernVulkanBackend` destroying the active callback prior to Instance termination.
