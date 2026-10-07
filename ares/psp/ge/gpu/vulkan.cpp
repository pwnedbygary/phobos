//The Vulkan backend: the shaders' SPIR-V (shaders/shaders.hpp) run as compute pipelines, through volk (thirdparty/
//volk, which loads the system's Vulkan at run time, so nothing links against it). The buffers live in memory both the
//host and the GPU see (where the GPU has one memory, as phones and Apple's chips do, the GPU's own), mapped for good.
//Each stage is recorded, submitted and waited for: the prototype is synchronous. A wait that lasts far longer than
//any run could (two seconds, a minute for a pipeline's first: a shader stuck, a driver that never says the device is
//lost) marks the device lost, as the driver's
//VK_ERROR_DEVICE_LOST does, and the software renderer draws everything from then on (GPU::draw()).
//
//Device functions come from a table of this device's own (volkLoadDeviceTable()), not volk's globals, which
//paraLLEl-RDP's Vulkan (the N64's) uses for its device.

//A Vulkan structure, zeroed, of its type.
template<typename T> static auto made(VkStructureType type) -> T {
  T value{};
  value.sType = type;
  return value;
}

//A shader's SPIR-V with its execution mode DenormPreserve 32 (SPV_KHR_float_controls): numbers below the normal
//floats kept, as IEEE 754 and the host have them, not flushed to zero. Three instructions, put where SPIR-V's layout
//has them: the capability after the module's own, the extension right after (extensions follow the capabilities),
//and the execution mode after the entry point (execution modes follow the entry points; each stage has one). None,
//should the words not be SPIR-V's instructions.
static auto keepingDenormals(const u32* words, size_t count) -> std::vector<u32> {
  enum : u32 { OpExtension = 10, OpEntryPoint = 15, OpExecutionMode = 16, OpCapability = 17 };
  enum : u32 { CapabilityDenormPreserve = 4464, ExecutionModeDenormPreserve = 4459 };
  const char extension[] = "SPV_KHR_float_controls";
  u32 name[(sizeof(extension) + 3) / 4] = {};  //(a string's bytes in order, zeros after: the hosts are little-endian)
  std::memcpy(name, extension, sizeof(extension));
  if(count < 5) return {};
  std::vector<u32> patched(words, words + 5);  //(the header)
  bool added = false;
  for(size_t at = 5; at < count;) {
    u32 length = words[at] >> 16, opcode = words[at] & 0xffff;
    if(!length || at + length > count) return {};
    if(opcode != OpCapability && !added) {
      added = true;
      patched.insert(patched.end(), {2u << 16 | OpCapability, CapabilityDenormPreserve});
      patched.push_back(u32(1 + std::size(name)) << 16 | OpExtension);
      patched.insert(patched.end(), std::begin(name), std::end(name));
    }
    patched.insert(patched.end(), words + at, words + at + length);
    if(opcode == OpEntryPoint) {  //(its id is its second word after the opcode's)
      patched.insert(patched.end(), {4u << 16 | OpExecutionMode, words[at + 2], ExecutionModeDenormPreserve, 32u});
    }
    at += length;
  }
  return patched;
}

