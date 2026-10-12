#include "Renderer.h"
#include "Utilities.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <limits>
#include "Logger.h"
#include "MainWindow.h"
#include "unordered_map"

#ifdef _WIN32			// Windows specific includes
#define NOMINMAX		// Prevent Windows.h from defining min/max macros
#include <windows.h>	// For HWND and GetModuleHandle
#include <vulkan/vulkan_win32.h>

#elif defined(__APPLE__)
#include <vulkan/vulkan_macos.h>
#include <vulkan/vulkan_metal.h>
#include <objc/objc.h>
#endif

VkVertexInputBindingDescription PointVertex::getBindingDescription()
{
    VkVertexInputBindingDescription description{};
    description.binding = 0;
    description.stride = sizeof(PointVertex);
    description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return description;
}

std::array<VkVertexInputAttributeDescription, 2> PointVertex::getAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 2> descriptions{};

    descriptions[0].binding = 0;
    descriptions[0].location = 0;
    descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    descriptions[0].offset = offsetof(PointVertex, position);

    descriptions[1].binding = 0;
    descriptions[1].location = 1;
    descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    descriptions[1].offset = offsetof(PointVertex, color);

    return descriptions;
}

Renderer::Renderer(QWindow* parent) : QWindow(parent), mLogger(Logger::getInstance())
{
    setSurfaceType(QWindow::VulkanSurface);		// Seems to be important to get this to work

    //Setup is done in the initVulkan() function
    //size of this window has to be set before initVulkan() is called
    //this is done in the MainWindow() constructor
}

Renderer::~Renderer()
{
    LOG("VulkanRenderer Destructor");
    cleanup();
}

void Renderer::initVulkan()
{
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    loadPointCloud(PATH + "laz_text.txt");
    constructRegularTriangulation(150, 150); //Convert the normalized point cloud into a regular grid of 150 x150
    calculateTriangleNeighbours();// calculates neighbouring triangles and save the lecture-note format.
    saveTriangulation(PATH + "triangulation.txt");
    createTriangleIndexBuffer();

    createPointVertexBuffer();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
}

void Renderer::loadPointCloud(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("failed to open point-cloud file: " + filename);

    std::size_t declaredPointCount = 0;
    if (!(file >> declaredPointCount) || declaredPointCount == 0)
        throw std::runtime_error("invalid point count in point-cloud file");

    struct SourcePoint
    {
        double x;
        double y;
        double z;
    };

    std::vector<SourcePoint> sourcePoints;
    sourcePoints.reserve(declaredPointCount);

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double minZ = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    double maxZ = std::numeric_limits<double>::lowest();

    for (std::size_t i = 0; i < declaredPointCount; ++i)
    {
        SourcePoint point{};
        if (!(file >> point.x >> point.y >> point.z))
            throw std::runtime_error("point-cloud file ended before the declared point count");

        sourcePoints.push_back(point);
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        minZ = std::min(minZ, point.z);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
        maxZ = std::max(maxZ, point.z);
    }

    const double centerX = (minX + maxX) * 0.5;
    const double centerY = (minY + maxY) * 0.5;
    const double horizontalSize = std::max(maxX - minX, maxY - minY);
    const double scale = horizontalSize > 0.0 ? 20.0 / horizontalSize : 1.0;
    const double elevationRange = std::max(maxZ - minZ, 1.0);

    pointVertices.clear();
    pointVertices.reserve(sourcePoints.size());

    for (const SourcePoint& point : sourcePoints)
    {
        PointVertex vertex{};

        // Geographic X remains X. Geographic elevation Z becomes Vulkan Y.
        // Geographic Y becomes negative Vulkan Z because this renderer is Y-up.
        vertex.position = glm::vec3(
            static_cast<float>((point.x - centerX) * scale),
            static_cast<float>((point.z - minZ) * scale),
            static_cast<float>(-(point.y - centerY) * scale));

        const float height = static_cast<float>((point.z - minZ) / elevationRange);
        vertex.color = glm::vec3(
            0.10f + 0.80f * height,
            0.35f + 0.55f * height,
            0.95f - 0.70f * height);

        pointVertices.push_back(vertex);
    }

    LOGP("Loaded %u terrain points", static_cast<unsigned int>(pointVertices.size()));
}

