#ifndef RENDERER_H
#define RENDERER_H

#include "Logger.h"

#include <QWindow>
#include <vulkan/vulkan_core.h>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Forward declarations
struct SwapChainSupportDetails;
struct QueueFamilyIndices;

// One vertex represents one terrain point.
struct PointVertex
{
    glm::vec3 position;
    glm::vec3 color;

    static VkVertexInputBindingDescription
    getBindingDescription();

    static std::array<VkVertexInputAttributeDescription, 2>
    getAttributeDescriptions();
};

// Camera matrix sent to the vertex shader.
struct PushConstants
{
    glm::mat4 viewProjection{1.0f};
};

class Renderer : public QWindow
{
    Q_OBJECT

public:
    explicit Renderer(QWindow* parent = nullptr);
    ~Renderer();

    void initVulkan();

    void setMainWindow(class MainWindow* mainWindowIn);

protected:
    // Qt event handlers
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;

    // Qt captures key presses
    void keyPressEvent(QKeyEvent* event) override;

private:
    class MainWindow* mMainWindow{nullptr};

    // Base Vulkan variables
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;

    bool framebufferResized = false;

    // Terrain point-cloud data
    std::vector<PointVertex> pointVertices;

    VkBuffer pointVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory pointVertexBufferMemory = VK_NULL_HANDLE;

    // Logger
    class Logger& mLogger;
    LineEnd endl;

    // Vulkan setup
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();

    // Point-cloud functions
    void loadPointCloud(const std::string& filename);
    void createPointVertexBuffer();

    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory);

    uint32_t findMemoryType(
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties);

    void cleanupSwapChain();
    void recreateSwapChain();

    void drawFrame();

    void recordCommandBuffer(
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex);

    VkShaderModule createShaderModule(
        const std::vector<char>& code);

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats);

    VkPresentModeKHR chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& availablePresentModes);

    VkExtent2D chooseSwapExtent(
        const VkSurfaceCapabilitiesKHR& capabilities);

    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);

    QueueFamilyIndices findQueueFamilies(
        VkPhysicalDevice device);

    SwapChainSupportDetails querySwapChainSupport(
        VkPhysicalDevice device);

    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    void populateDebugMessengerCreateInfo(
        VkDebugUtilsMessengerCreateInfoEXT& createInfo);

    void cleanup();
};

#endif // RENDERER_H