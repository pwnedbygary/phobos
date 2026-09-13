#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct GLFWwindow GLFWwindow; typedef struct GLFWmonitor GLFWmonitor;
#define GLFW_TRUE 1
#define GLFW_FALSE 0
const char** glfwGetRequiredInstanceExtensions(uint32_t* count);
int glfwGetPhysicalDevicePresentationSupport(VkInstance instance, VkPhysicalDevice device, uint32_t queuefamily);
VkResult glfwCreateWindowSurface(VkInstance instance, GLFWwindow* window, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);
void glfwGetWindowSize(GLFWwindow* window, int* width, int* height);
void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height);
#ifdef __cplusplus
}
#endif
