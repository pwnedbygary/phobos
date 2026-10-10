//The Vulkan backend: what the renderer recorded (GPU::Recorded) run on the GPU's rasterizer. Each target is a color
//picture (8888) and a depth and stencil one (32-bit float depth with 8-bit stencil, or 24-bit depth where that's
//all there is) in one render pass; each texture a picture sampled by texelFetch; each Pipeline key a graphics
//pipeline, its fragment shader specialized by the key's constants and kept. The vertices and the pictures to upload
//go in a staging buffer of the run's slot (eight, round: the CPU fills one while the GPU runs the others); read-backs
//go into a buffer the host reads once finish() has waited for everything.
//
//Vulkan's functions come from the vkGetInstanceProcAddr the host hands over (the loader or driver it chose: a
//custom one loaded by libadrenotools on Android included), into this backend's own tables; nothing goes through
//volk, whose globals paraLLEl-RDP (the N64's renderer) keeps for itself. With none handed over, the system's loader
//is opened here (tools and tests).
//
//A wait that lasts far longer than any run could (five seconds: a driver that never says the device is lost) marks
//the device lost, as VK_ERROR_DEVICE_LOST does, and the software renderer draws from then on (GPU::ready()).
//
//At a higher resolution (Backend::scale above 1: docs/psp-gpu-renderers.md, "Upscaling") each target's pictures are
//scale times the PSP's size each way, and every command's rectangle is scaled with them. What Vulkan's copies can't
//scale is drawn: memory's pixels are copied into pictures of the PSP's size and enlarged into the target by
//copy.frag (the colors, the depth, and the stencil a bit at a time); read back for memory, a target is shrunk by a
//blit that takes one of each pixel's scale x scale (the one at its middle, or just right of and below it), and its
//stencil comes back a row of each pixel's at a time, the same one of each taken on the CPU. So memory's bytes the GPU
//didn't draw over come back as they went in, at any scale.
//
//Presenting (docs/psp-gpu-renderers.md, "Presenting"): given the host's window (Android's), the screen's picture is
//drawn over the whole of a swapchain image by present.frag and presented, in the run that has the Present, with
//nothing read back: from the frame buffer's target, or from memory's picture, uploaded. The swapchain is made as
//it's first shown on and again when it's out of date; where the window can't have one (no surface, no swapchain),
//presenting is given up and the host shows the frames.

#define PSP_VULKAN_INSTANCE(F) \
  F(vkDestroyInstance) F(vkEnumeratePhysicalDevices) F(vkGetPhysicalDeviceProperties) \
  F(vkGetPhysicalDeviceFeatures) F(vkGetPhysicalDeviceQueueFamilyProperties) \
  F(vkGetPhysicalDeviceMemoryProperties) F(vkGetPhysicalDeviceFormatProperties) \
  F(vkEnumerateDeviceExtensionProperties) F(vkCreateDevice) F(vkGetDeviceProcAddr)
#define PSP_VULKAN_DEVICE(F) \
  F(vkDestroyDevice) F(vkGetDeviceQueue) F(vkDeviceWaitIdle) F(vkCreateBuffer) F(vkDestroyBuffer) \
  F(vkGetBufferMemoryRequirements) F(vkCreateImage) F(vkDestroyImage) F(vkGetImageMemoryRequirements) \
  F(vkAllocateMemory) F(vkFreeMemory) F(vkBindBufferMemory) F(vkBindImageMemory) F(vkMapMemory) \
  F(vkInvalidateMappedMemoryRanges) F(vkCreateImageView) F(vkDestroyImageView) F(vkCreateSampler) \
  F(vkDestroySampler) F(vkCreateRenderPass) F(vkDestroyRenderPass) F(vkCreateFramebuffer) \
  F(vkDestroyFramebuffer) F(vkCreateShaderModule) F(vkDestroyShaderModule) F(vkCreateDescriptorSetLayout) \
  F(vkDestroyDescriptorSetLayout) F(vkCreatePipelineLayout) F(vkDestroyPipelineLayout) \
  F(vkCreateDescriptorPool) F(vkDestroyDescriptorPool) F(vkAllocateDescriptorSets) F(vkFreeDescriptorSets) \
  F(vkUpdateDescriptorSets) F(vkCreateGraphicsPipelines) F(vkDestroyPipeline) F(vkCreatePipelineCache) \
  F(vkDestroyPipelineCache) F(vkCreateCommandPool) F(vkDestroyCommandPool) F(vkAllocateCommandBuffers) \
  F(vkResetCommandBuffer) F(vkBeginCommandBuffer) F(vkEndCommandBuffer) F(vkCreateFence) F(vkDestroyFence) \
  F(vkWaitForFences) F(vkResetFences) F(vkQueueSubmit) F(vkCmdPipelineBarrier) F(vkCmdCopyBufferToImage) \
  F(vkCmdCopyImageToBuffer) F(vkCmdCopyImage) F(vkCmdBeginRenderPass) F(vkCmdEndRenderPass) F(vkCmdBindPipeline) \
  F(vkCmdBindDescriptorSets) F(vkCmdBindVertexBuffers) F(vkCmdSetViewport) F(vkCmdSetScissor) \
  F(vkCmdSetStencilReference) F(vkCmdSetStencilCompareMask) F(vkCmdSetStencilWriteMask) \
  F(vkCmdSetBlendConstants) F(vkCmdPushConstants) F(vkCmdDraw) F(vkCmdBlitImage) F(vkCmdClearAttachments) \
  F(vkCmdBindIndexBuffer) F(vkCmdDrawIndexed)
