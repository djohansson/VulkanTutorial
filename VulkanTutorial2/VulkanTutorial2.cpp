#include "VulkanTutorial2.hpp"

#include "lodepng.h"

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_macos.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <assert.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#define CHECK_VKRESULT(res) \
assert(res == VK_SUCCESS)

template <typename T>
constexpr auto sizeof_array(const T& array)
{
    return (sizeof(array) / sizeof(array[0]));
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char* layerPrefix, const char* msg, void* userData)
{
    std::cerr << "validation layer: " << msg << std::endl; return VK_FALSE;
}

static std::vector<char> readSPIRVFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("failed to open file!");
    
    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    
    return buffer;
}

struct Vertex
{
    float pos[2];
    float color[3];
    float texCoord[2];
    
    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription = {};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }
    
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions = {};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);
        return attributeDescriptions;
    }
};

struct Vec4
{
    union
    {
        struct
        {
            float x;
            float y;
            float z;
            float w;
        };
        float data[4];
    };
};

struct PNGImage
{
    PNGImage(const char* filename)
    {
        unsigned error = lodepng::decode(myImage, myWidth, myHeight, filename);
        
        if (error)
            std::cerr << "decoder error " << error << ": " << lodepng_error_text(error) << std::endl;
        
        assert(!error);
    }
    
    std::vector<unsigned char> myImage;
    unsigned myWidth = 0;
    unsigned myHeight = 0;
};

struct UniformBufferObject
{
    Vec4 model[4];
    Vec4 view[4];
    Vec4 proj[4];
};

class VulkanTutorialApp
{
public:
    
    VulkanTutorialApp(void* view, int width, int height)
    {
        initVulkan(view, width, height);
    }
    
    ~VulkanTutorialApp()
    {
        cleanup();
    }
    
    void run()
    {
        mainLoop();
    }
    
private:
    