void Renderer::constructRegularTriangulation(uint32_t gridWidth,uint32_t gridDepth)
{
    if (gridWidth < 2 || gridDepth < 2)
        throw std::runtime_error("triangulation grid must be at least 2 by 2");

    if (pointVertices.empty())
        throw std::runtime_error("cannot triangulate an empty point cloud");

    // Keep a copy because pointVertices will become the grid geometry.
    const std::vector<PointVertex> sourcePoints = pointVertices;

    float minX = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxZ = std::numeric_limits<float>::lowest();

    for (const PointVertex& point : sourcePoints)
    {
        minX = std::min(minX, point.position.x);
        minZ = std::min(minZ, point.position.z);
        maxX = std::max(maxX, point.position.x);
        maxZ = std::max(maxZ, point.position.z);
    }

    const float sizeX = maxX - minX;
    const float sizeZ = maxZ - minZ;

    if (sizeX <= 0.0f || sizeZ <= 0.0f)
        throw std::runtime_error("point cloud has an invalid horizontal size");

    struct GridCell
    {
        double heightSum = 0.0;
        uint32_t pointCount = 0;
    };

    const std::size_t cellCount =static_cast<std::size_t>(gridWidth) * gridDepth;

    std::vector<GridCell> cells(cellCount);

    // Place every point into one regular grid cell.
    for (const PointVertex& point : sourcePoints)
    {
        const float normalizedX =(point.position.x - minX) / sizeX;

        const float normalizedZ =(point.position.z - minZ) / sizeZ;

        uint32_t column = std::min(static_cast<uint32_t>(normalizedX * static_cast<float>(gridWidth)),gridWidth - 1);
        uint32_t row = std::min(static_cast<uint32_t>(normalizedZ * static_cast<float>(gridDepth)),gridDepth - 1);

        GridCell& cell =
            cells[static_cast<std::size_t>(row) * gridWidth + column];

        cell.heightSum += point.position.y;
        ++cell.pointCount;
    }

    const uint32_t invalidIndex =std::numeric_limits<uint32_t>::max();

    // Maps grid cells to indices in pointVertices.
    std::vector<uint32_t> gridVertexIndices(
        cellCount,
        invalidIndex);

    pointVertices.clear();
    terrainTriangles.clear();
    triangleIndices.clear();

    const float cellSizeX =
        sizeX / static_cast<float>(gridWidth);

    const float cellSizeZ =
        sizeZ / static_cast<float>(gridDepth);

    // Create one vertex at the average height of every occupied cell.
    for (uint32_t row = 0; row < gridDepth; ++row)
    {
        for (uint32_t column = 0; column < gridWidth; ++column)
        {
            const std::size_t cellIndex =
                static_cast<std::size_t>(row) * gridWidth + column;

            const GridCell& cell = cells[cellIndex];

            if (cell.pointCount == 0)
                continue;

            const float averageHeight =
                static_cast<float>(
                    cell.heightSum /
                    static_cast<double>(cell.pointCount));

            PointVertex vertex{};

            vertex.position = glm::vec3(
                minX + (static_cast<float>(column) + 0.5f) * cellSizeX,
                averageHeight,
                minZ + (static_cast<float>(row) + 0.5f) * cellSizeZ);

            // The input data is already normalized to approximately 20 units.
            const float height =
                glm::clamp(averageHeight / 20.0f, 0.0f, 1.0f);

            vertex.color = glm::vec3(
                0.10f + 0.80f * height,
                0.35f + 0.55f * height,
                0.95f - 0.70f * height);

            gridVertexIndices[cellIndex] =
                static_cast<uint32_t>(pointVertices.size());

            pointVertices.push_back(vertex);
        }
    }

    // Split every complete grid square into two triangles.
    for (uint32_t row = 0; row + 1 < gridDepth; ++row)
    {
        for (uint32_t column = 0;
             column + 1 < gridWidth;
             ++column)
        {
            const uint32_t topLeft =
                gridVertexIndices[
                    static_cast<std::size_t>(row) * gridWidth +
                    column];

            const uint32_t topRight =
                gridVertexIndices[
                    static_cast<std::size_t>(row) * gridWidth +
                    column + 1];

            const uint32_t bottomLeft =
                gridVertexIndices[
                    static_cast<std::size_t>(row + 1) * gridWidth +
                    column];

            const uint32_t bottomRight =
                gridVertexIndices[
                    static_cast<std::size_t>(row + 1) * gridWidth +
                    column + 1];

            // Do not create triangles over missing terrain cells.
            if (topLeft == invalidIndex ||
                topRight == invalidIndex ||
                bottomLeft == invalidIndex ||
                bottomRight == invalidIndex)
            {
                continue;
            }

            TerrainTriangle first{};
            first.vertices[0] = topLeft;
            first.vertices[1] = bottomLeft;
            first.vertices[2] = topRight;
            terrainTriangles.push_back(first);

            TerrainTriangle second{};
            second.vertices[0] = topRight;
            second.vertices[1] = bottomLeft;
            second.vertices[2] = bottomRight;
            terrainTriangles.push_back(second);
        }
    }

    // Create the flat index list required by Vulkan.
    triangleIndices.reserve(terrainTriangles.size() * 3);

    for (const TerrainTriangle& triangle : terrainTriangles)
    {
        triangleIndices.push_back(triangle.vertices[0]);
        triangleIndices.push_back(triangle.vertices[1]);
        triangleIndices.push_back(triangle.vertices[2]);
    }

    if (terrainTriangles.empty())
        throw std::runtime_error(
            "regular triangulation produced no triangles");

    LOGP(
        "Regular triangulation: %u vertices",
        static_cast<unsigned int>(pointVertices.size()));

    LOGP(
        "Regular triangulation: %u triangles",
        static_cast<unsigned int>(terrainTriangles.size()));
}