//Presenting's, where there's a window to show on: missing ones mean only that there's no presenting
#define PSP_VULKAN_SURFACE(F) \
  F(vkDestroySurfaceKHR) F(vkGetPhysicalDeviceSurfaceSupportKHR) F(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
  F(vkGetPhysicalDeviceSurfaceFormatsKHR) F(vkGetPhysicalDeviceSurfacePresentModesKHR)
#define PSP_VULKAN_SWAPCHAIN(F) \
  F(vkCreateSwapchainKHR) F(vkDestroySwapchainKHR) F(vkGetSwapchainImagesKHR) F(vkAcquireNextImageKHR) \
  F(vkQueuePresentKHR) F(vkCreateSemaphore) F(vkDestroySemaphore)

struct VulkanFunctions {
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
  PFN_vkCreateInstance vkCreateInstance = nullptr;
  PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
  #define F(name) PFN_##name name = nullptr;
  PSP_VULKAN_INSTANCE(F)
  PSP_VULKAN_DEVICE(F)
  PSP_VULKAN_SURFACE(F)
  PSP_VULKAN_SWAPCHAIN(F)
  #undef F
  #if defined(__ANDROID__)
  PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR = nullptr;
  #endif
};

//A Vulkan structure, zeroed, of its type.
template<typename T> static auto made(VkStructureType type) -> T {
  T value{};
  value.sType = type;
  return value;
}

struct VulkanBackend : GPU::Backend {
  struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    u8* mapped = nullptr;
    VkDeviceSize size = 0;
    bool coherent = true;
  };
  struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
  };
  struct Target {
    u32 width = 0, height = 0;  //the PSP's pixels it has room for
    u32 rows = 0;               //rows of them its pictures have (at a higher resolution, as many as reached: fit())
    Image color, depth;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;  //(its colors sampled, to be presented: made when first presented)
    VkDescriptorSet frame = VK_NULL_HANDLE;  //its colors as draw.frag's input attachment (shader blending)
    bool fresh = true;  //its pictures not yet in their layouts (the next run puts them there)
  };
  //A picture the CPU fills for the GPU to read, at the PSP's size (a higher resolution's uploads: copy.frag), with
  //the descriptor set that samples it; made larger when a bigger one is wanted
  struct Staged {
    Image image;
    VkDescriptorSet set = VK_NULL_HANDLE;
    u32 width = 0, height = 0;
  };
  //Pictures and sets no longer used, destroyed once the runs that may use them are done
  struct Retired {
    u64 serial = 0;
    Image color = {}, depth = {};
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE, frame = VK_NULL_HANDLE;
  };
  struct Texture {
    u32 width = 0, height = 0;
    Image image;
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::vector<u32> pending;  //its texels, until the next run puts them on the GPU
    bool fresh = true;         //never written yet (its layout undefined)
  };
  //A run's: its commands, the fence it signals, and the vertices and pictures it uploads; presenting's, the
  //semaphore the swapchain image's acquiring signals, and the shot of a picture it presents from a target (the
  //PSP's width x height of it, in the GE format, taken from the buffer once the run is done)
  struct Slot {
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool busy = false;
    u64 serial = 0;
    Buffer staging;
    VkDescriptorSet transforms = VK_NULL_HANDLE;  //(fast mode's: the run's Transformed blocks, in its staging buffer)
    VkSemaphore acquired = VK_NULL_HANDLE;
    Buffer shot;
    bool shooting = false;
    u32 shotWidth = 0, shotHeight = 0, shotFormat = 3;
  };
  static constexpr u32 Slots = 8;
  static constexpr u64 Timeout = 5'000'000'000;
  //(general: a target's colors are its render pass's color attachment and input attachment at once, for shader
  //blending's draws to read)
  static constexpr VkImageLayout ColorLayout = VK_IMAGE_LAYOUT_GENERAL;
  static constexpr VkImageLayout DepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VulkanFunctions vk;
  void* library = nullptr;  //the system's loader, opened here (no vkGetInstanceProcAddr from the host)
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  u32 family = 0;
  VkPhysicalDeviceMemoryProperties memoryTypes{};
  std::string deviceName;
  VkFormat depthFormat = VK_FORMAT_UNDEFINED;
  bool depthClamp = false;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout frameLayout = VK_NULL_HANDLE;  //(set 1 of draws: a target's Target::frame)
  VkDescriptorSetLayout transformLayout = VK_NULL_HANDLE;  //(set 2: fast mode's Transformed blocks, Slot::transforms)
  u32 storageAlignment = 256;  //(their place in the staging buffer)
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VkShaderModule vertexModule = VK_NULL_HANDLE, fragmentModule = VK_NULL_HANDLE;
  VkShaderModule transformModule = VK_NULL_HANDLE;  //(fast mode's: transform.vert)
  VkPipelineCache pipelineCache = VK_NULL_HANDLE;
  VkCommandPool commandPool = VK_NULL_HANDLE;
  std::map<std::array<u8, sizeof(GPU::Pipeline)>, VkPipeline> pipelineCache_;  //(VK_NULL_HANDLE: it failed)
  std::unordered_map<u32, Target> targetImages;
  std::unordered_map<u32, Texture> textureImages;  //0: a blank one, bound for untextured draws
  u32 nextId = 1;
  Slot slots[Slots];
  u32 next = 0;
  u64 showedLast = 0, showedBefore = 0;  //(the serials of the last two runs that showed a frame: run())
  u64 submitted = 0, completed = 0;  //runs' serials
  //Targets and textures let go of, destroyed once the runs that may use them are done
  struct Grave { u64 serial; u32 id; bool texture; };
  std::vector<Grave> graves;
  Buffer readback;
  std::vector<std::pair<u64, u64>> readbacks;  //the last finish()'s: where each one's colors and stencils are
  std::vector<std::pair<u32, u32>> readbackSizes;
  bool timedOut = false;  //a run never finished: what it uses may still be in use, so nothing is destroyed
  //(a higher resolution's: memory's colors and depths for copy.frag, its pipelines (MODE 0-2) and their layout, the
  //picture targets are shrunk into to be read back, and the stencils read back a row of each pixel's at a time, each
  //made the PSP's size after the wait: readback n's, its width and height)
  Staged stagedColors, stagedDepths;
  VkShaderModule copyVertexModule = VK_NULL_HANDLE, copyFragmentModule = VK_NULL_HANDLE;
  VkPipelineLayout copyLayout = VK_NULL_HANDLE;
  VkPipeline copyPipelines[3] = {};
  Image shrunk;
  u32 shrunkWidth = 0, shrunkHeight = 0;
  struct Squeeze { u32 readback, width, height; };
  std::vector<Squeeze> squeezes;
  std::vector<VkBufferImageCopy> rows;
  std::vector<Retired> retired;
  //Presenting: whether the instance and device can (canPresent); the window shown on (the host's, as handed over),
  //its surface and swapchain (made again when rebuild says it's out of date), and failed: presenting given up for
  //this window. The swapchain's images, each with its view, framebuffer and the semaphore its drawing signals for
  //the present; the render pass and present.frag's pipelines (FORMAT 0-3) for the swapchain's format; memory's
  //picture, uploaded to be presented; the newest shot (Slot), for shot().
  struct Swapped {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkSemaphore rendered = VK_NULL_HANDLE;
  };
  bool canPresent = false, failed = false, rebuild = false;
  //The render pass's subpass, and so every pipeline, asks for rasterization order (readsInOrder as the device was
  //made: the renderer may stop counting on it later, splitting draws again, while the pass stays as it was)
  bool orderedPass = false;
  void* shownOn = nullptr;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkExtent2D swapExtent{};
  std::vector<Swapped> swapImages;
  VkFormat presentFormat = VK_FORMAT_UNDEFINED;
  VkRenderPass presentPass = VK_NULL_HANDLE;
  VkShaderModule presentModule = VK_NULL_HANDLE;
  VkPipelineLayout presentLayout = VK_NULL_HANDLE;
  VkPipeline presentPipelines[4] = {};
  Staged shownPicture;
  std::vector<u32> shotPixels;
  u32 shotWidth = 0, shotHeight = 0, shotFormat = 3;
  bool shotNew = false;
  static constexpr u64 AcquireTimeout = 100'000'000;  //(a tenth of a second: a frame not shown rather than stuck)

  ~VulkanBackend() override {
    if(!timedOut && device) {
      vk.vkDeviceWaitIdle(device);
      for(auto& [id, t] : targetImages) destroy(t);
      for(auto& [id, t] : textureImages) destroy(t);
      for(auto& [key, pipeline] : pipelineCache_) if(pipeline) vk.vkDestroyPipeline(device, pipeline, nullptr);
      for(auto& r : retired) destroy(r);
      for(auto* staged : {&stagedColors, &stagedDepths}) {
        if(staged->set) vk.vkFreeDescriptorSets(device, descriptorPool, 1, &staged->set);
        destroy(staged->image);
      }
      destroy(shrunk);
      dropSurface();
      dropPresentPass();
      if(presentLayout) vk.vkDestroyPipelineLayout(device, presentLayout, nullptr);
      if(presentModule) vk.vkDestroyShaderModule(device, presentModule, nullptr);
      if(shownPicture.set) vk.vkFreeDescriptorSets(device, descriptorPool, 1, &shownPicture.set);
      destroy(shownPicture.image);
      for(auto pipeline : copyPipelines) if(pipeline) vk.vkDestroyPipeline(device, pipeline, nullptr);
      if(copyLayout) vk.vkDestroyPipelineLayout(device, copyLayout, nullptr);
      if(copyVertexModule) vk.vkDestroyShaderModule(device, copyVertexModule, nullptr);
      if(copyFragmentModule) vk.vkDestroyShaderModule(device, copyFragmentModule, nullptr);
      for(auto& slot : slots) {
        release(slot.staging), release(slot.shot);
        if(slot.fence) vk.vkDestroyFence(device, slot.fence, nullptr);
        if(slot.acquired) vk.vkDestroySemaphore(device, slot.acquired, nullptr);
      }
      release(readback);
      if(commandPool) vk.vkDestroyCommandPool(device, commandPool, nullptr);
      if(pipelineCache) vk.vkDestroyPipelineCache(device, pipelineCache, nullptr);
      if(vertexModule) vk.vkDestroyShaderModule(device, vertexModule, nullptr);
      if(transformModule) vk.vkDestroyShaderModule(device, transformModule, nullptr);
      if(fragmentModule) vk.vkDestroyShaderModule(device, fragmentModule, nullptr);
      if(sampler) vk.vkDestroySampler(device, sampler, nullptr);
      if(descriptorPool) vk.vkDestroyDescriptorPool(device, descriptorPool, nullptr);
      if(pipelineLayout) vk.vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
      if(setLayout) vk.vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
      if(frameLayout) vk.vkDestroyDescriptorSetLayout(device, frameLayout, nullptr);
      if(transformLayout) vk.vkDestroyDescriptorSetLayout(device, transformLayout, nullptr);
      if(renderPass) vk.vkDestroyRenderPass(device, renderPass, nullptr);
      vk.vkDestroyDevice(device, nullptr);
    }
    if(!timedOut && instance) vk.vkDestroyInstance(instance, nullptr);
    #if !defined(_WIN32)
    if(library) dlclose(library);
    #endif
  }

  auto name() const -> std::string override { return "Vulkan: " + deviceName; }

  //Memory of one of the wanted kinds, the first there is, for what needs it.
  auto allocate(const VkMemoryRequirements& needs, std::initializer_list<VkMemoryPropertyFlags> wanted,
                VkDeviceMemory& memory, VkMemoryPropertyFlags* got = nullptr) -> bool {
    for(auto flags : wanted) {
      for(u32 type = 0; type < memoryTypes.memoryTypeCount; type++) {
        auto has = memoryTypes.memoryTypes[type].propertyFlags;
        if(!(needs.memoryTypeBits >> type & 1) || (has & flags) != flags) continue;
        auto info = made<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        info.allocationSize = needs.size, info.memoryTypeIndex = type;
        if(vk.vkAllocateMemory(device, &info, nullptr, &memory) != VK_SUCCESS) continue;
        if(got) *got = has;
        return true;
      }
    }
    return false;
  }

  //A buffer the host writes (staging: coherent) or reads (read-backs: cached, the GPU's writes made visible by
  //invalidating), mapped for good.
  auto make(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage, bool reading) -> bool {
    release(buffer);
    auto info = made<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = size, info.usage = usage, info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if(vk.vkCreateBuffer(device, &info, nullptr, &buffer.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements needs;
    vk.vkGetBufferMemoryRequirements(device, buffer.buffer, &needs);
    constexpr auto Visible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, Coherent = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    constexpr auto Cached = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    VkMemoryPropertyFlags got = 0;
    bool allocated = reading ? allocate(needs, {Visible | Cached | Coherent, Visible | Cached, Visible | Coherent},
                                        buffer.memory, &got)
                             : allocate(needs, {Visible | Coherent}, buffer.memory, &got);
    if(!allocated) return release(buffer), false;
    buffer.coherent = got & Coherent;
    void* mapped = nullptr;
    if(vk.vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0) != VK_SUCCESS ||
       vk.vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
      return release(buffer), false;
    }
    buffer.mapped = (u8*)mapped, buffer.size = size;
    return true;
  }
  auto release(Buffer& buffer) -> void {
    if(buffer.buffer) vk.vkDestroyBuffer(device, buffer.buffer, nullptr);
    if(buffer.memory) vk.vkFreeMemory(device, buffer.memory, nullptr);  //(unmapped with it)
    buffer = {};
  }

  auto make(Image& image, u32 width, u32 height, VkFormat format, VkImageUsageFlags usage,
            VkImageAspectFlags aspect) -> bool {
    auto info = made<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    info.imageType = VK_IMAGE_TYPE_2D, info.format = format, info.extent = {width, height, 1};
    info.mipLevels = 1, info.arrayLayers = 1, info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL, info.usage = usage, info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if(vk.vkCreateImage(device, &info, nullptr, &image.image) != VK_SUCCESS) return false;
    VkMemoryRequirements needs;
    vk.vkGetImageMemoryRequirements(device, image.image, &needs);
    if(!allocate(needs, {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0}, image.memory)) return false;
    if(vk.vkBindImageMemory(device, image.image, image.memory, 0) != VK_SUCCESS) return false;
    auto view = made<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view.image = image.image, view.viewType = VK_IMAGE_VIEW_TYPE_2D, view.format = format;
    view.subresourceRange = {aspect, 0, 1, 0, 1};
    return vk.vkCreateImageView(device, &view, nullptr, &image.view) == VK_SUCCESS;
  }
  auto destroy(Image& image) -> void {
    if(image.view) vk.vkDestroyImageView(device, image.view, nullptr);
    if(image.image) vk.vkDestroyImage(device, image.image, nullptr);
    if(image.memory) vk.vkFreeMemory(device, image.memory, nullptr);
    image = {};
  }
  auto destroy(Target& t) -> void {
    if(t.set) vk.vkFreeDescriptorSets(device, descriptorPool, 1, &t.set);
    if(t.frame) vk.vkFreeDescriptorSets(device, descriptorPool, 1, &t.frame);
    if(t.framebuffer) vk.vkDestroyFramebuffer(device, t.framebuffer, nullptr);
    destroy(t.color), destroy(t.depth);
  }
  auto destroy(Texture& t) -> void {
    if(t.set) vk.vkFreeDescriptorSets(device, descriptorPool, 1, &t.set);
    destroy(t.image);
  }
  auto destroy(Retired& r) -> void {
    if(r.framebuffer) vk.vkDestroyFramebuffer(device, r.framebuffer, nullptr);
    if(r.set) vk.vkFreeDescriptorSets(device, descriptorPool, 1, &r.set);
    if(r.frame) vk.vkFreeDescriptorSets(device, descriptorPool, 1, &r.frame);
    destroy(r.color), destroy(r.depth);
  }

  //A target's pictures, rows of the PSP's tall (and its width), at the scale: the colors (sampled too, to be shown,
  //and read by shader blending's draws: Target::frame) and the depth and stencil, and the framebuffer that draws
  //into them
  auto makePictures(Target& t, u32 rows) -> bool {
    constexpr auto Transfers = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    u32 width = t.width * scale, height = rows * scale;
    bool ok = make(t.color, width, height, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | Transfers,
                   VK_IMAGE_ASPECT_COLOR_BIT) &&
              make(t.depth, width, height, depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | Transfers,
                   VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    if(ok) {
      VkImageView views[2] = {t.color.view, t.depth.view};
      auto info = made<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
      info.renderPass = renderPass, info.attachmentCount = 2, info.pAttachments = views;
      info.width = width, info.height = height, info.layers = 1;
      ok = vk.vkCreateFramebuffer(device, &info, nullptr, &t.framebuffer) == VK_SUCCESS;
    }
    if(ok) {
      auto setInfo = made<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
      setInfo.descriptorPool = descriptorPool, setInfo.descriptorSetCount = 1, setInfo.pSetLayouts = &frameLayout;
      ok = vk.vkAllocateDescriptorSets(device, &setInfo, &t.frame) == VK_SUCCESS;
      if(!ok) t.frame = VK_NULL_HANDLE;
    }
    if(ok) {
      VkDescriptorImageInfo picture{VK_NULL_HANDLE, t.color.view, ColorLayout};
      auto write = made<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
      write.dstSet = t.frame, write.descriptorCount = 1;
      write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, write.pImageInfo = &picture;
      vk.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    t.rows = rows;
    return ok;
  }

  //(at a higher resolution, a target starts as tall as a PSP's screen, 272 rows and a few, and grows as it's
  //reached: 512 rows of 512 pixels at 10 times the PSP's size would be 300 MiB, a screen's 160)
  auto makeTarget(u32 width, u32 height) -> u32 override {
    if(lost) return 0;
    Target t;
    t.width = width, t.height = height;
    if(!makePictures(t, scale == 1 ? height : std::min(height, 288u))) return destroy(t), 0;
    u32 id = nextId++;
    targetImages[id] = t;
    return id;
  }

  //A target at a higher resolution made taller, before a command reaches rows past its pictures' (in steps of 32
  //rows of the PSP's, at most its height): its pixels, colors, depth and stencil, copied into the taller pictures,
  //and the shorter ones destroyed once the runs that use them are done. False where the GPU has no room.
  auto fit(VkCommandBuffer commands, Target& t, u32 rows) -> bool {
    if(rows <= t.rows) return true;
    Target grown;
    grown.width = t.width, grown.height = t.height;
    if(!makePictures(grown, std::min(t.height, (rows + 31) & ~31u))) return destroy(grown), false;
    constexpr auto DepthStencil = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    constexpr auto Source = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Destination = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transition(commands, grown.color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, Destination);
    transition(commands, grown.depth.image, DepthStencil, VK_IMAGE_LAYOUT_UNDEFINED, Destination);
    transition(commands, t.color.image, VK_IMAGE_ASPECT_COLOR_BIT, ColorLayout, Source);
    transition(commands, t.depth.image, DepthStencil, DepthLayout, Source);
    VkExtent3D extent{t.width * scale, t.rows * scale, 1};
    VkImageCopy color{{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {}, extent};
    VkImageCopy depth{{DepthStencil, 0, 0, 1}, {}, {DepthStencil, 0, 0, 1}, {}, extent};
    vk.vkCmdCopyImage(commands, t.color.image, Source, grown.color.image, Destination, 1, &color);
    vk.vkCmdCopyImage(commands, t.depth.image, Source, grown.depth.image, Destination, 1, &depth);
    transition(commands, grown.color.image, VK_IMAGE_ASPECT_COLOR_BIT, Destination, ColorLayout);
    transition(commands, grown.depth.image, DepthStencil, Destination, DepthLayout);
    retired.push_back({submitted + 1, t.color, t.depth, t.framebuffer, t.set, t.frame});
    t.color = grown.color, t.depth = grown.depth, t.framebuffer = grown.framebuffer, t.rows = grown.rows;
    t.set = VK_NULL_HANDLE, t.frame = grown.frame;
    return true;
  }

  //A Staged picture at least width x height (made afresh, the old one destroyed once its runs are done)
  auto stage(Staged& staged, u32 width, u32 height) -> bool {
    if(width <= staged.width && height <= staged.height && staged.set) return true;
    if(staged.set || staged.image.image) {
      retired.push_back({submitted + 1, staged.image, {}, VK_NULL_HANDLE, staged.set});
    }
    staged = {};
    width = std::max(width, 512u), height = std::max(height, 512u);
    if(!make(staged.image, width, height, VK_FORMAT_R8G8B8A8_UNORM,
             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT)) {
      return destroy(staged.image), false;
    }
    auto setInfo = made<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    setInfo.descriptorPool = descriptorPool, setInfo.descriptorSetCount = 1, setInfo.pSetLayouts = &setLayout;
    if(vk.vkAllocateDescriptorSets(device, &setInfo, &staged.set) != VK_SUCCESS) {
      return staged.set = VK_NULL_HANDLE, destroy(staged.image), false;
    }
    VkDescriptorImageInfo image{sampler, staged.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    auto write = made<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = staged.set, write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, write.pImageInfo = &image;
    vk.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    staged.width = width, staged.height = height;
    return true;
  }

  //The picture a target is shrunk into to be read back, at least width x height
  auto shrink(u32 width, u32 height) -> bool {
    if(width <= shrunkWidth && height <= shrunkHeight) return true;
    if(shrunk.image) retired.push_back({submitted + 1, shrunk});
    shrunk = {};
    shrunkWidth = std::max(width, 512u), shrunkHeight = std::max(height, 512u);
    if(make(shrunk, shrunkWidth, shrunkHeight, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT)) {
      return true;
    }
    destroy(shrunk), shrunkWidth = shrunkHeight = 0;
    return false;
  }
  auto dropTarget(u32 id) -> void override { graves.push_back({submitted + 1, id, false}); }

  auto makeTexture(u32 width, u32 height, const u32* texels) -> u32 override {
    if(lost || !width || !height) return 0;
    Texture t;
    t.width = width, t.height = height;
    if(!make(t.image, width, height, VK_FORMAT_R8G8B8A8_UNORM,
             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT)) {
      return destroy(t), 0;
    }
    auto setInfo = made<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    setInfo.descriptorPool = descriptorPool, setInfo.descriptorSetCount = 1, setInfo.pSetLayouts = &setLayout;
    if(vk.vkAllocateDescriptorSets(device, &setInfo, &t.set) != VK_SUCCESS) {
      return t.set = VK_NULL_HANDLE, destroy(t), 0;
    }
    VkDescriptorImageInfo image{sampler, t.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    auto write = made<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = t.set, write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, write.pImageInfo = &image;
    vk.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    if(texels) t.pending.assign(texels, texels + width * height);
    u32 id = nextId++;
    textureImages[id] = std::move(t);
    return id;
  }
  auto dropTexture(u32 id) -> void override { graves.push_back({submitted + 1, id, true}); }

  //Those let go of whose runs are done, destroyed.
  auto bury() -> void {
    for(auto r = retired.begin(); r != retired.end();) {
      if(r->serial > completed) { r++; continue; }
      destroy(*r), r = retired.erase(r);
    }
    for(auto grave = graves.begin(); grave != graves.end();) {
      if(grave->serial > completed) { grave++; continue; }
      if(grave->texture) {
        auto t = textureImages.find(grave->id);
        if(t != textureImages.end()) destroy(t->second), textureImages.erase(t);
      } else {
        auto t = targetImages.find(grave->id);
        if(t != targetImages.end()) destroy(t->second), targetImages.erase(t);
      }
      grave = graves.erase(grave);
    }
  }

  auto wait(Slot& slot) -> bool {
    if(!slot.busy) return true;
    VkResult waited = vk.vkWaitForFences(device, 1, &slot.fence, VK_TRUE, Timeout);
    if(waited == VK_TIMEOUT) return lost = timedOut = true, false;  //(the fence still waited on: left as it is)
    if(waited != VK_SUCCESS) return lost = true, false;
    vk.vkResetFences(device, 1, &slot.fence);
    slot.busy = false;
    completed = std::max(completed, slot.serial);
    if(slot.shooting) keep(slot);
    return true;
  }

  //A done run's shot of what it presented, kept as the newest (shot())
  auto keep(Slot& slot) -> void {
    slot.shooting = false;
    if(!slot.shot.coherent) {
      auto range = made<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
      range.memory = slot.shot.memory, range.offset = 0, range.size = VK_WHOLE_SIZE;
      vk.vkInvalidateMappedMemoryRanges(device, 1, &range);
    }
    auto pixels = (const u32*)slot.shot.mapped;
    shotPixels.assign(pixels, pixels + u64(slot.shotWidth) * slot.shotHeight);
    shotWidth = slot.shotWidth, shotHeight = slot.shotHeight, shotFormat = slot.shotFormat, shotNew = true;
  }
  auto waitAll() -> bool {
    for(u32 n = 1; n <= Slots; n++) {  //(oldest first)
      if(!wait(slots[(next + n - 1) % Slots])) return false;
    }
    bury();
    return true;
  }

  static auto compare(u8 ge) -> VkCompareOp {
    static constexpr VkCompareOp ops[8] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_EQUAL,
      VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_LESS, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER,
      VK_COMPARE_OP_GREATER_OR_EQUAL};
    return ops[ge & 7];
  }
  static auto factor(u8 f) -> VkBlendFactor {
    static constexpr VkBlendFactor factors[] = {VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE,
      VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_DST_COLOR,
      VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
      VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_SRC1_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
      VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA};
    return f < std::size(factors) ? factors[f] : VK_BLEND_FACTOR_ONE;
  }
  static auto stencilOperation(u8 o) -> VkStencilOp {
    static constexpr VkStencilOp ops[6] = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_REPLACE,
      VK_STENCIL_OP_INVERT, VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_DECREMENT_AND_CLAMP};
    return o < 6 ? ops[o] : VK_STENCIL_OP_KEEP;
  }

  //The pipeline for a key: made the first time it's met, and kept (VK_NULL_HANDLE where the driver wouldn't).
  auto pipelineFor(const GPU::Pipeline& k) -> VkPipeline {
    std::array<u8, sizeof(GPU::Pipeline)> key;
    std::memcpy(key.data(), &k, sizeof(k));
    if(auto found = pipelineCache_.find(key); found != pipelineCache_.end()) return found->second;
    if(k.transformed && !transformModule) return pipelineCache_[key] = VK_NULL_HANDLE;
    u32 constants[19] = {k.textured, k.function, k.alphaTest, k.colorTest, k.fog, k.depthRange, k.clear, k.source,
                         k.destination, k.alphaOut, k.dither, k.quantize, k.logic, k.clamp, k.texels, k.reads,
                         k.blending, k.term, k.flat};
    VkSpecializationMapEntry entries[19];
    for(u32 n = 0; n < 19; n++) entries[n] = {n, n * 4, 4};
    VkSpecializationInfo specialization{19, entries, sizeof(constants), constants};
    //(fast mode's 3D: transform.vert, taking the GE's vertices)
    VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
       k.transformed ? transformModule : vertexModule, "main", nullptr},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
       fragmentModule, "main", &specialization}};
    using V = GPU::Vertex;
    using M = GPU::Model;
    VkVertexInputBindingDescription binding{0, k.transformed ? u32(sizeof(M)) : u32(sizeof(V)),
                                            VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[8] = {
      {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, u32(offsetof(V, x))},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(V, u))},
      {2, 0, VK_FORMAT_R8G8B8A8_UNORM, u32(offsetof(V, color))},
      {3, 0, VK_FORMAT_R8G8B8A8_UNORM, u32(offsetof(V, specular))},
      {4, 0, VK_FORMAT_R32_SFLOAT, u32(offsetof(V, fog))},
      {5, 0, VK_FORMAT_R32_UINT, u32(offsetof(V, flags))},
      {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, u32(offsetof(V, columnFirst))},
      {7, 0, VK_FORMAT_R32G32_SINT, u32(offsetof(V, columnStart))}};
    VkVertexInputAttributeDescription models[7] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(M, x))},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, u32(offsetof(M, normal))},
      {2, 0, VK_FORMAT_R32G32_SFLOAT, u32(offsetof(M, u))},
      {3, 0, VK_FORMAT_R8G8B8A8_UNORM, u32(offsetof(M, color))},
      {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, u32(offsetof(M, weights))},
      {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, u32(offsetof(M, weights) + 16)},
      {6, 0, VK_FORMAT_R32_UINT, u32(offsetof(M, transform))}};
    auto input = made<VkPipelineVertexInputStateCreateInfo>(
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    input.vertexBindingDescriptionCount = 1, input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = k.transformed ? 7 : 8;
    input.pVertexAttributeDescriptions = k.transformed ? models : attributes;
    auto assembly = made<VkPipelineInputAssemblyStateCreateInfo>(
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto viewport = made<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport.viewportCount = 1, viewport.scissorCount = 1;
    auto raster = made<VkPipelineRasterizationStateCreateInfo>(
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    raster.depthClampEnable = depthClamp, raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE, raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, raster.lineWidth = 1;
    auto multisample = made<VkPipelineMultisampleStateCreateInfo>(
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    auto depth = made<VkPipelineDepthStencilStateCreateInfo>(
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    depth.depthTestEnable = k.depthTest, depth.depthWriteEnable = k.depthWrite;
    depth.depthCompareOp = compare(k.depthCompare);
    depth.stencilTestEnable = k.stencilTest;
    depth.front = {stencilOperation(k.stencilFail), stencilOperation(k.stencilPass),
                   stencilOperation(k.stencilDepthFail), compare(k.stencilCompare), 0xff, 0xff, 0};
    depth.back = depth.front;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = k.blend;
    attachment.srcColorBlendFactor = factor(k.sourceFactor), attachment.dstColorBlendFactor = factor(
      k.destinationFactor);
    static constexpr VkBlendOp operations[5] = {VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT,
      VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_MIN, VK_BLEND_OP_MAX};
    attachment.colorBlendOp = operations[k.operation < 5 ? k.operation : 0];
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = k.colorMask & 15;
    auto blend = made<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.logicOpEnable = k.logicOp, blend.logicOp = VkLogicOp(k.logicOperation & 15);
    //(shader blending's draws read the pixels the ones before wrote, in order, where the GPU can be asked to: every
    //pipeline of the subpass asks, so that the writes of draws that don't read are in that order too)
    if(orderedPass) {
      blend.flags = VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT;
    }
    blend.attachmentCount = 1, blend.pAttachments = &attachment;
    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
      VK_DYNAMIC_STATE_BLEND_CONSTANTS};
    auto dynamic = made<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = std::size(dynamics), dynamic.pDynamicStates = dynamics;
    auto info = made<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    info.stageCount = 2, info.pStages = stages;
    info.pVertexInputState = &input, info.pInputAssemblyState = &assembly, info.pViewportState = &viewport;
    info.pRasterizationState = &raster, info.pMultisampleState = &multisample, info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend, info.pDynamicState = &dynamic;
    info.layout = pipelineLayout, info.renderPass = renderPass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if(vk.vkCreateGraphicsPipelines(device, pipelineCache, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
      pipeline = VK_NULL_HANDLE;
    }
    pipelines++;
    pipelineCache_[key] = pipeline;
    return pipeline;
  }

  //copy.frag's pipeline for a MODE: the colors written as they are (0), the depth through gl_FragDepth (1), or the
  //stencil's bit the write mask has (2: replaced with the reference, 255, where the pixel's stencil has it). The
  //viewport is the rectangle copied into; nothing else is tested or blended.
  auto makeCopyPipeline(u32 mode) -> VkPipeline {
    VkSpecializationMapEntry entry{0, 0, 4};
    VkSpecializationInfo specialization{1, &entry, 4, &mode};
    VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
       copyVertexModule, "main", nullptr},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
       copyFragmentModule, "main", &specialization}};
    auto input = made<VkPipelineVertexInputStateCreateInfo>(
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    auto assembly = made<VkPipelineInputAssemblyStateCreateInfo>(
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto viewport = made<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport.viewportCount = 1, viewport.scissorCount = 1;
    auto raster = made<VkPipelineRasterizationStateCreateInfo>(
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    raster.polygonMode = VK_POLYGON_MODE_FILL, raster.cullMode = VK_CULL_MODE_NONE, raster.lineWidth = 1;
    auto multisample = made<VkPipelineMultisampleStateCreateInfo>(
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    auto depth = made<VkPipelineDepthStencilStateCreateInfo>(
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    depth.depthTestEnable = mode == 1, depth.depthWriteEnable = mode == 1;
    depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    depth.stencilTestEnable = mode == 2;
    depth.front = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, 0xff, 0xff,
                   0xff};
    depth.back = depth.front;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = mode == 0 ? 15 : 0;
    auto blend = made<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.attachmentCount = 1, blend.pAttachments = &attachment;
    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_STENCIL_WRITE_MASK};
    auto dynamic = made<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = std::size(dynamics), dynamic.pDynamicStates = dynamics;
    auto info = made<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    info.stageCount = 2, info.pStages = stages;
    info.pVertexInputState = &input, info.pInputAssemblyState = &assembly, info.pViewportState = &viewport;
    info.pRasterizationState = &raster, info.pMultisampleState = &multisample, info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend, info.pDynamicState = &dynamic;
    info.layout = copyLayout, info.renderPass = renderPass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if(vk.vkCreateGraphicsPipelines(device, pipelineCache, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
      return VK_NULL_HANDLE;
    }
    return pipeline;
  }

  //copy.frag's push constants
  struct CopyPush { s32 origin[2]; u32 scale, bit; };

  //A barrier for one image: what came before in from (any stage), done before what follows in to.
  auto transition(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                  VkImageLayout to) -> void {
    auto barrier = made<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.oldLayout = from, barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image, barrier.subresourceRange = {aspect, 0, 1, 0, 1};
    vk.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                            nullptr, 0, nullptr, 1, &barrier);
  }

  static auto aligned(u64 offset) -> u64 { return (offset + 15) & ~u64(15); }

  //A run: the recorded commands in a slot's command buffer, submitted; with wait, waited for, its read-backs then
  //readable (read()).
  auto run(const GPU::Recorded& r, bool waits) -> bool {
    using Clock = std::chrono::steady_clock;
    auto nanoseconds = [](Clock::time_point from) {
      return u64(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - from).count());
    };
    if(lost) return false;
    auto began = Clock::now();
    Slot& slot = slots[next];
    if(!wait(slot)) return false;
    //(two frames in flight at most: a run that shows one waits first for the run that showed the one before the
    //last, so that where the GPU is the bound (upscaled) the frames shown, and the player's input with them, aren't
    //up to eight runs late)
    for(auto& c : r.commands) {
      if(c.kind != GPU::Command::Kind::Present && c.kind != GPU::Command::Kind::Shot) continue;
      for(auto& other : slots) if(other.busy && other.serial == showedBefore && !wait(other)) return false;
      showedBefore = showedLast, showedLast = submitted + 1;  //(this run's, once submitted; one in no slot is done)
      break;
    }
    bury();
    //what goes in the staging buffer: the vertices, fast mode's models, indices and Transformed blocks, the uploads
    //(their depths as the depth format's 32 bits), the new textures' texels
    u64 modelsAt = aligned(r.vertices.size() * sizeof(GPU::Vertex));
    u64 indicesAt = aligned(modelsAt + r.models.size() * sizeof(GPU::Model));
    u64 transformsAt = (indicesAt + r.indices.size() * 4 + storageAlignment - 1) / storageAlignment * storageAlignment;
    u64 size = transformsAt + r.transforms.size() * sizeof(GPU::Transformed);
    for(auto& c : r.commands) {
      if(c.kind == GPU::Command::Kind::Upload) size = aligned(size) + aligned(u64(c.width) * c.height * 4) * 2 +
                                                      aligned(u64(c.width) * c.height);
      if(c.kind == GPU::Command::Kind::Present && !c.target) size = aligned(size) + u64(c.width) * c.height * 4;
    }
    for(auto& [id, t] : textureImages) if(!t.pending.empty()) size = aligned(size) + t.pending.size() * 4;
    size = aligned(size) + 16;
    if(slot.staging.size < size) {
      constexpr auto Usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      if(!make(slot.staging, std::max<u64>(size, slot.staging.size * 2), Usage, false)) return lost = true, false;
    }
    if(!r.transforms.empty()) {  //(the slot's run before is done: its set isn't in use)
      VkDescriptorBufferInfo blocks{slot.staging.buffer, transformsAt, r.transforms.size() * sizeof(GPU::Transformed)};
      auto write = made<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
      write.dstSet = slot.transforms, write.descriptorCount = 1;
      write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, write.pBufferInfo = &blocks;
      vk.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    u64 readbackSize = 0;  //(the colors at the size asked for; the stencils, at a higher resolution, a row in scale)
    for(auto& c : r.commands) {
      if(c.kind != GPU::Command::Kind::Readback) continue;
      u64 pixels = u64(c.width) * c.height;
      readbackSize = aligned(aligned(readbackSize + pixels * c.at * c.at * 4) + (c.at == 1 ? pixels * scale : 0));
    }
    if(readbackSize > readback.size) {
      if(!make(readback, std::max<u64>(readbackSize, readback.size * 2), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
        return lost = true, false;
      }
    }
    u8* staging = slot.staging.mapped;
    //(a run of only read-backs has no vertices, and an empty list's data() may be null, which memcpy mustn't
    //be given even for no bytes)
    if(!r.vertices.empty()) std::memcpy(staging, r.vertices.data(), r.vertices.size() * sizeof(GPU::Vertex));
    if(!r.models.empty()) std::memcpy(staging + modelsAt, r.models.data(), r.models.size() * sizeof(GPU::Model));
    if(!r.indices.empty()) std::memcpy(staging + indicesAt, r.indices.data(), r.indices.size() * 4);
    if(!r.transforms.empty()) {
      std::memcpy(staging + transformsAt, r.transforms.data(), r.transforms.size() * sizeof(GPU::Transformed));
    }
    u64 at = transformsAt + r.transforms.size() * sizeof(GPU::Transformed);

    //(a swapchain image for a Present, acquired before anything's recorded: none, nothing's presented)
    constexpr u32 NoImage = ~0u;
    u32 image = NoImage;
    presented = false;
    for(auto& c : r.commands) {
      if(c.kind == GPU::Command::Kind::Present && image == NoImage && !acquire(slot, image)) image = NoImage;
    }
    if(lost) return false;

    VkCommandBuffer commands = slot.commands;
    vk.vkResetCommandBuffer(commands, 0);
    auto begin = made<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk.vkBeginCommandBuffer(commands, &begin);
    constexpr auto DepthStencil = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    for(auto& [id, t] : targetImages) {
      if(!t.fresh) continue;
      transition(commands, t.color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, ColorLayout);
      transition(commands, t.depth.image, DepthStencil, VK_IMAGE_LAYOUT_UNDEFINED, DepthLayout);
      t.fresh = false;
    }
    for(auto& [id, t] : textureImages) {
      if(t.pending.empty()) continue;
      at = aligned(at);
      std::memcpy(staging + at, t.pending.data(), t.pending.size() * 4);
      transition(commands, t.image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      VkBufferImageCopy copy{at, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {t.width, t.height, 1}};
      vk.vkCmdCopyBufferToImage(commands, slot.staging.buffer, t.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                1, &copy);
      transition(commands, t.image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      at += t.pending.size() * 4;
      t.pending.clear(), t.pending.shrink_to_fit();
      t.fresh = false;
    }
    VkDeviceSize zero = 0;
    vk.vkCmdBindVertexBuffers(commands, 0, 1, &slot.staging.buffer, &zero);
    if(!r.indices.empty()) vk.vkCmdBindIndexBuffer(commands, slot.staging.buffer, indicesAt, VK_INDEX_TYPE_UINT32);
    bool modelsBound = false;      //(the vertex buffer: fast mode's models, or the vertices)
    bool transformsBound = false;  //(set 2)

    Target* pass = nullptr;  //the target whose render pass is open
    u32 lastState = ~0u;
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    VkDescriptorSet boundSet = VK_NULL_HANDLE;
    auto endPass = [&] {
      if(pass) vk.vkCmdEndRenderPass(commands), pass = nullptr;
    };
    readbacks.clear(), readbackSizes.clear(), squeezes.clear();
    u64 readAt = 0;
    bool presenting = false;
    for(auto& c : r.commands) {
      if(c.kind == GPU::Command::Kind::Present) {  //(the first alone, where an image was acquired)
        u64 from = 0;
        if(!c.target) {
          at = aligned(at), from = at;
          std::memcpy(staging + at, r.uploads.data() + c.colors, u64(c.width) * c.height * 4);
          at += u64(c.width) * c.height * 4;
        }
        if(image == NoImage || presenting) continue;
        endPass();
        auto shown = targetImages.find(c.target);
        present(commands, slot, c, c.target && shown != targetImages.end() ? &shown->second : nullptr, from, image);
        presenting = true;
        continue;
      }
      auto found = targetImages.find(c.target);
      if(found == targetImages.end()) continue;
      Target& t = found->second;
      if(c.kind == GPU::Command::Kind::Draw || c.kind == GPU::Command::Kind::Mesh) {
        const GPU::State& s = r.states[c.state];
        if(u32(s.scissor[1] + s.scissor[3]) > t.rows) {  //(past its pictures' rows, at a higher resolution)
          endPass();
          if(!fit(commands, t, s.scissor[1] + s.scissor[3])) return lost = true, false;
        }
        if(pass != &t) {
          endPass();
          beginPass(commands, t);
          VkViewport viewport{0, 0, f32(t.width * scale), f32(t.height * scale), 0, 1};
          vk.vkCmdSetViewport(commands, 0, 1, &viewport);
          pass = &t, lastState = ~0u;
          //(the sets bound again from 0 up, as a copy's layout may have been bound, which leaves this layout's sets
          //undefined, and binding a lower set after a higher one through another layout would too: set 0 the blank
          //texture's until a draw's own, set 1 the target's colors for shader blending, set 2 at the first mesh)
          VkDescriptorSet sets[2] = {textureImages.find(0)->second.set, t.frame};
          vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, sets, 0,
                                     nullptr);
          boundSet = sets[0], transformsBound = false;
        }
        if(c.state != lastState) {
          VkPipeline pipeline = pipelineFor(s.pipeline);
          if(!pipeline) continue;
          if(pipeline != boundPipeline) {
            vk.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline), boundPipeline = pipeline;
          }
          auto texture = textureImages.find(s.texture);
          if(texture == textureImages.end()) texture = textureImages.find(0);
          if(texture->second.set != boundSet) {
            boundSet = texture->second.set;
            vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &boundSet,
                                       0, nullptr);
          }
          VkRect2D scissor = scaled(t, s.scissor[0], s.scissor[1], s.scissor[2], s.scissor[3]);
          vk.vkCmdSetScissor(commands, 0, 1, &scissor);
          vk.vkCmdSetStencilReference(commands, VK_STENCIL_FACE_FRONT_AND_BACK, s.stencilReference);
          vk.vkCmdSetStencilCompareMask(commands, VK_STENCIL_FACE_FRONT_AND_BACK, s.stencilCompareMask);
          vk.vkCmdSetStencilWriteMask(commands, VK_STENCIL_FACE_FRONT_AND_BACK, s.stencilWriteMask);
          f32 constant[4];
          for(u32 n = 0; n < 4; n++) constant[n] = (s.blendConstant >> n * 8 & 0xff) / 255.0f;
          vk.vkCmdSetBlendConstants(commands, constant);
          vk.vkCmdPushConstants(commands, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                0, sizeof(GPU::Push), &s.push);
          lastState = c.state;
        }
        //(what the draws before wrote, made visible to its reading, where the GPU doesn't keep the order: in
        //rasterization order every pipeline of the subpass asks for it, which orders the draws before too)
        if(s.pipeline.reads && !(readsInOrder && orderedPass)) {
          auto barrier = made<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
          barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
          barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
          vk.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 1, &barrier, 0,
                                  nullptr, 0, nullptr);
        }
        bool mesh = c.kind == GPU::Command::Kind::Mesh;
        if(mesh != modelsBound) {
          VkDeviceSize offset = mesh ? modelsAt : 0;
          vk.vkCmdBindVertexBuffers(commands, 0, 1, &slot.staging.buffer, &offset);
          modelsBound = mesh;
        }
        if(!mesh) {
          vk.vkCmdDraw(commands, c.count, 1, c.first, 0);
          continue;
        }
        if(!transformsBound) {
          vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 2, 1,
                                     &slot.transforms, 0, nullptr);
          transformsBound = true;
        }
        vk.vkCmdDrawIndexed(commands, c.count, 1, c.first, 0, 0);
        continue;
      }
      endPass();
      if(c.kind == GPU::Command::Kind::Shot) {  //(the slot's shot, kept once its run is done: shot())
        shoot(commands, slot, t, c);
        continue;
      }
      if(!fit(commands, t, c.y + c.height)) return lost = true, false;
      u32 count = c.width * c.height;
      VkOffset3D offset{c.x * s32(scale), c.y * s32(scale), 0};
      VkExtent3D extent{c.width * scale, c.height * scale, 1};
      constexpr auto Source = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      constexpr auto Destination = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      if(c.kind == GPU::Command::Kind::Upload) {
        u64 colors = aligned(at), stencils = aligned(colors + count * 4), depths = aligned(stencils + count);
        at = depths + count * 4;
        const u8* in = r.uploads.data();
        bool colored = c.parts & 1, depthed = c.parts & 2;
        if(scale > 1) {
          upload(commands, slot, t, c, in, colors, depths);
          if(lost) return false;  //(no room for its picture: nothing half recorded is submitted)
          boundPipeline = VK_NULL_HANDLE, boundSet = VK_NULL_HANDLE, lastState = ~0u, transformsBound = false;
          continue;
        }
        std::memcpy(staging + colors, in + c.colors, count * 4);
        std::memcpy(staging + stencils, in + c.stencil, count);
        u32* depth = (u32*)(staging + depths);
        for(u32 n = 0; n < count; n++) {
          u16 z;
          std::memcpy(&z, in + c.depth + n * 2, 2);
          if(depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            f32 value = z / 65535.0f;
            std::memcpy(&depth[n], &value, 4);
          } else {
            depth[n] = u32((u64(z) * 0xff'ffff + 32767) / 65535);
          }
        }
        transition(commands, t.color.image, VK_IMAGE_ASPECT_COLOR_BIT, ColorLayout, Destination);
        transition(commands, t.depth.image, DepthStencil, DepthLayout, Destination);
        VkBufferImageCopy copies[3] = {
          {colors, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, offset, extent},
          {stencils, 0, 0, {VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1}, offset, extent},
          {depths, 0, 0, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1}, offset, extent}};
        //(the colors and the stencil, the depth, or both: the stencil is the depth image's)
        if(colored) {
          vk.vkCmdCopyBufferToImage(commands, slot.staging.buffer, t.color.image, Destination, 1, &copies[0]);
        }
        vk.vkCmdCopyBufferToImage(commands, slot.staging.buffer, t.depth.image, Destination,
                                  colored && depthed ? 2 : 1, &copies[colored ? 1 : 2]);
        transition(commands, t.color.image, VK_IMAGE_ASPECT_COLOR_BIT, Destination, ColorLayout);
        transition(commands, t.depth.image, DepthStencil, Destination, DepthLayout);
      } else if(c.kind == GPU::Command::Kind::Copy) {  //(a texture of the target's scale: GPU::textureFor())
        auto found = textureImages.find(c.texture);
        if(found == textureImages.end()) continue;
        Texture& texture = found->second;
        VkImageLayout was = texture.fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        texture.fresh = false;
        transition(commands, t.color.image, VK_IMAGE_ASPECT_COLOR_BIT, ColorLayout, Source);
        transition(commands, texture.image.image, VK_IMAGE_ASPECT_COLOR_BIT, was, Destination);
        VkImageCopy region{{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, offset, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                           {0, 0, 0}, extent};
        vk.vkCmdCopyImage(commands, t.color.image, Source, texture.image.image, Destination, 1, &region);
        transition(commands, t.color.image, VK_IMAGE_ASPECT_COLOR_BIT, Source, ColorLayout);
        transition(commands, texture.image.image, VK_IMAGE_ASPECT_COLOR_BIT, Destination,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      } else {
        u32 times = std::clamp<u32>(c.at, 1, scale);
        u64 colors = readAt, stencils = aligned(colors + u64(count) * times * times * 4);
        readAt = aligned(stencils + (times == 1 ? u64(count) * scale : 0));
        readbacks.push_back({colors, stencils});
        transition(commands, t.color.image, VK_IMAGE_ASPECT_COLOR_BIT, ColorLayout, Source);
        if(times == scale) {  //(the target's own pixels: the PSP's at 1, the screen's at the target's resolution)
          VkBufferImageCopy color{colors, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, offset, extent};
          vk.vkCmdCopyImageToBuffer(commands, t.color.image, Source, readback.buffer, 1, &color);
        } else {  //(shrunk first: for memory one of each pixel's, for the screen blended)
          if(!shrink(c.width * times, c.height * times)) return lost = true, false;
          transition(commands, shrunk.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, Destination);
          VkImageBlit blit{{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                           {offset, {offset.x + s32(extent.width), offset.y + s32(extent.height), 1}},
                           {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                           {{0, 0, 0}, {s32(c.width * times), s32(c.height * times), 1}}};
          vk.vkCmdBlitImage(commands, t.color.image, Source, shrunk.image, Destination, 1, &blit,
                            times == 1 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
          transition(commands, shrunk.image, VK_IMAGE_ASPECT_COLOR_BIT, Destination, Source);
          VkBufferImageCopy color{colors, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {},
                                  {c.width * times, c.height * times, 1}};
          vk.vkCmdCopyImageToBuffer(commands, shrunk.image, Source, readback.buffer, 1, &color);
        }
        transition(commands, t.color.image, VK_IMAGE_ASPECT_COLOR_BIT, Source, ColorLayout);
        if(times == 1) {  //(the stencil for memory: at a higher resolution, the row of each pixel's the blit took)
          transition(commands, t.depth.image, DepthStencil, DepthLayout, Source);
          if(scale == 1) {
            VkBufferImageCopy stencil{stencils, 0, 0, {VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1}, offset, extent};
            vk.vkCmdCopyImageToBuffer(commands, t.depth.image, Source, readback.buffer, 1, &stencil);
          } else {
            rows.resize(c.height);
            for(u32 y = 0; y < c.height; y++) {
              rows[y] = {stencils + u64(y) * c.width * scale, 0, 0, {VK_IMAGE_ASPECT_STENCIL_BIT, 0, 0, 1},
                         {offset.x, s32((c.y + y) * scale + scale / 2), 0}, {extent.width, 1, 1}};
            }
            vk.vkCmdCopyImageToBuffer(commands, t.depth.image, Source, readback.buffer, rows.size(), rows.data());
            squeezes.push_back({u32(readbacks.size() - 1), c.width, c.height});
          }
          transition(commands, t.depth.image, DepthStencil, Source, DepthLayout);
        }
      }
    }
    endPass();
    if(image != NoImage && !presenting) {  //(acquired, so presented: black, where the Present couldn't be drawn)
      GPU::Command none{GPU::Command::Kind::Present, 0};
      present(commands, slot, none, nullptr, 0, image);
    }
    if(!readbacks.empty() || slot.shooting) {  //(what was copied, for the host)
      auto memory = made<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
      memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, memory.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
      vk.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &memory,
                              0, nullptr, 0, nullptr);
    }
    vk.vkEndCommandBuffer(commands);
    recording += nanoseconds(began);
    began = Clock::now();
    auto submitInfo = made<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submitInfo.commandBufferCount = 1, submitInfo.pCommandBuffers = &commands;
    VkPipelineStageFlags drawn = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if(image != NoImage) {  //(the image's drawing waits for its acquiring, its present for the drawing)
      submitInfo.waitSemaphoreCount = 1, submitInfo.pWaitSemaphores = &slot.acquired;
      submitInfo.pWaitDstStageMask = &drawn;
      submitInfo.signalSemaphoreCount = 1, submitInfo.pSignalSemaphores = &swapImages[image].rendered;
    }
    if(vk.vkQueueSubmit(queue, 1, &submitInfo, slot.fence) != VK_SUCCESS) return lost = true, false;
    submitting += nanoseconds(began);
    slot.busy = true, slot.serial = ++submitted;
    next = (next + 1) % Slots;
    if(image != NoImage) {
      auto info = made<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
      info.waitSemaphoreCount = 1, info.pWaitSemaphores = &swapImages[image].rendered;
      info.swapchainCount = 1, info.pSwapchains = &swapchain, info.pImageIndices = &image;
      VkResult shown = vk.vkQueuePresentKHR(queue, &info);
      presented = shown == VK_SUCCESS || shown == VK_SUBOPTIMAL_KHR;
      if(shown == VK_SUBOPTIMAL_KHR || shown == VK_ERROR_OUT_OF_DATE_KHR) rebuild = true;
      else if(shown == VK_ERROR_SURFACE_LOST_KHR) dropSurface();
      else if(shown == VK_ERROR_DEVICE_LOST) return lost = true, false;
    }
    if(!waits) return true;
    began = Clock::now();
    if(!waitAll()) return false;
    waiting += nanoseconds(began);
    if(!readback.coherent && readAt) {
      auto range = made<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
      range.memory = readback.memory, range.offset = 0, range.size = VK_WHOLE_SIZE;
      vk.vkInvalidateMappedMemoryRanges(device, 1, &range);
    }
    //(each stencil row of scale x the width: the byte of each pixel's the blit took, put where the PSP's goes)
    for(auto& squeeze : squeezes) {
      u8* stencil = readback.mapped + readbacks[squeeze.readback].second;
      for(u32 n = 0, y = 0; y < squeeze.height; y++) {
        const u8* row = stencil + u64(y) * squeeze.width * scale + scale / 2;
        for(u32 x = 0; x < squeeze.width; x++) stencil[n++] = row[x * scale];
      }
    }
    return true;
  }

  //A target's render pass begun, over the whole of its pictures
  auto beginPass(VkCommandBuffer commands, Target& t) -> void {
    auto info = made<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    info.renderPass = renderPass, info.framebuffer = t.framebuffer;
    info.renderArea = {{0, 0}, {t.width * scale, t.rows * scale}};
    vk.vkCmdBeginRenderPass(commands, &info, VK_SUBPASS_CONTENTS_INLINE);
    passes++;
  }

  //A rectangle of the PSP's pixels as the target's, inside its pictures
  auto scaled(const Target& t, s32 x, s32 y, s32 width, s32 height) const -> VkRect2D {
    s32 right = std::min<s32>((x + width) * scale, t.width * scale);
    s32 bottom = std::min<s32>((y + height) * scale, t.rows * scale);
    x = std::max(x, 0) * scale, y = std::max(y, 0) * scale;
    return {{x, y}, {u32(std::max(right - x, 0)), u32(std::max(bottom - y, 0))}};
  }

  //Memory's pixels put into a target at a higher resolution (copy.frag): the colors (the stencil their alpha) and
  //the depths (two bytes of each, the low one first) copied into pictures of the PSP's size, then drawn into the
  //rectangle each of them covers: the colors as they are, the stencil cleared and then set a bit at a time (eight
  //draws, each writing one bit where the pixel's stencil has it), the depth through gl_FragDepth.
  auto upload(VkCommandBuffer commands, Slot& slot, Target& t, const GPU::Command& c, const u8* in, u64 colors,
              u64 depths) -> void {
    u32 count = c.width * c.height;
    bool colored = c.parts & 1, depthed = c.parts & 2;
    u8* staging = slot.staging.mapped;
    for(u32 n = 0; n < count && colored; n++) {
      u32 color;
      std::memcpy(&color, in + c.colors + n * 4, 4);
      color = (color & 0xff'ffff) | u32(in[c.stencil + n]) << 24;
      std::memcpy(staging + colors + n * 4, &color, 4);
    }
    for(u32 n = 0; n < count && depthed; n++) {
      u16 z;
      std::memcpy(&z, in + c.depth + n * 2, 2);
      u32 bytes = z;
      std::memcpy(staging + depths + n * 4, &bytes, 4);
    }
    constexpr auto Destination = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    constexpr auto Read = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkRect2D area = scaled(t, c.x, c.y, c.width, c.height);
    if(!area.extent.width || !area.extent.height) return;
    using Part = std::tuple<bool, Staged*, u64>;
    for(auto [part, staged, from] : {Part{colored, &stagedColors, colors}, Part{depthed, &stagedDepths, depths}}) {
      if(!part) continue;
      if(!stage(*staged, c.width, c.height)) return void(lost = true);
      transition(commands, staged->image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, Destination);
      VkBufferImageCopy copy{from, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {}, {c.width, c.height, 1}};
      vk.vkCmdCopyBufferToImage(commands, slot.staging.buffer, staged->image.image, Destination, 1, &copy);
      transition(commands, staged->image.image, VK_IMAGE_ASPECT_COLOR_BIT, Destination, Read);
    }
    beginPass(commands, t);
    VkViewport viewport{f32(c.x * scale), f32(c.y * scale), f32(c.width * scale), f32(c.height * scale), 0, 1};
    vk.vkCmdSetViewport(commands, 0, 1, &viewport);
    vk.vkCmdSetScissor(commands, 0, 1, &area);
    CopyPush push{{c.x * s32(scale), c.y * s32(scale)}, scale, 0};
    auto draw = [&](u32 mode, const Staged& staged) {
      vk.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, copyPipelines[mode]);
      vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, copyLayout, 0, 1, &staged.set, 0,
                                 nullptr);
      vk.vkCmdPushConstants(commands, copyLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
      vk.vkCmdDraw(commands, 3, 1, 0, 0);
    };
    if(colored) {
      draw(0, stagedColors);
      VkClearAttachment clear{VK_IMAGE_ASPECT_STENCIL_BIT, 0, {}};
      VkClearRect rect{area, 0, 1};
      vk.vkCmdClearAttachments(commands, 1, &clear, 1, &rect);
      for(u32 bit = 0; bit < 8; bit++) {
        push.bit = 1 << bit;
        vk.vkCmdSetStencilWriteMask(commands, VK_STENCIL_FACE_FRONT_AND_BACK, push.bit);
        draw(2, stagedColors);
      }
    }
    if(depthed) draw(1, stagedDepths);
    vk.vkCmdEndRenderPass(commands);
  }

  //Presenting: a new window (none: let go of) dropped the surface of the one before, which disconnects from it so
  //that the host can draw on it; the same one again may have changed size.
  auto window(void* next) -> void override {
    if(next == shownOn) return void(rebuild = rebuild || swapchain);
    dropSurface();
    shownOn = next, failed = false;
  }
  auto presents() const -> bool override { return canPresent && !failed && !lost; }
  auto shot(std::vector<u32>& pixels, u32& width, u32& height, u32& format) -> bool override {
    if(!shotNew) return false;
    std::swap(pixels, shotPixels);
    width = shotWidth, height = shotHeight, format = shotFormat, shotNew = false;
    return true;
  }

  //The swapchain's images and the swapchain let go of, once the GPU is done with them (where a run never ended,
  //they're left: the GPU may still use them)
  auto dropSwapchain() -> void {
    if(!swapchain && swapImages.empty()) return;
    if(!timedOut) {
      waitAll();
      vk.vkDeviceWaitIdle(device);  //(presents too)
      for(auto& image : swapImages) {
        if(image.framebuffer) vk.vkDestroyFramebuffer(device, image.framebuffer, nullptr);
        if(image.view) vk.vkDestroyImageView(device, image.view, nullptr);
        if(image.rendered) vk.vkDestroySemaphore(device, image.rendered, nullptr);
      }
      if(swapchain) vk.vkDestroySwapchainKHR(device, swapchain, nullptr);
    }
    swapImages.clear(), swapchain = VK_NULL_HANDLE, rebuild = false;
  }
  auto dropSurface() -> void {
    dropSwapchain();
    if(surface && !timedOut) vk.vkDestroySurfaceKHR(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
  }
  auto dropPresentPass() -> void {
    for(auto& pipeline : presentPipelines) {
      if(pipeline) vk.vkDestroyPipeline(device, pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
    }
    if(presentPass) vk.vkDestroyRenderPass(device, presentPass, nullptr);
    presentPass = VK_NULL_HANDLE, presentFormat = VK_FORMAT_UNDEFINED;
  }

  //The render pass that draws a swapchain image whole (cleared first: black where nothing's drawn) for presenting,
  //and present.frag's pipelines for it, one for each FORMAT; made again for another format.
  auto makePresentPass(VkFormat format) -> bool {
    if(presentPass && presentFormat == format) return true;
    dropPresentPass();
    VkAttachmentDescription attachment{0, format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
      VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1, subpass.pColorAttachments = &color;
    //(the image's acquiring waited for, where the semaphore's wait is: its color output)
    VkSubpassDependency dependency{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0};
    auto passInfo = made<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    passInfo.attachmentCount = 1, passInfo.pAttachments = &attachment;
    passInfo.subpassCount = 1, passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 1, passInfo.pDependencies = &dependency;
    if(vk.vkCreateRenderPass(device, &passInfo, nullptr, &presentPass) != VK_SUCCESS) {
      return presentPass = VK_NULL_HANDLE, false;
    }
    presentFormat = format;
    for(u32 n = 0; n < 4; n++) {
      VkSpecializationMapEntry entry{0, 0, 4};
      VkSpecializationInfo specialization{1, &entry, 4, &n};
      VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
         copyVertexModule, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
         presentModule, "main", &specialization}};
      auto input = made<VkPipelineVertexInputStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
      auto assembly = made<VkPipelineInputAssemblyStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
      assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      auto viewport = made<VkPipelineViewportStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
      viewport.viewportCount = 1, viewport.scissorCount = 1;
      auto raster = made<VkPipelineRasterizationStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
      raster.polygonMode = VK_POLYGON_MODE_FILL, raster.cullMode = VK_CULL_MODE_NONE, raster.lineWidth = 1;
      auto multisample = made<VkPipelineMultisampleStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
      multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
      VkPipelineColorBlendAttachmentState blended{};
      blended.colorWriteMask = 15;
      auto blend = made<VkPipelineColorBlendStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
      blend.attachmentCount = 1, blend.pAttachments = &blended;
      VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
      auto dynamic = made<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
      dynamic.dynamicStateCount = std::size(dynamics), dynamic.pDynamicStates = dynamics;
      auto info = made<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
      info.stageCount = 2, info.pStages = stages;
      info.pVertexInputState = &input, info.pInputAssemblyState = &assembly, info.pViewportState = &viewport;
      info.pRasterizationState = &raster, info.pMultisampleState = &multisample, info.pColorBlendState = &blend;
      info.pDynamicState = &dynamic, info.layout = presentLayout, info.renderPass = presentPass;
      if(vk.vkCreateGraphicsPipelines(device, pipelineCache, 1, &info, nullptr, &presentPipelines[n]) !=
         VK_SUCCESS) {
        presentPipelines[n] = VK_NULL_HANDLE;
        return dropPresentPass(), false;
      }
    }
    return true;
  }

  //The window's surface (Android's alone: elsewhere the host's own renderer has the window)
  auto makeSurface() -> bool {
    #if defined(__ANDROID__)
    auto info = made<VkAndroidSurfaceCreateInfoKHR>(VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR);
    info.window = (ANativeWindow*)shownOn;
    if(vk.vkCreateAndroidSurfaceKHR(instance, &info, nullptr, &surface) != VK_SUCCESS) {
      return surface = VK_NULL_HANDLE, false;
    }
    VkBool32 supported = VK_FALSE;
    vk.vkGetPhysicalDeviceSurfaceSupportKHR(physical, family, surface, &supported);
    return supported;
    #else
    return false;
    #endif
  }

  //The swapchain, as the window is now: 1 made; 0 not now (the window has no size: tried again at the next
  //present); -1 it can't be (another has the window, say: the host's drawing).
  auto makeSwapchain() -> s32 {
    VkSurfaceCapabilitiesKHR capabilities;
    if(vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities) != VK_SUCCESS) return -1;
    VkExtent2D extent = capabilities.currentExtent;
    #if defined(__ANDROID__)
    if(extent.width == 0xffff'ffff) {
      extent = {u32(ANativeWindow_getWidth((ANativeWindow*)shownOn)),
                u32(ANativeWindow_getHeight((ANativeWindow*)shownOn))};
    }
    #endif
    if(!extent.width || !extent.height || extent.width == 0xffff'ffff) return 0;
    u32 count = 0;
    vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());
    if(formats.empty()) return -1;
    VkSurfaceFormatKHR format = formats[0];  //(8888 unorm preferred: the colors as they are, no sRGB curve)
    for(auto& f : formats) {
      if(f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) { format = f; break; }
    }
    vk.vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vk.vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data());
    //(mailbox, the newest frame shown at the next refresh without waiting for one, where there is it; else FIFO,
    //which every surface has: the emulation keeps its own time, so a wait here is a refresh's at most)
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for(auto m : modes) if(m == VK_PRESENT_MODE_MAILBOX_KHR) mode = m;
    u32 images = std::max(capabilities.minImageCount, 3u);
    if(capabilities.maxImageCount) images = std::min(images, capabilities.maxImageCount);
    auto alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if(!(capabilities.supportedCompositeAlpha & alpha)) {
      for(u32 bit = 1; bit <= 8; bit <<= 1) {
        if(capabilities.supportedCompositeAlpha & bit) { alpha = VkCompositeAlphaFlagBitsKHR(bit); break; }
      }
    }
    auto info = made<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    info.surface = surface, info.minImageCount = images;
    info.imageFormat = format.format, info.imageColorSpace = format.colorSpace, info.imageExtent = extent;
    info.imageArrayLayers = 1, info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    //(the picture as the window is, the compositor turning it where the screen is turned)
    info.preTransform = capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                      ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;
    info.compositeAlpha = alpha, info.presentMode = mode, info.clipped = VK_TRUE;
    if(vk.vkCreateSwapchainKHR(device, &info, nullptr, &swapchain) != VK_SUCCESS) {
      return swapchain = VK_NULL_HANDLE, -1;
    }
    swapExtent = extent;
    if(!makePresentPass(format.format)) return -1;
    vk.vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
    std::vector<VkImage> chained(count);
    vk.vkGetSwapchainImagesKHR(device, swapchain, &count, chained.data());
    for(auto image : chained) {
      Swapped& swapped = swapImages.emplace_back();
      swapped.image = image;
      auto view = made<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
      view.image = image, view.viewType = VK_IMAGE_VIEW_TYPE_2D, view.format = format.format;
      view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      if(vk.vkCreateImageView(device, &view, nullptr, &swapped.view) != VK_SUCCESS) return -1;
      auto framebuffer = made<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
      framebuffer.renderPass = presentPass, framebuffer.attachmentCount = 1, framebuffer.pAttachments = &swapped.view;
      framebuffer.width = extent.width, framebuffer.height = extent.height, framebuffer.layers = 1;
      if(vk.vkCreateFramebuffer(device, &framebuffer, nullptr, &swapped.framebuffer) != VK_SUCCESS) return -1;
      auto semaphore = made<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
      if(vk.vkCreateSemaphore(device, &semaphore, nullptr, &swapped.rendered) != VK_SUCCESS) return -1;
    }
    return 1;
  }

  //A swapchain image to present on, acquired for the slot's run (its semaphore signalled as it's ready): false
  //where there's none (no window, no size yet, a tenth of a second without one) or presenting has been given up.
  auto acquire(Slot& slot, u32& image) -> bool {
    if(!presents() || !shownOn) return false;
    for(u32 attempt = 0; attempt < 2; attempt++) {
      if(rebuild) dropSwapchain();
      if(!surface && !makeSurface()) return giveUp();
      if(!swapchain) {
        s32 outcome = makeSwapchain();
        if(outcome < 0) return giveUp();
        if(outcome == 0) return dropSwapchain(), false;
      }
      VkResult result = vk.vkAcquireNextImageKHR(device, swapchain, AcquireTimeout, slot.acquired, VK_NULL_HANDLE,
                                                 &image);
      if(result == VK_SUCCESS) return true;
      if(result == VK_SUBOPTIMAL_KHR) return rebuild = true, true;  //(acquired: shown, then made again)
      if(result == VK_ERROR_OUT_OF_DATE_KHR) { rebuild = true; continue; }
      if(result == VK_ERROR_SURFACE_LOST_KHR) return dropSurface(), false;  //(made again at the next present)
      if(result == VK_ERROR_DEVICE_LOST) lost = true;
      return false;  //(a timeout: not shown this time)
    }
    return false;
  }
  //(presenting given up for this window, its surface dropped so that the host can draw on it)
  auto giveUp() -> bool {
    dropSurface();
    failed = true;
    return false;
  }

  //A Present recorded into the acquired swapchain image (present.frag over the whole of it): from the target, the
  //PSP's width x height of it at the scale, a shot of it taken at the PSP's size too (as a read-back for memory
  //takes it); or from memory's picture, put in shownPicture from the staging buffer (at from).
  auto present(VkCommandBuffer commands, Slot& slot, const GPU::Command& c, Target* t, u64 from, u32 image) -> void {
    constexpr auto Read = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    constexpr auto Destination = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VkDescriptorSet set = VK_NULL_HANDLE;
    f32 source[2] = {f32(c.width), f32(c.height)};
    if(t) {
      if(!t->set) {
        auto setInfo = made<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        setInfo.descriptorPool = descriptorPool, setInfo.descriptorSetCount = 1, setInfo.pSetLayouts = &setLayout;
        if(vk.vkAllocateDescriptorSets(device, &setInfo, &t->set) != VK_SUCCESS) t->set = VK_NULL_HANDLE;
        if(t->set) {
          VkDescriptorImageInfo picture{sampler, t->color.view, Read};
          auto write = made<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
          write.dstSet = t->set, write.descriptorCount = 1;
          write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, write.pImageInfo = &picture;
          vk.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
      }
      shoot(commands, slot, *t, c);
      transition(commands, t->color.image, VK_IMAGE_ASPECT_COLOR_BIT, ColorLayout, Read);
      set = t->set, source[0] *= scale, source[1] *= scale;
    } else if(!c.target && c.width && c.height && stage(shownPicture, c.width, c.height)) {
      transition(commands, shownPicture.image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 Destination);
      VkBufferImageCopy copy{from, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {}, {c.width, c.height, 1}};
      vk.vkCmdCopyBufferToImage(commands, slot.staging.buffer, shownPicture.image.image, Destination, 1, &copy);
      transition(commands, shownPicture.image.image, VK_IMAGE_ASPECT_COLOR_BIT, Destination, Read);
      set = shownPicture.set;
    }
    auto info = made<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    VkClearValue black{};
    info.renderPass = presentPass, info.framebuffer = swapImages[image].framebuffer;
    info.renderArea = {{0, 0}, swapExtent}, info.clearValueCount = 1, info.pClearValues = &black;
    vk.vkCmdBeginRenderPass(commands, &info, VK_SUBPASS_CONTENTS_INLINE);
    if(set) {
      VkViewport viewport{0, 0, f32(swapExtent.width), f32(swapExtent.height), 0, 1};
      VkRect2D scissor{{0, 0}, swapExtent};
      vk.vkCmdSetViewport(commands, 0, 1, &viewport);
      vk.vkCmdSetScissor(commands, 0, 1, &scissor);
      vk.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, presentPipelines[t ? c.format & 3 : 3]);
      vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, presentLayout, 0, 1, &set, 0, nullptr);
      f32 push[4] = {source[0], source[1], f32(swapExtent.width), f32(swapExtent.height)};
      vk.vkCmdPushConstants(commands, presentLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
      vk.vkCmdDraw(commands, 3, 1, 0, 0);
    }
    vk.vkCmdEndRenderPass(commands);
    if(t) transition(commands, t->color.image, VK_IMAGE_ASPECT_COLOR_BIT, Read, ColorLayout);
  }

  //A shot of the target's picture as presented, at the PSP's size (one of each pixel's scale x scale, as a
  //read-back for memory takes it), into the slot's shot buffer, kept once the run is done (keep()).
  auto shoot(VkCommandBuffer commands, Slot& slot, Target& t, const GPU::Command& c) -> void {
    u64 bytes = u64(c.width) * c.height * 4;
    if(slot.shot.size < bytes && !make(slot.shot, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) return;
    constexpr auto Source = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Destination = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VkBufferImageCopy copy{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {}, {c.width, c.height, 1}};
    transition(commands, t.color.image, VK_IMAGE_ASPECT_COLOR_BIT, ColorLayout, Source);
    if(scale == 1) {
      vk.vkCmdCopyImageToBuffer(commands, t.color.image, Source, slot.shot.buffer, 1, &copy);
    } else if(shrink(c.width, c.height)) {
      transition(commands, shrunk.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, Destination);
      VkImageBlit blit{{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                       {{0, 0, 0}, {s32(c.width * scale), s32(c.height * scale), 1}},
                       {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {s32(c.width), s32(c.height), 1}}};
      vk.vkCmdBlitImage(commands, t.color.image, Source, shrunk.image, Destination, 1, &blit, VK_FILTER_NEAREST);
      transition(commands, shrunk.image, VK_IMAGE_ASPECT_COLOR_BIT, Destination, Source);
      vk.vkCmdCopyImageToBuffer(commands, shrunk.image, Source, slot.shot.buffer, 1, &copy);
    } else {
      return transition(commands, t.color.image, VK_IMAGE_ASPECT_COLOR_BIT, Source, ColorLayout);
    }
    transition(commands, t.color.image, VK_IMAGE_ASPECT_COLOR_BIT, Source, ColorLayout);
    slot.shooting = true, slot.shotWidth = c.width, slot.shotHeight = c.height, slot.shotFormat = c.format;
  }

  auto drawable(const GPU::Pipeline& k) -> bool override { return !lost && pipelineFor(k); }
  auto submit(const GPU::Recorded& recorded) -> bool override { return run(recorded, false); }
  auto finish(const GPU::Recorded& recorded) -> bool override {
    if(recorded.empty()) return !lost && waitAll();
    return run(recorded, true);
  }
  auto read(u32 n, const u32*& colors, const u8*& stencil) -> bool override {
    if(n >= readbacks.size()) return false;
    colors = (const u32*)(readback.mapped + readbacks[n].first);
    stencil = readback.mapped + readbacks[n].second;
    return true;
  }

  //(fast: Vulkan (fast), which blends with the GPU's own units throughout: nothing asks for rasterization order)
  auto create(PFN_vkGetInstanceProcAddr getInstanceProcAddr, std::string& error, bool fast) -> bool {
    if(!getInstanceProcAddr) {  //(none from the host: the system's loader)
      #if !defined(_WIN32)  //(Windows' host always hands its own over)
      #if defined(__APPLE__)
      const char* names[] = {"libvulkan.1.dylib", "libvulkan.dylib", "libMoltenVK.dylib"};
      #elif defined(__ANDROID__)
      const char* names[] = {"libvulkan.so"};
      #else
      const char* names[] = {"libvulkan.so.1", "libvulkan.so"};
      #endif
      for(auto name : names) if(!library) library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
      if(library) getInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
      #endif
      if(!getInstanceProcAddr) return error = "no Vulkan loader", false;
    }
    vk.vkGetInstanceProcAddr = getInstanceProcAddr;
    vk.vkCreateInstance = (PFN_vkCreateInstance)getInstanceProcAddr(nullptr, "vkCreateInstance");
    vk.vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)getInstanceProcAddr(
      nullptr, "vkEnumerateInstanceExtensionProperties");
    if(!vk.vkCreateInstance || !vk.vkEnumerateInstanceExtensionProperties) return error = "no Vulkan loader", false;

    //MoltenVK (Vulkan on Apple's Metal) is a "portability" implementation, listed only to programs that ask
    u32 count = 0;
    vk.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vk.vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    bool portability = false, surfaces = false, androidSurfaces = false;
    for(auto& extension : extensions) {
      portability |= !std::strcmp(extension.extensionName, "VK_KHR_portability_enumeration");
      surfaces |= !std::strcmp(extension.extensionName, "VK_KHR_surface");
      androidSurfaces |= !std::strcmp(extension.extensionName, "VK_KHR_android_surface");
    }
    //(and on Android, a window's surface, to present on: Presenting)
    std::vector<const char*> instanceExtensions;
    if(portability) instanceExtensions.push_back("VK_KHR_portability_enumeration");
    #if defined(__ANDROID__)
    surfaces = surfaces && androidSurfaces;
    if(surfaces) {
      instanceExtensions.push_back("VK_KHR_surface");
      instanceExtensions.push_back("VK_KHR_android_surface");
    }
    #else
    surfaces = androidSurfaces = false;  //(the host's own renderer has the window)
    #endif
    auto application = made<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    application.pApplicationName = "Phobos PSP GPU renderer";
    //(1.1 where the loader has it, for vkGetPhysicalDeviceFeatures2 and Properties2: shader blending's rasterization
    //order, and the driver's ID)
    auto instanceVersion = (PFN_vkEnumerateInstanceVersion)getInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion");
    u32 version = VK_API_VERSION_1_0;
    if(instanceVersion && instanceVersion(&version) != VK_SUCCESS) version = VK_API_VERSION_1_0;
    application.apiVersion = version >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
    auto instanceInfo = made<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    instanceInfo.pApplicationInfo = &application;
    if(portability) instanceInfo.flags = 0x00000001;  //VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
    instanceInfo.enabledExtensionCount = instanceExtensions.size();
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
    if(vk.vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
      return error = "no Vulkan instance", false;
    }
    #define F(name) vk.name = (PFN_##name)getInstanceProcAddr(instance, #name); \
      if(!vk.name) return error = "the Vulkan driver lacks " #name, false;
    PSP_VULKAN_INSTANCE(F)
    #undef F
    if(surfaces) {
      #define F(name) vk.name = (PFN_##name)getInstanceProcAddr(instance, #name); surfaces = surfaces && vk.name;
      PSP_VULKAN_SURFACE(F)
      #if defined(__ANDROID__)
      F(vkCreateAndroidSurfaceKHR)
      #endif
      #undef F
    }

    //The first GPU with a graphics queue; a GPU that's really the CPU (lavapipe, SwiftShader) only when asked for
    //(PSP_GPU_ON_CPU), as it's no faster than the software renderer.
    vk.vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vk.vkEnumeratePhysicalDevices(instance, &count, devices.data());
    bool onCPU = std::getenv("PSP_GPU_ON_CPU") && *std::getenv("PSP_GPU_ON_CPU");
    for(auto candidate : devices) {
      VkPhysicalDeviceProperties properties;
      vk.vkGetPhysicalDeviceProperties(candidate, &properties);
      if(properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && !onCPU) continue;
      vk.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
      std::vector<VkQueueFamilyProperties> families(count);
      vk.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
      for(u32 n = 0; n < count && !physical; n++) {
        if(families[n].queueFlags & VK_QUEUE_GRAPHICS_BIT) physical = candidate, family = n;
      }
      if(physical) { deviceName = properties.deviceName; break; }
    }
    if(!physical) return error = "no Vulkan GPU", false;
    vk.vkGetPhysicalDeviceMemoryProperties(physical, &memoryTypes);
    //The most scale its pictures, framebuffers and viewports allow for a target 512 of the PSP's pixels across (at
    //most 10, as the settings offer)
    VkPhysicalDeviceProperties chosen;
    vk.vkGetPhysicalDeviceProperties(physical, &chosen);
    auto& limits = chosen.limits;
    u32 largest = std::min({limits.maxImageDimension2D, limits.maxFramebufferWidth, limits.maxFramebufferHeight,
                            limits.maxViewportDimensions[0], limits.maxViewportDimensions[1]});
    mostScale = std::clamp<u32>(largest / 512, 1, 10);
    storageAlignment = std::max<u32>(16, limits.minStorageBufferOffsetAlignment);
    //The depth and stencil: 32-bit float depth holds the PSP's 16 bits exactly; 24 bits where that's missing
    for(VkFormat format : {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}) {
      VkFormatProperties properties;
      vk.vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
      if(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        depthFormat = format;
        break;
      }
    }
    if(!depthFormat) return error = "no depth and stencil format", false;

    VkPhysicalDeviceFeatures has{}, wanted{};
    vk.vkGetPhysicalDeviceFeatures(physical, &has);
    wanted.dualSrcBlend = dualSource = has.dualSrcBlend;
    wanted.logicOp = logicOps = has.logicOp;
    wanted.depthClamp = depthClamp = has.depthClamp;
    vk.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    extensions.resize(count);
    vk.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());
    std::vector<const char*> deviceExtensions;
    for(auto& extension : extensions) {  //(a portability implementation's subset must be named when it has one)
      if(!std::strcmp(extension.extensionName, "VK_KHR_portability_subset")) {
        deviceExtensions.push_back("VK_KHR_portability_subset");
      }
      if(surfaces && !std::strcmp(extension.extensionName, "VK_KHR_swapchain")) {
        deviceExtensions.push_back("VK_KHR_swapchain"), canPresent = true;
      }
    }
    //Shader blending (docs/psp-gpu-renderers.md): the target's pixel read through an input attachment, which every
    //Vulkan GPU has. Where the GPU offers rasterization order (EXT's, or ARM's before it) every blend reads, in
    //order; else each draw that reads waits for the ones before (a barrier) and holds no overlapping primitives
    //(GPU::emit()), so only what the GPU's own units can't do reads (GPU::settings())
    reads = true;
    auto ordered = made<VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT);
    const char* orderedName = nullptr;
    for(auto& extension : extensions) {
      for(auto name : {"VK_EXT_rasterization_order_attachment_access",
                       "VK_ARM_rasterization_order_attachment_access"}) {
        if(!orderedName && !std::strcmp(extension.extensionName, name)) orderedName = name;
      }
    }
    auto features2 = (PFN_vkGetPhysicalDeviceFeatures2)getInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2");
    if(orderedName && features2 && application.apiVersion >= VK_API_VERSION_1_1 &&
       chosen.apiVersion >= VK_API_VERSION_1_1 && !fast) {
      auto query = made<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
      query.pNext = &ordered;
      features2(physical, &query);
      readsInOrder = ordered.rasterizationOrderColorAttachmentAccess;
    }
    if(readsInOrder) {
      deviceExtensions.push_back(orderedName);
      ordered.rasterizationOrderDepthAttachmentAccess = ordered.rasterizationOrderStencilAttachmentAccess = false;
    }
    readsBlending = readsInOrder;
    //Qualcomm's own driver (the RP6's, of 2023) draws the start-up check's small pictures right, but in a game's
    //frame buffer, past some dozens of draws a pass, a draw that reads sees the pixels as they were before the
    //draws just before it, barrier or not, and not every time (docs/psp-gpu-renderers.md, "Without rasterization
    //order"): so without rasterization order nothing reads there, and the GPU's own units blend. (The check's
    //game-sized picture is for drivers not known, as it catches this one only some of the time.) Its ID comes from
    //VK_KHR_driver_properties, which a 1.1 instance and device may query without enabling it.
    auto properties2 = (PFN_vkGetPhysicalDeviceProperties2)getInstanceProcAddr(instance,
                                                                             "vkGetPhysicalDeviceProperties2");
    bool driverProperties = false;
    for(auto& extension : extensions) {
      driverProperties |= !std::strcmp(extension.extensionName, "VK_KHR_driver_properties");
    }
    if(properties2 && driverProperties && application.apiVersion >= VK_API_VERSION_1_1 &&
       chosen.apiVersion >= VK_API_VERSION_1_1) {
      auto driver = made<VkPhysicalDeviceDriverProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
      auto query = made<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
      query.pNext = &driver;
      properties2(physical, &query);
      readsApart = driver.driverID != VK_DRIVER_ID_QUALCOMM_PROPRIETARY;
    }
    if((!readsInOrder && !readsApart) || fast) reads = false;
    float priority = 1.0f;
    auto queueInfo = made<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    queueInfo.queueFamilyIndex = family, queueInfo.queueCount = 1, queueInfo.pQueuePriorities = &priority;
    auto deviceInfo = made<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    deviceInfo.queueCreateInfoCount = 1, deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = deviceExtensions.size();
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    deviceInfo.pEnabledFeatures = &wanted;
    if(readsInOrder) deviceInfo.pNext = &ordered;
    if(vk.vkCreateDevice(physical, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
      return error = "no Vulkan device", false;
    }
    #define F(name) vk.name = (PFN_##name)vk.vkGetDeviceProcAddr(device, #name); \
      if(!vk.name) return error = "the Vulkan driver lacks " #name, false;
    PSP_VULKAN_DEVICE(F)
    #undef F
    if(canPresent) {
      #define F(name) vk.name = (PFN_##name)vk.vkGetDeviceProcAddr(device, #name); canPresent = canPresent && vk.name;
      PSP_VULKAN_SWAPCHAIN(F)
      #undef F
    }
    vk.vkGetDeviceQueue(device, family, 0, &queue);

    //One render pass for every target: what's there kept, what's drawn stored
    VkAttachmentDescription attachments[2] = {
      {0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
       VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, ColorLayout, ColorLayout},
      {0, depthFormat, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
       VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, DepthLayout, DepthLayout}};
    //(the colors its input attachment too, for shader blending, with the dependency on itself its barriers need)
    VkAttachmentReference color{0, ColorLayout}, depth{1, DepthLayout};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1, subpass.pColorAttachments = &color, subpass.pDepthStencilAttachment = &depth;
    subpass.inputAttachmentCount = 1, subpass.pInputAttachments = &color;
    orderedPass = readsInOrder;
    if(orderedPass) subpass.flags = VK_SUBPASS_DESCRIPTION_RASTERIZATION_ORDER_ATTACHMENT_COLOR_ACCESS_BIT_EXT;
    VkSubpassDependency dependencies[2] = {
      {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
       VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, 0},
      {0, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT}};
    auto passInfo = made<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    passInfo.attachmentCount = 2, passInfo.pAttachments = attachments;
    passInfo.subpassCount = 1, passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 2, passInfo.pDependencies = dependencies;
    if(vk.vkCreateRenderPass(device, &passInfo, nullptr, &renderPass) != VK_SUCCESS) {
      return error = "no render pass", false;
    }

    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    auto layoutInfo = made<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layoutInfo.bindingCount = 1, layoutInfo.pBindings = &binding;
    if(vk.vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout) != VK_SUCCESS) {
      return error = "no descriptor set layout", false;
    }
    VkDescriptorSetLayoutBinding frameBinding{0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                                              nullptr};
    layoutInfo.pBindings = &frameBinding;
    if(vk.vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &frameLayout) != VK_SUCCESS) {
      return error = "no descriptor set layout", false;
    }
    VkDescriptorSetLayoutBinding transformBinding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
                                                  nullptr};
    layoutInfo.pBindings = &transformBinding;
    if(vk.vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &transformLayout) != VK_SUCCESS) {
      return error = "no descriptor set layout", false;
    }
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPU::Push)};
    VkDescriptorSetLayout drawSets[3] = {setLayout, frameLayout, transformLayout};
    auto pipelineLayoutInfo = made<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pipelineLayoutInfo.setLayoutCount = 3, pipelineLayoutInfo.pSetLayouts = drawSets;
    pipelineLayoutInfo.pushConstantRangeCount = 1, pipelineLayoutInfo.pPushConstantRanges = &range;
    if(vk.vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
      return error = "no pipeline layout", false;
    }
    pipelineLayoutInfo.setLayoutCount = 1, pipelineLayoutInfo.pSetLayouts = &setLayout;  //(copies' and presenting's)
    VkDescriptorPoolSize poolSizes[3] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192},
                                         {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1024},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Slots}};
    auto poolInfo = made<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 8192 + 1024 + Slots, poolInfo.poolSizeCount = 3, poolInfo.pPoolSizes = poolSizes;
    if(vk.vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
      return error = "no descriptor pool", false;
    }
    auto samplerInfo = made<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);  //(texelFetch's: unused)
    samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW =
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(vk.vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) return error = "no sampler", false;

    VkPushConstantRange copyRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CopyPush)};
    pipelineLayoutInfo.pPushConstantRanges = &copyRange;
    if(vk.vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &copyLayout) != VK_SUCCESS) {
      return error = "no pipeline layout", false;
    }
    VkPushConstantRange presentRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4 * sizeof(f32)};  //(present.frag's)
    pipelineLayoutInfo.pPushConstantRanges = &presentRange;
    if(vk.vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &presentLayout) != VK_SUCCESS) {
      return error = "no pipeline layout", false;
    }

    struct Code { const u32* words; size_t size; };
    Code vertexCode{GPUShaders::vertexSPIRV, sizeof(GPUShaders::vertexSPIRV)};
    Code fragmentCode = dualSource ? Code{GPUShaders::fragmentSPIRV, sizeof(GPUShaders::fragmentSPIRV)}
                                   : Code{GPUShaders::fragmentSingleSPIRV, sizeof(GPUShaders::fragmentSingleSPIRV)};
    Code copyVertexCode{GPUShaders::copyVertexSPIRV, sizeof(GPUShaders::copyVertexSPIRV)};
    Code copyFragmentCode{GPUShaders::copyFragmentSPIRV, sizeof(GPUShaders::copyFragmentSPIRV)};
    Code presentCode{GPUShaders::presentSPIRV, sizeof(GPUShaders::presentSPIRV)};
    for(auto [code, module] : {std::pair{vertexCode, &vertexModule}, std::pair{fragmentCode, &fragmentModule},
                               std::pair{copyVertexCode, &copyVertexModule},
                               std::pair{copyFragmentCode, &copyFragmentModule},
                               std::pair{presentCode, &presentModule}}) {
      auto moduleInfo = made<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
      moduleInfo.codeSize = code.size, moduleInfo.pCode = code.words;
      if(vk.vkCreateShaderModule(device, &moduleInfo, nullptr, module) != VK_SUCCESS) {
        return error = "the shaders weren't taken", false;
      }
    }
    //(fast mode's transform.vert: one the driver won't take leaves the GE to transform 3D, as in the accurate mode)
    if(fast) {
      auto moduleInfo = made<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
      moduleInfo.codeSize = sizeof(GPUShaders::transformSPIRV), moduleInfo.pCode = GPUShaders::transformSPIRV;
      if(vk.vkCreateShaderModule(device, &moduleInfo, nullptr, &transformModule) != VK_SUCCESS) {
        transformModule = VK_NULL_HANDLE;
      }
      transforms = transformModule != VK_NULL_HANDLE;
    }
    auto cacheInfo = made<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
    if(vk.vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache) != VK_SUCCESS) {
      pipelineCache = VK_NULL_HANDLE;
    }
    for(u32 mode = 0; mode < 3; mode++) {
      if(!(copyPipelines[mode] = makeCopyPipeline(mode))) return error = "the copy pipelines weren't made", false;
    }
    auto commandPoolInfo = made<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = family;
    if(vk.vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool) != VK_SUCCESS) {
      return error = "no command pool", false;
    }
    for(auto& slot : slots) {
      auto commandInfo = made<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
      commandInfo.commandPool = commandPool, commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      commandInfo.commandBufferCount = 1;
      if(vk.vkAllocateCommandBuffers(device, &commandInfo, &slot.commands) != VK_SUCCESS) {
        return error = "no command buffer", false;
      }
      auto fenceInfo = made<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
      if(vk.vkCreateFence(device, &fenceInfo, nullptr, &slot.fence) != VK_SUCCESS) return error = "no fence", false;
      auto setInfo = made<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
      setInfo.descriptorPool = descriptorPool, setInfo.descriptorSetCount = 1, setInfo.pSetLayouts = &transformLayout;
      if(vk.vkAllocateDescriptorSets(device, &setInfo, &slot.transforms) != VK_SUCCESS) {
        return error = "no descriptor set", false;
      }
      auto semaphoreInfo = made<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
      if(canPresent && vk.vkCreateSemaphore(device, &semaphoreInfo, nullptr, &slot.acquired) != VK_SUCCESS) {
        slot.acquired = VK_NULL_HANDLE, canPresent = false;
      }
    }
    //the blank texture, texture 0
    u32 blank = 0;
    nextId = 0;
    makeTexture(1, 1, &blank);
    if(!textureImages.count(0)) return error = "no texture", false;
    return true;
  }
};

auto GPU::vulkan(void* getInstanceProcAddr, std::string& error, bool fast) -> std::unique_ptr<GPU> {
  auto backend = std::make_unique<VulkanBackend>();
  if(!backend->create((PFN_vkGetInstanceProcAddr)getInstanceProcAddr, error, fast)) return {};
  auto gpu = std::make_unique<GPU>(std::move(backend));
  gpu->fast = fast;
  return gpu;
}