    void createInstance()
    {
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_1;
        
        uint32_t instanceLayerCount;
        CHECK_VKRESULT(vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr));
        std::cout << instanceLayerCount << " layers found!\n";
        if (instanceLayerCount > 0)
        {
            std::unique_ptr<VkLayerProperties[]> instanceLayers(new VkLayerProperties[instanceLayerCount]);
            CHECK_VKRESULT(vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayers.get()));
            for (int i = 0; i < instanceLayerCount; ++i)
            {
                std::cout << instanceLayers[i].layerName << "\n";
            }
        }
        
        const char * enabledLayerNames[] =
        {
            "VK_LAYER_LUNARG_standard_validation"
        };
        
        uint32_t instanceExtensionCount;
        vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr);
        
        std::vector<VkExtensionProperties> availableInstanceExtensions(instanceExtensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, availableInstanceExtensions.data());
        
        std::vector<const char*> instanceExtensions(instanceExtensionCount);
        for (uint i = 0; i < instanceExtensionCount; i++)
            instanceExtensions[i] = availableInstanceExtensions[i].extensionName;
        
        std::sort(instanceExtensions.begin(), instanceExtensions.end(), [](const char* lhs, const char* rhs)
        {
            return strcmp(lhs, rhs) < 0;
        });
        
        std::vector<const char*> requiredExtensions =
        {
            "VK_KHR_surface",
            "VK_MVK_macos_surface",
        };
        
        assert(std::includes(instanceExtensions.begin(), instanceExtensions.end(), requiredExtensions.begin(), requiredExtensions.end(), [](const char* lhs, const char* rhs)
                             {
                                 return strcmp(lhs, rhs) < 0;
                             }));
        
        VkInstanceCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &appInfo;
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = enabledLayerNames;
        info.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
        info.ppEnabledExtensionNames = instanceExtensions.data();
        
        CHECK_VKRESULT(vkCreateInstance(&info, NULL, &myInstance));
    }
    
    void createDebugCallback()
    {
        VkDebugReportCallbackCreateInfoEXT debugCallbackInfo = {};
        debugCallbackInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debugCallbackInfo.flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT| VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        debugCallbackInfo.pfnCallback = debugCallback;
        
        auto vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(myInstance, "vkCreateDebugReportCallbackEXT");
        assert(vkCreateDebugReportCallbackEXT != nullptr);
        CHECK_VKRESULT(vkCreateDebugReportCallbackEXT(myInstance, &debugCallbackInfo, nullptr, &myDebugCallback));
    }
    
    void createSurface(void* view)
    {
        VkMacOSSurfaceCreateInfoMVK surfaceCreateInfo = {};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK;
        surfaceCreateInfo.flags = 0;
        surfaceCreateInfo.pView = view;
        auto vkCreateMacOSSurfaceMVK = (PFN_vkCreateMacOSSurfaceMVK)vkGetInstanceProcAddr(myInstance, "vkCreateMacOSSurfaceMVK");
        assert(vkCreateMacOSSurfaceMVK != nullptr);
        CHECK_VKRESULT(vkCreateMacOSSurfaceMVK(myInstance, &surfaceCreateInfo, nullptr, &mySurface));
    }
    
    void createDevice()
    {
        uint32_t deviceCount = 0;
        CHECK_VKRESULT(vkEnumeratePhysicalDevices(myInstance, &deviceCount, nullptr));
        if (deviceCount == 0)
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        
        std::vector<VkPhysicalDevice> devices(deviceCount);
        CHECK_VKRESULT(vkEnumeratePhysicalDevices(myInstance, &deviceCount, devices.data()));
        
        for (const auto& device : devices)
        {
            myQueueFamilyIndex = isDeviceSuitable(myInstance, mySurface, device);
            if (myQueueFamilyIndex >= 0)
            {
                myPhysicalDevice = device;
                break;
            }
        }
        
        if (myPhysicalDevice == VK_NULL_HANDLE)
        {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
        
        const float graphicsQueuePriority = 1.0f;
        
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = myQueueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &graphicsQueuePriority;
        
        VkPhysicalDeviceFeatures deviceFeatures = {};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        
        uint32_t deviceExtensionCount;
        vkEnumerateDeviceExtensionProperties(myPhysicalDevice, nullptr, &deviceExtensionCount, nullptr);
        
        std::vector<VkExtensionProperties> availableDeviceExtensions(deviceExtensionCount);
        vkEnumerateDeviceExtensionProperties(myPhysicalDevice, nullptr, &deviceExtensionCount, availableDeviceExtensions.data());
        
        assert(std::find_if(availableDeviceExtensions.begin(), availableDeviceExtensions.end(), [](const VkExtensionProperties& extension)
        {
            return strcmp(extension.extensionName, "VK_KHR_swapchain") == 0;
        }) != availableDeviceExtensions.end());
        
        std::vector<const char*> deviceExtensions;
        for (const auto& extension : availableDeviceExtensions)
        {
            if (strcmp(extension.extensionName, "VK_MVK_moltenvk") == 0 ||
                strcmp(extension.extensionName, "VK_KHR_surface") == 0 ||
                strcmp(extension.extensionName, "VK_MVK_macos_surface") == 0)
                continue;
            
            deviceExtensions.push_back(extension.extensionName);
        }
        
        std::sort(deviceExtensions.begin(), deviceExtensions.end(), [](const char* lhs, const char* rhs)
        {
            return strcmp(lhs, rhs) < 0;
        });
        
        /*
         static const std::vector<const char*> requiredExtensions =
         {
         "VK_KHR_get_memory_requirements2",
         "VK_KHR_dedicated_allocation",
         };
         
         assert(std::includes(deviceExtensions.begin(), deviceExtensions.end(), requiredExtensions.begin(), requiredExtensions.end(), [](const char* lhs, const char* rhs)
         {
         return strcmp(lhs, rhs) < 0;
         }));
         */
        
        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        
        CHECK_VKRESULT(vkCreateDevice(myPhysicalDevice, &deviceCreateInfo, nullptr, &myDevice));
        
        vkGetDeviceQueue(myDevice, myQueueFamilyIndex, 0, &myQueue);
    }
    
    void createAllocator()
    {
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = myPhysicalDevice;
        allocatorInfo.device = myDevice;
        vmaCreateAllocator(&allocatorInfo, &myAllocator);
    }
    
    void createSwapChain(uint width, uint height)
    {
        VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
        swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapChainCreateInfo.surface = mySurface;
        swapChainCreateInfo.minImageCount = 2;
        swapChainCreateInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        swapChainCreateInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        swapChainCreateInfo.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
        swapChainCreateInfo.imageArrayLayers = 1;
        swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapChainCreateInfo.queueFamilyIndexCount = 0;
        swapChainCreateInfo.pQueueFamilyIndices = nullptr;
        swapChainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapChainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapChainCreateInfo.clipped = VK_TRUE;
        swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
        
        CHECK_VKRESULT(vkCreateSwapchainKHR(myDevice, &swapChainCreateInfo, nullptr, &mySwapChain));
        
        mySwapChainImageFormat = swapChainCreateInfo.imageFormat;
        mySwapChainExtent = swapChainCreateInfo.imageExtent;
        
        uint32_t imageCount;
        vkGetSwapchainImagesKHR(myDevice, mySwapChain, &imageCount, nullptr);
        
        mySwapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(myDevice, mySwapChain, &imageCount, mySwapChainImages.data());
        
        mySwapChainImageViews.resize(imageCount);
        for (size_t i = 0; i < imageCount; i++)
            mySwapChainImageViews[i] = createImageView2D(mySwapChainImages[i], mySwapChainImageFormat);
    }
    
    void createRenderPass()
    {
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = mySwapChainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        
        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        
        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        
        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        
        CHECK_VKRESULT(vkCreateRenderPass(myDevice, &renderPassInfo, nullptr, &myRenderPass));
    }
    
    void createFramebuffers()
    {
        mySwapChainFramebuffers.resize(mySwapChainImageViews.size());
        for (size_t i = 0; i < mySwapChainImageViews.size(); i++)
        {
            VkImageView attachments[] = { mySwapChainImageViews[i] };
            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = myRenderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = mySwapChainExtent.width;
            framebufferInfo.height = mySwapChainExtent.height;
            framebufferInfo.layers = 1;
            
            CHECK_VKRESULT(vkCreateFramebuffer(myDevice, &framebufferInfo, nullptr, &mySwapChainFramebuffers[i]));
        }
    }
    
    void createDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 2> poolSizes = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 1;
        
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;
        poolInfo.flags = 0; //VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
        
        CHECK_VKRESULT(vkCreateDescriptorPool(myDevice, &poolInfo, nullptr, &myDescriptorPool));
    }
    
    void createDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;
        
        VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        samplerLayoutBinding.pImmutableSamplers = &mySampler;
        
        std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
        
        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        
        CHECK_VKRESULT(vkCreateDescriptorSetLayout(myDevice, &layoutInfo, nullptr, &myDescriptorSetLayout));
    }
    
    void createDescriptorSet()
    {
        VkDescriptorSetLayout layouts[] = { myDescriptorSetLayout };
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = myDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = layouts;
        
        CHECK_VKRESULT(vkAllocateDescriptorSets(myDevice, &allocInfo, &myDescriptorSet));
        
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = myUniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);
        
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = myImageView;
        imageInfo.sampler = mySampler;
        
        std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = myDescriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = myDescriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;
        
        vkUpdateDescriptorSets(myDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
    
    void createGraphicsPipeline()
    {
        auto vsCode = readSPIRVFile("vert.spv");
        
        VkShaderModuleCreateInfo vsCreateInfo = {};
        vsCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vsCreateInfo.codeSize = vsCode.size();
        vsCreateInfo.pCode = reinterpret_cast<const uint32_t*>(vsCode.data());
        
        VkShaderModule vsModule;
        CHECK_VKRESULT(vkCreateShaderModule(myDevice, &vsCreateInfo, nullptr, &vsModule));
        
        VkPipelineShaderStageCreateInfo vsStageInfo = {};
        vsStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vsStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vsStageInfo.module = vsModule;
        vsStageInfo.pName = "main";
        
        auto fsCode = readSPIRVFile("frag.spv");
        
        VkShaderModuleCreateInfo fsCreateInfo = {};
        fsCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fsCreateInfo.codeSize = fsCode.size();
        fsCreateInfo.pCode = reinterpret_cast<const uint32_t*>(fsCode.data());
        
        VkShaderModule fsModule;
        CHECK_VKRESULT(vkCreateShaderModule(myDevice, &fsCreateInfo, nullptr, &fsModule));
        
        VkPipelineShaderStageCreateInfo fsStageInfo = {};
        fsStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fsStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fsStageInfo.module = fsModule;
        fsStageInfo.pName = "main";
        
        VkPipelineShaderStageCreateInfo shaderStages[] = { vsStageInfo, fsStageInfo };
        
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        
        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
        
        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;
        
        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)mySwapChainExtent.width;
        viewport.height = (float)mySwapChainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        
        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = mySwapChainExtent;
        
        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;
        
        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f;
        rasterizer.depthBiasClamp = 0.0f;
        rasterizer.depthBiasSlopeFactor = 0.0f;
        
        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f;
        multisampling.pSampleMask = nullptr;
        multisampling.alphaToCoverageEnable = VK_FALSE;
        multisampling.alphaToOneEnable = VK_FALSE;
        
        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        
        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;
        
        //VkPipelineDynamicStateCreateInfo dynamicState = {};
        //dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        //dynamicState.dynamicStateCount = 0;
        //dynamicState.pDynamicStates = nullptr;
        
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &myDescriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;
        
        CHECK_VKRESULT(vkCreatePipelineLayout(myDevice, &pipelineLayoutInfo, nullptr, &myPipelineLayout));
        
        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = nullptr;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = nullptr;
        pipelineInfo.layout = myPipelineLayout;
        pipelineInfo.renderPass = myRenderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;
        
        CHECK_VKRESULT(vkCreateGraphicsPipelines(myDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &myGraphicsPipeline));
        
        vkDestroyShaderModule(myDevice, vsModule, nullptr);
        vkDestroyShaderModule(myDevice, fsModule, nullptr);
    }
    
    void createCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = myQueueFamilyIndex;
        poolInfo.flags = 0;
        
        CHECK_VKRESULT(vkCreateCommandPool(myDevice, &poolInfo, nullptr, &myCommandPool));
    }
    
    void createCommandBuffers()
    {
        myCommandBuffers.resize(mySwapChainFramebuffers.size());
        
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = myCommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)myCommandBuffers.size();
        
        CHECK_VKRESULT(vkAllocateCommandBuffers(myDevice, &allocInfo, myCommandBuffers.data()));
    }
    
    void createSyncObjects()
    {
        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        
        myImageAvailableSemaphores.resize(MaxFramesInFlight);
        myRenderFinishedSemaphores.resize(MaxFramesInFlight);
        myInFlightFences.resize(MaxFramesInFlight);
        for (uint i = 0; i < MaxFramesInFlight; i++)
        {
            CHECK_VKRESULT(vkCreateSemaphore(myDevice, &semaphoreInfo, nullptr, &myImageAvailableSemaphores[i]));
            CHECK_VKRESULT(vkCreateSemaphore(myDevice, &semaphoreInfo, nullptr, &myRenderFinishedSemaphores[i]));
            CHECK_VKRESULT(vkCreateFence(myDevice, &fenceInfo, nullptr, &myInFlightFences[i]));
        }
    }
    
    VkCommandBuffer beginSingleTimeCommands()
    {
        VkCommandBuffer commandBuffer;
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = myCommandPool;
        allocInfo.commandBufferCount = 1;
        
        CHECK_VKRESULT(vkAllocateCommandBuffers(myDevice, &allocInfo, &commandBuffer));
        
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        
        CHECK_VKRESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        
        return commandBuffer;
    }
    
    void endSingleTimeCommands(VkCommandBuffer commandBuffer)
    {
        CHECK_VKRESULT(vkEndCommandBuffer(commandBuffer));
        
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        
        CHECK_VKRESULT(vkQueueSubmit(myQueue, 1, &submitInfo, VK_NULL_HANDLE));
        CHECK_VKRESULT(vkQueueWaitIdle(myQueue));
        
        vkFreeCommandBuffers(myDevice, myCommandPool, 1, &commandBuffer);
    }
    
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        
        VkBufferCopy copyRegion = {};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
        
        endSingleTimeCommands(commandBuffer);
    }
    
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags, VkBuffer& outBuffer, VmaAllocation& outBufferMemory)
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_UNKNOWN;
        allocInfo.requiredFlags = flags;
        allocInfo.memoryTypeBits = 0;//memRequirements.memoryTypeBits;
        
        CHECK_VKRESULT(vmaCreateBuffer(myAllocator, &bufferInfo, &allocInfo, &outBuffer, &outBufferMemory, nullptr));
    }
    
    template <typename T>
    void createDeviceLocalBuffer(const T* bufferData, uint bufferElementCount, VkBufferUsageFlags usage, VkBuffer& outBuffer, VmaAllocation& outBufferMemory)
    {
        assert(bufferData != nullptr);
        assert(bufferElementCount > 0);
        size_t bufferSize = sizeof(T) * bufferElementCount;
        
        // todo: use staging buffer pool, or use scratchpad memory
        VkBuffer stagingBuffer;
        VmaAllocation stagingBufferMemory;
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
        
        void* data;
        CHECK_VKRESULT(vmaMapMemory(myAllocator, stagingBufferMemory, &data));
        memcpy(data, bufferData, bufferSize);
        vmaUnmapMemory(myAllocator, stagingBufferMemory);
        
        createBuffer(bufferSize, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outBuffer, outBufferMemory);
        
        copyBuffer(stagingBuffer, outBuffer, bufferSize);
        
        vmaDestroyBuffer(myAllocator, stagingBuffer, stagingBufferMemory);
    }
    
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        
        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            assert(false); // not implemented yet
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = 0;
        }
        
        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        
        endSingleTimeCommands(commandBuffer);
    }
    
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint width, uint height)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        
        vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        
        endSingleTimeCommands(commandBuffer);
    }
    
    void createImage2D(uint width, uint height, uint /*pixelSizeBytes*/, VkImageUsageFlags usage, VkMemoryPropertyFlags memoryFlags, VkImage& outImage, VmaAllocation& outImageMemory)
    {
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = static_cast<uint32_t>(width);
        imageInfo.extent.height = static_cast<uint32_t>(height);
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = 0;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = (memoryFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_UNKNOWN;
        allocInfo.requiredFlags = memoryFlags;
        allocInfo.memoryTypeBits = 0;//memRequirements.memoryTypeBits;
        
        VmaAllocationInfo outAllocInfo = {};
        CHECK_VKRESULT(vmaCreateImage(myAllocator, &imageInfo, &allocInfo, &outImage, &outImageMemory, &outAllocInfo));
    }
    
    template <typename T>
    void createDeviceLocalImage2D(const T* imageData, uint width, uint height, uint pixelSizeBytes, VkImageUsageFlags usage, VkImage& outImage, VmaAllocation& outImageMemory)
    {
        VkDeviceSize imageSize = width * height * pixelSizeBytes;
        //assert(sizeof_array(imageData) == imageSize);
        
        VkBuffer stagingBuffer;
        VmaAllocation stagingBufferMemory;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
        
        void* data;
        CHECK_VKRESULT(vmaMapMemory(myAllocator, stagingBufferMemory, &data));
        memcpy(data, imageData, imageSize);
        vmaUnmapMemory(myAllocator, stagingBufferMemory);
        
        createImage2D(width, height, pixelSizeBytes, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outImage, outImageMemory);
        
        transitionImageLayout(myImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copyBufferToImage(stagingBuffer, myImage, width, height);
        transitionImageLayout(myImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        
        vmaDestroyBuffer(myAllocator, stagingBuffer, stagingBufferMemory);
    }
    
    VkImageView createImageView2D(VkImage image, VkFormat format)
    {
        VkImageView imageView;
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        
        CHECK_VKRESULT(vkCreateImageView(myDevice, &viewInfo, nullptr, &imageView));
        
        return imageView;
    }
    
    void createTextureSampler()
    {
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        
        CHECK_VKRESULT(vkCreateSampler(myDevice, &samplerInfo, nullptr, &mySampler));
    }
    
    void initVulkan(void* window, int width, int height)
    {
        createInstance();
        createDebugCallback();
        createSurface(window);
        createDevice();
        createAllocator();
        createCommandPool();
        createSwapChain(width, height);
        createRenderPass();
        createFramebuffers();
        
        // todo - make all single time comands use same command buffer
        createDeviceLocalBuffer(ourVertices, sizeof_array(ourVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, myVertexBuffer, myVertexBufferMemory);
        createDeviceLocalBuffer(ourIndices, sizeof_array(ourIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, myIndexBuffer, myIndexBufferMemory);
        {
            const PNGImage pngImage = PNGImage("/../Resources/fractal_tree.png");
            createDeviceLocalImage2D(pngImage.myImage.data(), pngImage.myWidth, pngImage.myHeight, 4, VK_IMAGE_USAGE_SAMPLED_BIT, myImage, myImageMemory);
            myImageView = createImageView2D(myImage, VK_FORMAT_R8G8B8A8_UNORM);
            
        }
        createBuffer(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, myUniformBuffer, myUniformBufferMemory);
        
        createTextureSampler();
        createDescriptorPool();
        createDescriptorSetLayout();
        createDescriptorSet();
        createGraphicsPipeline();
        createCommandBuffers();
        createSyncObjects();
        
        recordCommandBuffers();
    }
    
    void recreateSwapChain(uint width, uint height)
    {
        CHECK_VKRESULT(vkDeviceWaitIdle(myDevice));
        
        cleanupSwapChain();
        
        createSwapChain(width, height);
        createRenderPass();
        createFramebuffers();
        createGraphicsPipeline();
        createCommandBuffers();
        
        recordCommandBuffers();
    }
    
    void recordCommandBuffers()
    {
        for (size_t i = 0; i < myCommandBuffers.size(); i++)
        {
            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            beginInfo.pInheritanceInfo = nullptr;
            CHECK_VKRESULT(vkBeginCommandBuffer(myCommandBuffers[i], &beginInfo));
            
            VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
            
            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = myRenderPass;
            renderPassInfo.framebuffer = mySwapChainFramebuffers[i];
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = mySwapChainExtent;
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearColor;
            
            VkBuffer vertexBuffers[] = { myVertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            
            vkCmdBeginRenderPass(myCommandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindDescriptorSets(myCommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, myPipelineLayout, 0, 1, &myDescriptorSet, 0, nullptr);
            vkCmdBindPipeline(myCommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, myGraphicsPipeline);
            vkCmdBindVertexBuffers(myCommandBuffers[i], 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(myCommandBuffers[i], myIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(myCommandBuffers[i], static_cast<uint32_t>(sizeof_array(ourIndices)), 1, 0, 0, 0);
            vkCmdEndRenderPass(myCommandBuffers[i]);
            
            CHECK_VKRESULT(vkEndCommandBuffer(myCommandBuffers[i]));
        }
    }
    
    void checkFlipOrPresentResult(VkResult result)
    {
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            recreateSwapChain(mySwapChainExtent.width, mySwapChainExtent.height);
        else if (result != VK_SUCCESS)
            throw std::runtime_error("failed to flip swap chain image!");
    }
    
    void updateUniformBuffer(float time)
    {
        UniformBufferObject ubo = {};
        ubo.model[0] = { 2, 0, 0, 0 };
        ubo.model[1] = { 0, 2, 0, 0 };
        ubo.model[2] = { 0, 0, 2, 0 };
        ubo.model[3] = { 0, 0, 0, 1 };
        ubo.view[0] = { 1, 0, 0, 0 };
        ubo.view[1] = { 0, 1, 0, 0 };
        ubo.view[2] = { 0, 0, 1, 0 };
        ubo.view[3] = { 0, 0, 0, 1 };
        ubo.proj[0] = { 1, 0, 0, 0 };
        ubo.proj[1] = { 0, 1, 0, 0 };
        ubo.proj[2] = { 0, 0, 1, 0 };
        ubo.proj[3] = { 0, 0, 0, 1 };
        
        void* data;
        CHECK_VKRESULT(vmaMapMemory(myAllocator, myUniformBufferMemory, &data));
        memcpy(data, &ubo, sizeof(ubo));
        vmaUnmapMemory(myAllocator, myUniformBufferMemory);
    }
    
    void drawFrame()
    {
        CHECK_VKRESULT(vkWaitForFences(myDevice, 1, &myInFlightFences[myCurrentFrame], VK_TRUE, std::numeric_limits<uint64_t>::max()));
        CHECK_VKRESULT(vkResetFences(myDevice, 1, &myInFlightFences[myCurrentFrame]));
        
        uint32_t imageIndex;
        checkFlipOrPresentResult(vkAcquireNextImageKHR(myDevice, mySwapChain, std::numeric_limits<uint64_t>::max(), myImageAvailableSemaphores[myCurrentFrame], VK_NULL_HANDLE, &imageIndex));
        
        VkSemaphore waitSemaphores[] = { myImageAvailableSemaphores[myCurrentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSemaphores[] = { myRenderFinishedSemaphores[myCurrentFrame] };
        
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &myCommandBuffers[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;
        
        CHECK_VKRESULT(vkQueueSubmit(myQueue, 1, &submitInfo, myInFlightFences[myCurrentFrame]));
        
        VkSwapchainKHR swapChains[] = { mySwapChain };
        
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pResults = nullptr;
        
        checkFlipOrPresentResult(vkQueuePresentKHR(myQueue, &presentInfo));
        
        myCurrentFrame = (myCurrentFrame + 1) % MaxFramesInFlight;
    }
    
    void mainLoop()
    {
        do
        {
            //static auto startTime = std::chrono::high_resolution_clock::now();
            //auto currentTime = std::chrono::high_resolution_clock::now();
            float time = 0.0f;//std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime);
            updateUniformBuffer(time);
            drawFrame();
        } while (true); // todo
        
        CHECK_VKRESULT(vkDeviceWaitIdle(myDevice));
    }
    
    void cleanupSwapChain()
    {
        for (size_t i = 0; i < mySwapChainFramebuffers.size(); i++)
            vkDestroyFramebuffer(myDevice, mySwapChainFramebuffers[i], nullptr);
        
        vkFreeCommandBuffers(myDevice, myCommandPool, static_cast<uint32_t>(myCommandBuffers.size()), myCommandBuffers.data());
        vkDestroyPipeline(myDevice, myGraphicsPipeline, nullptr);
        vkDestroyPipelineLayout(myDevice, myPipelineLayout, nullptr);
        vkDestroyRenderPass(myDevice, myRenderPass, nullptr);
        
        for (size_t i = 0; i < mySwapChainImageViews.size(); i++)
            vkDestroyImageView(myDevice, mySwapChainImageViews[i], nullptr);
        
        vkDestroySwapchainKHR(myDevice, mySwapChain, nullptr);
    }
    
    void cleanup()
    {
        cleanupSwapChain();
        
        for (uint i = 0; i < MaxFramesInFlight; i++)
        {
            vkDestroySemaphore(myDevice, myRenderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(myDevice, myImageAvailableSemaphores[i], nullptr);
            vkDestroyFence(myDevice, myInFlightFences[i], nullptr);
        }
        
        vkDestroyDescriptorSetLayout(myDevice, myDescriptorSetLayout, nullptr);
        vkDestroyDescriptorPool(myDevice, myDescriptorPool, nullptr);
        
        vmaDestroyBuffer(myAllocator, myUniformBuffer, myUniformBufferMemory);
        vmaDestroyBuffer(myAllocator, myVertexBuffer, myVertexBufferMemory);
        vmaDestroyBuffer(myAllocator, myIndexBuffer, myIndexBufferMemory);
        vmaDestroyImage(myAllocator, myImage, myImageMemory);
        vkDestroyImageView(myDevice, myImageView, nullptr);
        vkDestroySampler(myDevice, mySampler, nullptr);
        
        vmaDestroyAllocator(myAllocator);
        
        vkDestroyCommandPool(myDevice, myCommandPool, nullptr);
        vkDestroyDevice(myDevice, nullptr);
        
        auto vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(myInstance, "vkDestroyDebugReportCallbackEXT");
        assert(vkDestroyDebugReportCallbackEXT != nullptr);
        vkDestroyDebugReportCallbackEXT(myInstance, myDebugCallback, nullptr);
        
        vkDestroySurfaceKHR(myInstance, mySurface, nullptr);
        vkDestroyInstance(myInstance, nullptr);
    }
    
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(myPhysicalDevice, &memProperties);
        
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties))
                return i;
        
        return 0;
    }
    
    static int isDeviceSuitable(VkInstance instance, VkSurfaceKHR surface, VkPhysicalDevice device)
    {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);
        
        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
        
        struct SwapChainInfo
        {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        } swapChainInfo;
        
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &swapChainInfo.capabilities);
        
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount != 0)
        {
            swapChainInfo.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, swapChainInfo.formats.data());
        }
        
        assert(!swapChainInfo.formats.empty());
        
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount != 0)
        {
            swapChainInfo.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, swapChainInfo.presentModes.data());
        }
        
        assert(!swapChainInfo.presentModes.empty());
        
        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
            deviceFeatures.samplerAnisotropy)
        {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
            
            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
            
            for (int i = 0; i < queueFamilies.size(); i++)
            {
                const auto& queueFamily = queueFamilies[i];
                
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
                
                if (queueFamily.queueCount > 0 &&
                    queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT &&
                    presentSupport)
                {
                    return i;
                }
            }
        }
        
        return -1;
    }
    
    enum
    {
        MaxFramesInFlight = 2,
    };
    
    VkInstance myInstance = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT myDebugCallback = VK_NULL_HANDLE;
    VkSurfaceKHR mySurface = VK_NULL_HANDLE;
    VkPhysicalDevice myPhysicalDevice = VK_NULL_HANDLE;
    VkDevice myDevice = VK_NULL_HANDLE;
    VmaAllocator myAllocator = VK_NULL_HANDLE;
    int myQueueFamilyIndex = -1;
    VkQueue myQueue = VK_NULL_HANDLE;
    VkSwapchainKHR mySwapChain = VK_NULL_HANDLE;
    VkFormat mySwapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D mySwapChainExtent = { 0, 0 };
    std::vector<VkImage> mySwapChainImages;
    std::vector<VkImageView> mySwapChainImageViews;
    std::vector<VkFramebuffer> mySwapChainFramebuffers;
    VkRenderPass myRenderPass = VK_NULL_HANDLE;
    VkDescriptorPool myDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout myDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet myDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout myPipelineLayout = VK_NULL_HANDLE;
    VkPipeline myGraphicsPipeline = VK_NULL_HANDLE;
    VkBuffer myVertexBuffer = VK_NULL_HANDLE;
    VmaAllocation myVertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer myIndexBuffer = VK_NULL_HANDLE;
    VmaAllocation myIndexBufferMemory = VK_NULL_HANDLE;
    VkImage myImage = VK_NULL_HANDLE;
    VmaAllocation myImageMemory = VK_NULL_HANDLE;
    VkImageView myImageView = VK_NULL_HANDLE;
    VkSampler mySampler = VK_NULL_HANDLE;
    VkBuffer myUniformBuffer = VK_NULL_HANDLE;
    VmaAllocation myUniformBufferMemory = VK_NULL_HANDLE;
    VkCommandPool myCommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> myCommandBuffers;
    std::vector<VkSemaphore> myImageAvailableSemaphores;
    std::vector<VkSemaphore> myRenderFinishedSemaphores;
    std::vector<VkFence> myInFlightFences;
    size_t myCurrentFrame = 0;
    
    static const Vertex ourVertices[4];
    static const uint16_t ourIndices[6];
};

const Vertex VulkanTutorialApp::ourVertices[] =
{
    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}
};

const uint16_t VulkanTutorialApp::ourIndices[] =
{
    0, 1, 2, 2, 3, 0
};

VulkanTutorialApp* theApp = nullptr;

int vktut2_create(void* view, int width, int height)
{
    assert(theApp == nullptr);
    
    try
    {
        static const char* VK_LOADER_DEBUG_STR = "VK_LOADER_DEBUG";
        if (char* vkLoaderDebug = getenv(VK_LOADER_DEBUG_STR))
            std::cout << VK_LOADER_DEBUG_STR << "=" << vkLoaderDebug << std::endl;
        
        static const char* VK_LAYER_PATH_STR = "VK_LAYER_PATH";
        if (char* vkLayerPath = getenv(VK_LAYER_PATH_STR))
            std::cout << VK_LAYER_PATH_STR << "=" << vkLayerPath << std::endl;
        
        static const char* VK_ICD_FILENAMES_STR = "VK_ICD_FILENAMES";
        if (char* vkIcdFilenames = getenv(VK_ICD_FILENAMES_STR))
            std::cout << VK_ICD_FILENAMES_STR << "=" << vkIcdFilenames << std::endl;
        
        theApp = new VulkanTutorialApp(view, width, height);
        theApp->run();
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void vktut2_destroy()
{
    assert(theApp != nullptr);
    
    delete theApp;
}