void Renderer::calculateTriangleNeighbours()
{
    struct EdgeOwner
    {
        std::size_t triangleIndex;
        uint32_t edgeIndex;
    };

    std::unordered_map<uint64_t, EdgeOwner> edgeOwners;

    auto makeEdgeKey = [](uint32_t first, uint32_t second)
    {
        const uint32_t smaller = std::min(first, second);
        const uint32_t larger = std::max(first, second);

        return
            (static_cast<uint64_t>(smaller) << 32) |
            static_cast<uint64_t>(larger);
    };

    for (std::size_t triangleIndex = 0;
         triangleIndex < terrainTriangles.size();
         ++triangleIndex)
    {
        TerrainTriangle& triangle =
            terrainTriangles[triangleIndex];

        // Reset all neighbours to boundary values.
        triangle.neighbours[0] = -1;
        triangle.neighbours[1] = -1;
        triangle.neighbours[2] = -1;

        // Neighbour 0 is across the edge opposite vertex 0
        // Neighbour 1 is across the edge opposite vertex 1
        // Neighbour 2 is across the edge opposite vertex 2
        const uint32_t edgeVertices[3][2] =
            {
                {triangle.vertices[1], triangle.vertices[2]},
                {triangle.vertices[2], triangle.vertices[0]},
                {triangle.vertices[0], triangle.vertices[1]}
            };

        for (uint32_t edgeIndex = 0;
             edgeIndex < 3;
             ++edgeIndex)
        {
            const uint64_t edgeKey = makeEdgeKey(
                edgeVertices[edgeIndex][0],
                edgeVertices[edgeIndex][1]);

            const auto existingEdge =
                edgeOwners.find(edgeKey);

            if (existingEdge == edgeOwners.end())
            {
                edgeOwners.emplace(
                    edgeKey,
                    EdgeOwner{triangleIndex, edgeIndex});
            }
            else
            {
                const EdgeOwner owner = existingEdge->second;

                triangle.neighbours[edgeIndex] =
                    static_cast<int32_t>(owner.triangleIndex);

                terrainTriangles[owner.triangleIndex]
                    .neighbours[owner.edgeIndex] =
                    static_cast<int32_t>(triangleIndex);
            }
        }
    }

    LOG("Calculated triangle neighbours");
}

