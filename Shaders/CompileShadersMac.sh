#!/bin/bash

# Compile vertex shader glslangValidator->glslang
# Seems Mac does not like the extra stuff from glslangValidator
/Users/ole/VulkanSDK/1.4.335.1/macOS/bin/glslc ./Shader.vert -o ./vert.spv

# Compile fragment shader glslangValidator->glslang
# Seems Mac does not like the extra stuff from glslangValidator
/Users/ole/VulkanSDK/1.4.335.1/macOS/bin/glslc ./Shader.frag -o ./frag.spv

# Pause equivalent (wait for user input)
# echo "Press any key to continue..."
# read -n 1 -s
