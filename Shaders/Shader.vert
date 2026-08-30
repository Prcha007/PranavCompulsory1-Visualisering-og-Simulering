#version 450

// Data received from the Vulkan vertex buffer.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

// Data sent to the fragment shader.
layout(location = 0) out vec3 fragColor;

// Camera transformation supplied by Renderer.cpp.
layout(push_constant) uniform PushConstants
{
    mat4 viewProjection;
} pushConstants;

void main()
{
    gl_Position =
        pushConstants.viewProjection *
        vec4(inPosition, 1.0);

    // One-pixel point size works without requiring
    // the Vulkan largePoints device feature.
    gl_PointSize = 1.0;

    fragColor = inColor;
}