void Renderer::saveTriangulation(
    const std::string& filename)
{
    std::ofstream file(filename);

    if (!file.is_open())
        throw std::runtime_error(
            "failed to create triangulation file: " + filename);

    // triangle count
    // vertex0 vertex1 vertex2 neighbour0 neighbour1 neighbour2
    file << terrainTriangles.size() << '\n';

    for (const TerrainTriangle& triangle : terrainTriangles)
    {
        file
            << triangle.vertices[0] << ' '
            << triangle.vertices[1] << ' '
            << triangle.vertices[2] << ' '
            << triangle.neighbours[0] << ' '
            << triangle.neighbours[1] << ' '
            << triangle.neighbours[2] << '\n';
    }

    if (!file)
        throw std::runtime_error(
            "failed while writing triangulation file: " + filename);

    LOGP(
        "Saved %u triangles to triangulation.txt",
        static_cast<unsigned int>(terrainTriangles.size()));
}

void Renderer::createPointVertexBuffer()
{
    if (pointVertices.empty())
        throw std::runtime_error("cannot create an empty point-cloud vertex buffer");

    const VkDeviceSize bufferSize =
        sizeof(PointVertex) * static_cast<VkDeviceSize>(pointVertices.size());

    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        pointVertexBuffer,
        pointVertexBufferMemory);

    void* mappedMemory = nullptr;
    if (vkMapMemory(device, pointVertexBufferMemory, 0, bufferSize, 0, &mappedMemory) != VK_SUCCESS)
        throw std::runtime_error("failed to map point-cloud vertex-buffer memory");

    std::memcpy(mappedMemory, pointVertices.data(), static_cast<std::size_t>(bufferSize));
    vkUnmapMemory(device, pointVertexBufferMemory);
}

void Renderer::createTriangleIndexBuffer()
{
    if (triangleIndices.empty())
        throw std::runtime_error(
            "cannot create an empty triangle index buffer");

    const VkDeviceSize bufferSize =
        sizeof(uint32_t) *
        static_cast<VkDeviceSize>(triangleIndices.size());

    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        triangleIndexBuffer,
        triangleIndexBufferMemory);

    void* mappedMemory = nullptr;

    if (vkMapMemory(
            device,
            triangleIndexBufferMemory,
            0,
            bufferSize,
            0,
            &mappedMemory) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "failed to map triangle index-buffer memory");
    }

    std::memcpy(
        mappedMemory,
        triangleIndices.data(),
        static_cast<std::size_t>(bufferSize));

    vkUnmapMemory(
        device,
        triangleIndexBufferMemory);

    LOGP(
        "Created index buffer with %u indices",
        static_cast<unsigned int>(triangleIndices.size()));
}

void Renderer::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
        throw std::runtime_error("failed to create point-cloud vertex buffer");

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocationInfo.allocationSize = memoryRequirements.size;
    allocationInfo.memoryTypeIndex = findMemoryType(
        memoryRequirements.memoryTypeBits,
        properties);

    if (vkAllocateMemory(device, &allocationInfo, nullptr, &bufferMemory) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate point-cloud vertex-buffer memory");

    if (vkBindBufferMemory(device, buffer, bufferMemory, 0) != VK_SUCCESS)
        throw std::runtime_error("failed to bind point-cloud vertex-buffer memory");
}

uint32_t Renderer::findMemoryType(
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        const bool supported = (typeFilter & (1u << i)) != 0;
        const bool hasProperties =
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;

        if (supported && hasProperties)
            return i;
    }

    throw std::runtime_error("failed to find suitable Vulkan memory type");
}

