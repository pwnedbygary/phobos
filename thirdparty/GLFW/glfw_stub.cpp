#include "glfw3.h"
extern "C" {
    const char** glfwGetRequiredInstanceExtensions(uint32_t* count) { *count = 0; return nullptr; }
    int glfwGetPhysicalDevicePresentationSupport(VkInstance instance, VkPhysicalDevice device, uint32_t queuefamily) { return 1; }
    VkResult glfwCreateWindowSurface(VkInstance instance, GLFWwindow* window, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) { return VK_SUCCESS; }
    void glfwGetWindowSize(GLFWwindow* window, int* width, int* height) { if(width) *width = 1920; if(height) *height = 1080; }
    void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height) { if(width) *width = 1920; if(height) *height = 1080; }
}