struct VulkanDevice : GPU::Device {
  enum : u32 { VRAMBinding, RecordBinding, TexelBinding, BinBinding, ParameterBinding, Bindings };
  struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
  };

  VolkDeviceTable vk{};
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  u32 family = 0;
  VkPhysicalDeviceMemoryProperties memoryTypes{};
  std::string deviceName;
  Buffer vramBuffer, texelBuffer;  //every run's
  //A run's own: its records, bins and parameters, the descriptor set naming them (with VRAM and the texels), its
  //commands, and the fence it signals. Runs go round the slots, so the host fills one while the GPU draws the others.
  struct Slot {
    Buffer records, bins, parameters;
    VkDescriptorSet set = VK_NULL_HANDLE;
    bool stale = true;  //a buffer was made again since the set was written
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool busy = false;   //submitted, not yet waited for
    bool first = false;  //(a pipeline runs for the first time in it: FirstTimeout)
    u32 pipelines = 0;   //(those it ran, a bit each)
  };
  static constexpr u32 Slots = 3;
  Slot slots[Slots];
  u32 next = 0;     //the slot the next run is filled into
  u32 oldest = 0;   //(the busy slots are oldest, oldest + 1, ..., next - 1, round)
  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  VkShaderModule modules[3] = {};      //bin, raster, probe
  VkPipeline pipelines[3] = {};
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandBuffer commands = VK_NULL_HANDLE;  //(the one being recorded: submit())
  bool keepsDenormals = false;  //the shaders run with DenormPreserve 32 (the device has float controls that can)
  bool timedOut = false;  //a run never finished: what it uses may still be in use, so nothing is destroyed
  //How long a run is waited for, in nanoseconds: one takes milliseconds, but a pipeline's first may take seconds, as
  //a driver may compile its shader only then (Mesa's lavapipe, a CPU's Vulkan: five and a half).
  static constexpr u64 Timeout = 2'000'000'000, FirstTimeout = 60'000'000'000;
  bool ran[3] = {};  //each pipeline has run since it was made

  ~VulkanDevice() override {
    if(timedOut) return;  //(left as it is: destroying what the GPU may still use isn't allowed, nor waiting safe)
    if(device) {
      vk.vkDeviceWaitIdle(device);
      for(auto pipeline : pipelines) if(pipeline) vk.vkDestroyPipeline(device, pipeline, nullptr);
      for(auto module : modules) if(module) vk.vkDestroyShaderModule(device, module, nullptr);
      release(vramBuffer), release(texelBuffer);
      for(auto& slot : slots) {
        release(slot.records), release(slot.bins), release(slot.parameters);
        if(slot.fence) vk.vkDestroyFence(device, slot.fence, nullptr);
      }
      if(commandPool) vk.vkDestroyCommandPool(device, commandPool, nullptr);
      if(descriptorPool) vk.vkDestroyDescriptorPool(device, descriptorPool, nullptr);
      if(pipelineLayout) vk.vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
      if(setLayout) vk.vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
      vk.vkDestroyDevice(device, nullptr);
    }
    if(instance) vkDestroyInstance(instance, nullptr);
  }

  auto name() const -> std::string override {
    return "Vulkan: " + deviceName + (keepsDenormals ? " (DenormPreserve)" : "");
  }

  //A buffer of size bytes in memory the host sees (coherent: no flushing), the GPU's own where it can be. One a run
  //submitted may use is never made again (wait() first).
  auto make(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage) -> bool {
    release(buffer);
    auto info = made<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = size, info.usage = usage, info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if(vk.vkCreateBuffer(device, &info, nullptr, &buffer.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements needs;
    vk.vkGetBufferMemoryRequirements(device, buffer.buffer, &needs);
    constexpr VkMemoryPropertyFlags Shared =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    s32 chosen = -1;
    for(VkMemoryPropertyFlags wanted : {Shared | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Shared}) {
      for(u32 type = 0; type < memoryTypes.memoryTypeCount && chosen < 0; type++) {
        if((needs.memoryTypeBits >> type & 1) && (memoryTypes.memoryTypes[type].propertyFlags & wanted) == wanted) {
          chosen = type;
        }
      }
    }
    if(chosen < 0) return false;
    auto allocation = made<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = needs.size, allocation.memoryTypeIndex = chosen;
    if(vk.vkAllocateMemory(device, &allocation, nullptr, &buffer.memory) != VK_SUCCESS) return false;
    if(vk.vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0) != VK_SUCCESS) return false;
    if(vk.vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped) != VK_SUCCESS) return false;
    buffer.size = size;
    return true;
  }
  auto release(Buffer& buffer) -> void {
    if(buffer.buffer) vk.vkDestroyBuffer(device, buffer.buffer, nullptr);
    if(buffer.memory) vk.vkFreeMemory(device, buffer.memory, nullptr);  //(unmapped with it)
    buffer = {};
  }
  //A storage buffer at least words long, made again (larger) if it isn't; nullptr if that fails.
  auto atLeast(Slot& slot, Buffer& buffer, u32 words) -> u32* {
    VkDeviceSize size = std::max<VkDeviceSize>(u64(words) * 4, 16);
    if(buffer.size < size) {
      if(!make(buffer, std::max(size, buffer.size * 2), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return nullptr;
      slot.stale = true;
    }
    return (u32*)buffer.mapped;
  }

  //The oldest busy slot waited for (false: the GPU lost, or the wait failed).
  auto waitOldest() -> bool {
    Slot& slot = slots[oldest];
    if(!slot.busy) return true;
    VkResult waited = vk.vkWaitForFences(device, 1, &slot.fence, VK_TRUE, slot.first ? FirstTimeout : Timeout);
    if(waited == VK_TIMEOUT) return lost = timedOut = true, false;  //(the fence still waited on: left as it is)
    if(waited == VK_ERROR_DEVICE_LOST) return lost = true, false;
    if(waited != VK_SUCCESS) return lost = true, false;
    vk.vkResetFences(device, 1, &slot.fence);
    for(u32 n = 0; n < 3; n++) ran[n] |= slot.pipelines >> n & 1;
    slot.busy = false;
    oldest = (oldest + 1) % Slots;
    return true;
  }
  //The next run's slot, free: the runs before it in it waited for (nullptr: the GPU lost).
  auto free() -> Slot* {
    if(lost) return nullptr;
    while(slots[next].busy) {
      if(!waitOldest()) return nullptr;
    }
    return &slots[next];
  }

  auto vram() -> u8* override { return (u8*)vramBuffer.mapped; }
  auto records(u32 words) -> u32* override {
    auto slot = free();
    return slot ? atLeast(*slot, slot->records, words) : nullptr;
  }
  auto texels() -> u32* override { return (u32*)texelBuffer.mapped; }
  auto bins(u32 words) -> u32* override {
    auto slot = free();
    return slot ? atLeast(*slot, slot->bins, words) : nullptr;
  }
  auto finish() -> bool override {
    if(lost) return false;
    while(slots[oldest].busy) {
      if(!waitOldest()) return false;
    }
    return true;
  }

  auto create(std::string& error) -> bool {
    if(volkInitialize() != VK_SUCCESS) return error = "no Vulkan loader", false;
    //MoltenVK (Vulkan on Apple's Metal) is a "portability" implementation, listed only to programs that ask
    u32 count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    bool portability = false;
    for(auto& extension : extensions) {
      portability |= !std::strcmp(extension.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }
    const char* instanceExtensions[] = {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
    auto application = made<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    application.pApplicationName = "Phobos PSP GPU renderer";
    application.apiVersion = VK_API_VERSION_1_1;
    auto instanceInfo = made<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    instanceInfo.pApplicationInfo = &application;
    if(portability) {
      instanceInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
      instanceInfo.enabledExtensionCount = 1, instanceInfo.ppEnabledExtensionNames = instanceExtensions;
    }
    if(vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) return error = "no Vulkan instance", false;
    volkLoadInstanceOnly(instance);

    //The first GPU with a compute queue; a GPU that's really the CPU (lavapipe, SwiftShader) only when asked for
    //(PSP_GPU_ON_CPU), as it's no faster than the software renderer and some leak under the address sanitizer.
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    bool onCPU = std::getenv("PSP_GPU_ON_CPU") && *std::getenv("PSP_GPU_ON_CPU");
    for(auto candidate : devices) {
      VkPhysicalDeviceProperties properties;
      vkGetPhysicalDeviceProperties(candidate, &properties);
      if(properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && !onCPU) continue;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
      std::vector<VkQueueFamilyProperties> families(count);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
      for(u32 n = 0; n < count && !physical; n++) {
        if(families[n].queueFlags & VK_QUEUE_COMPUTE_BIT) physical = candidate, family = n;
      }
      if(physical) {
        deviceName = properties.deviceName;
        break;
      }
    }
    if(!physical) return error = "no Vulkan GPU with compute", false;
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryTypes);

    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    extensions.resize(count);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());
    std::vector<const char*> deviceExtensions;
    bool floatControls = false;
    for(auto& extension : extensions) {  //(a portability implementation's subset must be named when it has one)
      if(!std::strcmp(extension.extensionName, "VK_KHR_portability_subset")) {
        deviceExtensions.push_back("VK_KHR_portability_subset");
      }
      floatControls |= !std::strcmp(extension.extensionName, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
    }
    //Whether the shaders can keep numbers below the normal floats (VK_KHR_shader_float_controls, asked through
    //Vulkan 1.1's properties2). Apple's GPUs through MoltenVK can't say so, and flush them.
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    if(floatControls && properties.apiVersion >= VK_API_VERSION_1_1 && vkGetPhysicalDeviceProperties2) {
      auto controls = made<VkPhysicalDeviceFloatControlsPropertiesKHR>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR);
      auto asked = made<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
      asked.pNext = &controls;
      vkGetPhysicalDeviceProperties2(physical, &asked);
      if(controls.shaderDenormPreserveFloat32) {
        deviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
        keepsDenormals = true;
      }
    }
    float priority = 1.0f;
    auto queueInfo = made<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    queueInfo.queueFamilyIndex = family, queueInfo.queueCount = 1, queueInfo.pQueuePriorities = &priority;
    auto deviceInfo = made<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    deviceInfo.queueCreateInfoCount = 1, deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = deviceExtensions.size();
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    if(vkCreateDevice(physical, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
      return error = "no Vulkan device", false;
    }
    volkLoadDeviceTable(&vk, device);
    vk.vkGetDeviceQueue(device, family, 0, &queue);

    bool buffersMade = make(vramBuffer, Memory::VRAMSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
                make(texelBuffer, u64(GPU::TexelWords) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    for(auto& slot : slots) {
      buffersMade = buffersMade && make(slot.records, 1 << 20, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
             make(slot.bins, 1 << 20, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
             make(slot.parameters, sizeof(GPU::Parameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }
    if(!buffersMade) return error = "the GPU's buffers weren't made", false;

    VkDescriptorSetLayoutBinding bindings[Bindings];
    for(u32 n = 0; n < Bindings; n++) {
      bindings[n] = {n, n == ParameterBinding ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                     1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    auto layoutInfo = made<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layoutInfo.bindingCount = Bindings, layoutInfo.pBindings = bindings;
    if(vk.vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout) != VK_SUCCESS) {
      return error = "no descriptor set layout", false;
    }
    auto pipelineLayoutInfo = made<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pipelineLayoutInfo.setLayoutCount = 1, pipelineLayoutInfo.pSetLayouts = &setLayout;
    if(vk.vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
      return error = "no pipeline layout", false;
    }
    VkDescriptorPoolSize sizes[2] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * Slots}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Slots}};
    auto poolInfo = made<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.maxSets = Slots, poolInfo.poolSizeCount = 2, poolInfo.pPoolSizes = sizes;
    if(vk.vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
      return error = "no descriptor pool", false;
    }
    for(auto& slot : slots) {
      auto setInfo = made<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
      setInfo.descriptorPool = descriptorPool, setInfo.descriptorSetCount = 1, setInfo.pSetLayouts = &setLayout;
      if(vk.vkAllocateDescriptorSets(device, &setInfo, &slot.set) != VK_SUCCESS) {
        return error = "no descriptor set", false;
      }
    }

    struct Code { const u32* words; size_t size; } codes[3] = {
      {GPUShaders::binSPIRV, sizeof(GPUShaders::binSPIRV)},
      {GPUShaders::rasterSPIRV, sizeof(GPUShaders::rasterSPIRV)},
      {GPUShaders::probeSPIRV, sizeof(GPUShaders::probeSPIRV)},
    };
    for(u32 n = 0; n < 3; n++) {
      std::vector<u32> kept;
      if(keepsDenormals) {
        kept = keepingDenormals(codes[n].words, codes[n].size / 4);
        if(kept.empty()) return error = "the shaders weren't SPIR-V", false;
        codes[n] = {kept.data(), kept.size() * 4};
      }
      auto moduleInfo = made<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
      moduleInfo.codeSize = codes[n].size, moduleInfo.pCode = codes[n].words;
      if(vk.vkCreateShaderModule(device, &moduleInfo, nullptr, &modules[n]) != VK_SUCCESS) {
        return error = "the shaders weren't taken", false;
      }
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
    }
    return true;
  }

  auto configure(bool fused, bool nativeFma) -> bool override {
    if(!finish()) return false;
    u32 values[2] = {fused, nativeFma};
    VkSpecializationMapEntry entries[2] = {{0, 0, 4}, {1, 4, 4}};
    VkSpecializationInfo specialization{2, entries, sizeof(values), values};
    for(u32 n = 0; n < 3; n++) {
      if(pipelines[n]) vk.vkDestroyPipeline(device, pipelines[n], nullptr), pipelines[n] = VK_NULL_HANDLE;
      ran[n] = false;
      auto info = made<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
      info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT,
                    modules[n], "main", &specialization};
      info.layout = pipelineLayout;
      if(vk.vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipelines[n]) != VK_SUCCESS) {
        return false;
      }
    }
    return true;
  }

  //The commands recorded by record(commands), using the pipelines first to last, submitted in the next slot (its
  //descriptor set written first if one of its buffers was made again), and not waited for. The run before it may
  //still be drawing: what it wrote is made visible to this one's shaders first (barrier()), and what this one's
  //shaders write to the host once it's done.
  template<typename Record>
  auto submit(const GPU::Parameters& parameters, u32 first, u32 last, const Record& record) -> bool {
    auto slot = free();
    if(!slot) return false;
    std::memcpy(slot->parameters.mapped, &parameters, sizeof(parameters));
    if(slot->stale) {
      Buffer* named[Bindings] = {&vramBuffer, &slot->records, &texelBuffer, &slot->bins, &slot->parameters};
      VkDescriptorBufferInfo infos[Bindings];
      VkWriteDescriptorSet writes[Bindings];
      for(u32 n = 0; n < Bindings; n++) {
        infos[n] = {named[n]->buffer, 0, VK_WHOLE_SIZE};
        writes[n] = made<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writes[n].dstSet = slot->set, writes[n].dstBinding = n, writes[n].descriptorCount = 1;
        writes[n].descriptorType = n == ParameterBinding ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                         : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[n].pBufferInfo = &infos[n];
      }
      vk.vkUpdateDescriptorSets(device, Bindings, writes, 0, nullptr);
      slot->stale = false;
    }
    commands = slot->commands;
    vk.vkResetCommandBuffer(commands, 0);
    auto begin = made<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk.vkBeginCommandBuffer(commands, &begin);
    barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &slot->set, 0,
                               nullptr);
    record();
    barrier(VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);  //(what the shaders wrote, for the host)
    vk.vkEndCommandBuffer(commands);
    auto submitInfo = made<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submitInfo.commandBufferCount = 1, submitInfo.pCommandBuffers = &commands;
    VkResult submitted = vk.vkQueueSubmit(queue, 1, &submitInfo, slot->fence);
    if(submitted == VK_ERROR_DEVICE_LOST) return lost = true, false;
    if(submitted != VK_SUCCESS) return false;
    slot->busy = true;
    slot->pipelines = 0, slot->first = false;
    for(u32 n = first; n <= last; n++) slot->pipelines |= 1 << n, slot->first |= !ran[n];
    next = (next + 1) % Slots;
    return true;
  }
  auto barrier(VkPipelineStageFlags stage, VkAccessFlags access) -> void {
    auto memory = made<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    memory.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, memory.dstAccessMask = access;
    vk.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, stage, 0, 1, &memory, 0, nullptr, 0,
                            nullptr);
  }

  auto draw(const GPU::Parameters& p) -> bool override {
    return submit(p, 0, 1, [&] {
      vk.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[0]);
      vk.vkCmdDispatch(commands, (p.jobCount + 63) / 64, 1, 1);
      barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
      vk.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[1]);
      vk.vkCmdDispatch(commands, p.tilesAcross, p.tilesDown, 1);
    });
  }
  auto probe(const GPU::Parameters& p) -> bool override {
    return submit(p, 2, 2, [&] {
      vk.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[2]);
      vk.vkCmdDispatch(commands, (p.probeCount + 63) / 64, 1, 1);
    }) && finish() && (next = oldest = (next + Slots - 1) % Slots, true);  //(its bins read: its slot next again)
  }
};

//A GPU renderer on the first Vulkan GPU, its rounding found out (GPU::detect()); none, and why, where there's no
//Vulkan, no GPU, or it fails.
auto GPU::vulkan(std::string& error) -> std::unique_ptr<GPU> {
  auto device = std::make_unique<VulkanDevice>();
  if(!device->create(error)) return {};
  auto gpu = std::make_unique<GPU>(std::move(device));
  if(!gpu->detect(error)) return {};
  return gpu;
}