void Renderer::cleanupSwapChain()
{
    for (auto framebuffer : swapChainFramebuffers)
    {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    for (auto imageView : swapChainImageViews)
    {
        vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(device, swapChain, nullptr);
}

void Renderer::cleanup()
{
    cleanupSwapChain();

    if (pointVertexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device, pointVertexBuffer, nullptr);

    if (pointVertexBufferMemory != VK_NULL_HANDLE)
        vkFreeMemory(device, pointVertexBufferMemory, nullptr);

    if (triangleIndexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(device,triangleIndexBuffer,nullptr);

    if (triangleIndexBufferMemory != VK_NULL_HANDLE)
        vkFreeMemory(device,triangleIndexBufferMemory,nullptr);

    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    vkDestroyRenderPass(device, renderPass, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(device, commandPool, nullptr);

    vkDestroyDevice(device, nullptr);

    if (enableValidationLayers)
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);

    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}

void Renderer::recreateSwapChain() {
    int width = 0, height = 0;
    // glfwGetFramebufferSize(window, &width, &height);
    // while (width == 0 || height == 0) {
    // glfwGetFramebufferSize(window, &width, &height);
    // glfwWaitEvents();
    // }

    vkDeviceWaitIdle(device);

    cleanupSwapChain();

    createSwapChain();
    createImageViews();
    createFramebuffers();
}

void Renderer::createInstance()
{
#ifdef _WIN32
    if (enableValidationLayers && !checkValidationLayerSupport())
        throw std::runtime_error("validation layers requested, but not available!");

#elif defined(__APPLE__)    //Validation layers on Mac not working atm
    enableValidationLayers = false;
    LOG("Mac has no support for Validation layers atm.");
#endif

    //Information about the application itself
    //Mostly used for debugging
    //Needed in VkInstanceCreateInfo
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hello Triangle";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // Not using GLFW, so need to do this another way
    std::vector<const char*> extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
        throw std::runtime_error("failed to create instance!");
    else
        LOG("createInstance successful");
}

void Renderer::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

void Renderer::setupDebugMessenger()
{
    if (!enableValidationLayers)
        return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS)
        throw std::runtime_error("failed to set up debug messenger!");
    else
        LOG("setupDebugMessenger successful");
}

void Renderer::createSurface()
{
    //Qt version, since we are not using GLFW:
#ifdef Q_OS_WIN
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = reinterpret_cast<HWND>(this->winId());
    createInfo.hinstance = GetModuleHandle(nullptr);

    if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create a surface!");
    else
        LOG("\nSuccessfully created a surface!");

#elif defined(__APPLE__)
    void* nsview = reinterpret_cast<void*>(this->winId());

    extern void* setupMetalLayerForView(void* nsview);
    CAMetalLayer* metalLayer =
        (CAMetalLayer*)setupMetalLayerForView(nsview);

    // setupMetalLayerForView(nsview);

    VkMetalSurfaceCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    // createInfo.pNext = NULL;
    // createInfo.flags = 0;
    createInfo.pLayer = metalLayer; //nsview;

    //std::this_thread::sleep_for(std::chrono::milliseconds(500));  // Small delay to let Metal settle

    // This must happen AFTER the layer is successfully set up
    if (vkCreateMetalSurfaceEXT(instance, &createInfo, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("Failed to create a surface!");
    else
        LOG("Successfully created a surface!");
#endif
}

void Renderer::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0)
        throw std::runtime_error("failed to find GPUs with Vulkan support!");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // These lines are a little extended to print out device info for all Vulkan GPUs:
    // Print the names of the physical devices and check if they has required extensions
    int i{ 0 };
    for (const auto& device : devices)
    {
        LOGH("Finding physical devices:");
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);
        LOGP("Physical Device No.%d, Name: %s", i, deviceProperties.deviceName);
        LOGP("   - Max Vulkan API support: %u.%u.%u",
             VK_VERSION_MAJOR(deviceProperties.apiVersion),
             VK_VERSION_MINOR(deviceProperties.apiVersion),
             VK_VERSION_PATCH(deviceProperties.apiVersion));

        if (isDeviceSuitable(device))
            LOGP("   - %s is a suitable device!", deviceProperties.deviceName);

        else
            LOGP("   - %s is not suitable device", deviceProperties.deviceName);

        i++;
    }

    //This will automatically select the first device that is suitable:
    for (const auto& device : devices)
    {
        if (isDeviceSuitable(device))
        {
            physicalDevice = device;
            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(device, &deviceProperties);
            LOGW("Selected physical device:");
            LOGP("   - %s", deviceProperties.deviceName);
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("failed to find a suitable GPU!");
    else
        LOG("Found suitable GPU");
}

void Renderer::createLogicalDevice()
{
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    deviceFeatures.fillModeNonSolid = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
        throw std::runtime_error("failed to create logical device!");
    else
        LOG("Created logical device");

    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
}

void Renderer::createSwapChain()
{
    //Get swap chain details so we can pick best settings
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

    //1. Choose best surface format
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    //2. Choose best presentation mode
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    //3. Choose swapchain image resolution
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    //How many images are in the swap chain? Recommended to request at least 1 more than the minimum
    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    //Check if image count is greater than the max allowed, clamp to max allowed
    //0 is a special value for maxImageCount that means there is no maximum
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount)
        imageCount = swapChainSupport.capabilities.maxImageCount;

    //Creation information for the swap chain
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;

    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    //Get queue family indices
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS)
        throw std::runtime_error("failed to create swap chain!");
    else
        LOG("Created swap chain");

    //Get swap chain images (first count, then values)
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

    LOGP(" - swapChainImageCount %d:", imageCount);

    //Store for later use
    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
}

void Renderer::createImageViews()
{
    swapChainImageViews.resize(swapChainImages.size());

    for (size_t i = 0; i < swapChainImages.size(); i++)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to create image views!");
        else
            LOG("Created image views!");
    }
}

