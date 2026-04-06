#include <Inventor/ModernVulkanBackend.h>
#include <iostream>

int main(int argc, char** argv) {
    try {
        ModernVulkanBackend backend;
        std::cout << "Initializing Vulkan/MoltenVK Backend..." << std::endl;
        backend.init();
        std::cout << "Vulkan Instance and Device successfully created!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Initialization failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
