find_package(glfw3 QUIET)

if(glfw3_FOUND OR TARGET glfw)
    add_executable(vulkan-gui-test shadow-ecs-vk/vulkan-gui-test.cpp)
    target_link_libraries(vulkan-gui-test Coin glfw ${Vulkan_LIBRARIES})
    target_compile_features(vulkan-gui-test PRIVATE cxx_std_17)
    
    # Optional test for raw compute test
    #add_executable(vulkan-test shadow-ecs-vk/vulkan-test.cpp)
    #target_link_libraries(vulkan-test Coin glfw)
    #target_compile_features(vulkan-test PRIVATE cxx_std_17)
endif()

add_executable(benchmark-ecs-test benchmark-ecs/benchmark-test.cpp)
target_link_libraries(benchmark-ecs-test Coin ${Vulkan_LIBRARIES})
target_compile_features(benchmark-ecs-test PRIVATE cxx_std_17)
if(glfw3_FOUND OR TARGET glfw)
    target_link_libraries(benchmark-ecs-test glfw)
endif()

# We don't add them to add_test() unless they are headless unit tests.