void Renderer::createRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("failed to create render pass!");
    else
        LOG("create render pass!");
}

void Renderer::createGraphicsPipeline()
{
    auto vertShaderCode = readFile(PATH + "Shaders/vert.spv");
    auto fragShaderCode = readFile(PATH + "Shaders/frag.spv");

    LOGH("Vertex shader:");
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    LOGH("Fragment shader:");
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    const auto bindingDescription = PointVertex::getBindingDescription();
    const auto attributeDescriptions = PointVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth = 1.0f;                    // set to wider lines if using VK_POLYGON_MODE_LINE
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // VK_FRONT_FACE_COUNTER_CLOCKWISE
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("failed to create pipeline layout!");
    else
        LOG("create pipeline layout!");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS)
        throw std::runtime_error("failed to create graphics pipeline!");
    else
        LOG("create graphics pipeline!");

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void Renderer::createFramebuffers()
{
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++)
    {
        VkImageView attachments[] = {
            swapChainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to create framebuffer!");
        else
            LOG("create framebuffer!");
    }
}

void Renderer::createCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        throw std::runtime_error("failed to create command pool!");
    else
        LOG("create command pool!");
}

void Renderer::createCommandBuffers()
{
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) commandBuffers.size();

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate command buffers!");
    else
        LOG("allocate command buffers!");
}


void Renderer::recordCommandBuffer(
    VkCommandBuffer commandBuffer,
    uint32_t imageIndex)

{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("failed to begin recording command buffer!");

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent;

    VkClearValue clearColor = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) swapChainExtent.width;
    viewport.height = (float) swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkBuffer vertexBuffers[] = {pointVertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(commandBuffer,triangleIndexBuffer,0,VK_INDEX_TYPE_UINT32);

    const float aspectRatio =
        static_cast<float>(swapChainExtent.width) /
        static_cast<float>(swapChainExtent.height);

    // Camera positioned above and in front of the normalized terrain.
    const glm::vec3 cameraPosition(0.0f, 14.0f, 24.0f);
    const glm::vec3 cameraTarget(0.0f, 2.5f, 0.0f);
    const glm::vec3 cameraUp(0.0f, 1.0f, 0.0f);

    const glm::mat4 view = glm::lookAt(
        cameraPosition,
        cameraTarget,
        cameraUp);

    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f),
        aspectRatio,
        0.1f,
        100.0f);

    // GLM uses an inverted Y convention compared with Vulkan clip space.
    projection[1][1] *= -1.0f;

    PushConstants pushConstants{};
    pushConstants.viewProjection = projection * view;

    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(PushConstants),
        &pushConstants);

    vkCmdDrawIndexed(
        commandBuffer,
        static_cast<uint32_t>(triangleIndices.size()),
        1,
        0,
        0,
        0);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to record command buffer!");
    // else
    // LOG("recorded command buffer!"); //spams the log
}

void Renderer::createSyncObjects()
{
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
        else
            LOGP("create synchronization objects for frame %d", i);
    }
}

void Renderer::drawFrame()
{
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    vkResetCommandBuffer(commandBuffers[currentFrame], /*VkCommandBufferResetFlagBits*/ 0);
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS)
        throw std::runtime_error("failed to submit draw command buffer!");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;

    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized)
    {
        framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

VkShaderModule Renderer::createShaderModule(const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        throw std::runtime_error("failed to create shader module!");
    else
        LOG("create shader module");

    return shaderModule;
}

VkSurfaceFormatKHR Renderer::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
    for (const auto& availableFormat : availableFormats)
    {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR Renderer::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
{
    for (const auto& availablePresentMode : availablePresentModes)
    {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            return availablePresentMode;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Renderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;
    else
    {
        //If value can vary, need to set manually
        //Get the size of the render window - Qt style
        //GLFW uses glfwGetFramebufferSize(window, &width, &height);
        int width = this->width();
        int height = this->height();

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

// Query the supported swap chain details
SwapChainSupportDetails Renderer::querySwapChainSupport(VkPhysicalDevice device)
{
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}


bool Renderer::isDeviceSuitable(VkPhysicalDevice device)
{
    QueueFamilyIndices indices = findQueueFamilies(device);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    // Check for basic swapchain support (formats and present modes)
    bool swapChainAdequate = false;
    if (extensionsSupported)
    {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

bool Renderer::checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

QueueFamilyIndices Renderer::findQueueFamilies(VkPhysicalDevice device)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies)
    {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphicsFamily = i;

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if (presentSupport)
            indices.presentFamily = i;

        // MacOS MoltenVK automatically supports presentation on the same queue as graphics
        if (indices.isComplete())
            break;

        i++;
    }

    return indices;
}



std::vector<const char *> Renderer::getRequiredExtensions()
{
    //To finsih of the createInfo struct, we need to set up some more data
    //Since we don't use GLFW, this part is a little more verbose
    //GLFW hides parts of this.

    //Create a vector to hold the extensions
    std::vector<const char*> extensions = std::vector<const char*>();

    uint32_t extensionCount{ 0 };
    VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    if (result != VK_SUCCESS)
        qWarning("Failed to enumerate Vulkan instance extensions!");
    else
        LOGP("\nVulkan instance extension count: %u", extensionCount);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

    // Log all available extensions
    LOG("Available Vulkan extensions:");
    for (const auto& ext : availableExtensions)
        LOG(ext.extensionName);

    extensions.push_back("VK_KHR_surface");

#ifdef _WIN32
    extensions.push_back("VK_KHR_win32_surface");	// Only on Windows

#elif defined(Q_OS_LINUX)
    instanceExtensions.push_back("VK_KHR_xcb_surface");		// or xlib_surface, depending on your Qt build

#elif defined(__APPLE__)
    extensions.push_back("VK_MVK_macos_surface");
    extensions.push_back("VK_MVK_moltenvk");
    //extensions.push_back("VK_EXT_METAL_SURFACE_EXTENSION_NAME");
#endif

    // If validation is enabled, add extension to report validation debug info
    if (enableValidationLayers)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return extensions;
}

bool Renderer::checkValidationLayerSupport()
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers)
    {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers)
        {
            if (strcmp(layerName, layerProperties.layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }

        if (!layerFound)
            return false;
    }

    return true;
}

VkBool32 Renderer::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                 VkDebugUtilsMessageTypeFlagsEXT messageType,
                                 const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                 void *pUserData)
{
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}

// ***** Qt specific functions: *****

void Renderer::exposeEvent(QExposeEvent *event)
{
//Called also on resize
#ifdef _WIN32   // These lines does not work on macOS so need this test
    LOG("exposeEvent called");
    if (isExposed())
        drawFrame();			//actual drawing
#endif
}

void Renderer::resizeEvent(QResizeEvent *event)
{
    LOGH("resizeEvent called");
    if (isExposed())
        drawFrame();			//actual drawing
}

bool Renderer::event(QEvent *event)
{
    if (event->type() == QEvent::UpdateRequest && isExposed())
    {
        drawFrame();
        requestUpdate();    // Tells Qt to draw next frame
        return true;
    }

    return QWindow::event(event);
}

void Renderer::keyPressEvent(QKeyEvent *event)
{
    // Just sending the keypresses back to MainWindow to handle all in one place
    mMainWindow->keyPressEvent(event);
}

void Renderer::setMainWindow(MainWindow *mainWindowIn)
{
    mMainWindow = mainWindowIn;
}
