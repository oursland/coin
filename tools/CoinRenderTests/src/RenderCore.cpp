#include "RenderCore.h"

#include "GLFWBackend.h"

#include <Inventor/SoDB.h>
#include <Inventor/SoInput.h>
#include <Inventor/SbLinear.h>
#include <Inventor/SbViewportRegion.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/system/gl.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include <stb_image_write.h>

namespace CoinRenderTests {

struct RenderCore::Impl {
  Options options;
  GLFWBackend backend;
  SoSeparator* scene_root = nullptr;
  std::vector<uint8_t> pixels;
};

RenderCore::RenderCore() : impl_(new Impl()) {}

RenderCore::~RenderCore() {
  if (impl_) {
    if (impl_->scene_root) {
      impl_->scene_root->unref();
      impl_->scene_root = nullptr;
    }
    delete impl_;
  }
}

bool RenderCore::initialize(const Options& options) {
  if (!impl_) {
    impl_ = new Impl();
  }

  impl_->options = options;

  if (options.scene_file.empty() || options.output_file.empty()) {
    std::cerr << "Scene file and output path are required." << std::endl;
    return false;
  }

  GLFWBackendConfig config;
  config.width = options.width;
  config.height = options.height;
  config.msaa_samples = options.msaa_samples;
  config.gles = (options.backend == "gles");
  config.visible = options.visible_window;
  config.offscreen = !options.visible_window;
  if (config.gles) {
    config.gl_major = 2;
    config.gl_minor = 0;
  } else {
    config.gl_major = 3;
    config.gl_minor = 3;
  }

  if (!impl_->backend.init(config)) {
    return false;
  }

  SoInput input;
  if (!input.openFile(options.scene_file.c_str())) {
    std::cerr << "Unable to open scene file: " << options.scene_file << std::endl;
    return false;
  }

  SoNode* loaded = SoDB::readAll(&input);
  if (!loaded) {
    std::cerr << "Failed to parse scene file: " << options.scene_file << std::endl;
    return false;
  }

  loaded->ref();
  SoSearchAction search;
  search.setType(SoCamera::getClassTypeId());
  search.setSearchingAll(TRUE);
  search.setInterest(SoSearchAction::ALL);
  search.apply(loaded);
  const int scene_camera_count = search.getPaths().getLength();
  if (scene_camera_count > 0 && options.spec_mode) {
    std::cerr << "[coin-snapshot] Spec camera overriding " << scene_camera_count
              << " camera(s) defined inside the scene; they are ignored.\n";
  }

  impl_->scene_root = new SoSeparator;
  impl_->scene_root->ref();

  SoCamera* camera = nullptr;
  if (options.spec_mode && options.camera.type == CameraSpec::Type::Orthographic) {
    camera = new SoOrthographicCamera;
    static_cast<SoOrthographicCamera*>(camera)
        ->height.setValue(options.camera.fov_deg);
  } else {
    auto* perspective = new SoPerspectiveCamera;
    const float radians = options.camera.fov_deg * static_cast<float>(M_PI / 180.0);
    perspective->heightAngle.setValue(radians);
    camera = perspective;
  }
  camera->position.setValue(options.camera.position);
  camera->pointAt(options.camera.target);
  camera->nearDistance.setValue(options.camera.near);
  camera->farDistance.setValue(options.camera.far);
  impl_->scene_root->addChild(camera);

  auto light = new SoDirectionalLight;
  light->direction.setValue(SbVec3f(-0.5f, -1.0f, -0.5f));
  impl_->scene_root->addChild(light);
  impl_->scene_root->addChild(loaded);
  loaded->unref();

  return true;
}

bool RenderCore::run() {
  if (!impl_ || !impl_->scene_root) {
    return false;
  }

  impl_->backend.bindFramebuffer();
  glViewport(0, 0, impl_->options.width, impl_->options.height);
  if (impl_->options.state.depth_test) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  if (impl_->options.state.blend) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  } else {
    glDisable(GL_BLEND);
  }
  if (impl_->options.state.cull_face) {
    glEnable(GL_CULL_FACE);
  } else {
    glDisable(GL_CULL_FACE);
  }
  if (impl_->options.state.srgb) {
    glEnable(GL_FRAMEBUFFER_SRGB);
  } else {
    glDisable(GL_FRAMEBUFFER_SRGB);
  }
  const auto& bg = impl_->options.background_set ? impl_->options.clear_color
                                                  : std::array<float, 4>{0.2f, 0.2f, 0.2f, 1.0f};
  glClearColor(bg[0], bg[1], bg[2], bg[3]);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  SoGLRenderAction action(SbViewportRegion(impl_->options.width, impl_->options.height));
  action.apply(impl_->scene_root);
  if (!impl_->backend.usesOffscreen()) {
    impl_->backend.swapBuffers();
  }

  impl_->pixels.clear();
  if (!impl_->backend.readPixels(impl_->pixels)) {
    std::cerr << "Failed to read pixels from the framebuffer." << std::endl;
    return false;
  }

  const int stride = impl_->options.width * 4;
  const int height = impl_->options.height;
  for (int row = 0; row < height / 2; ++row) {
    auto* top = impl_->pixels.data() + static_cast<size_t>(row) * stride;
    auto* bottom = impl_->pixels.data() + static_cast<size_t>(height - row - 1) * stride;
    for (int i = 0; i < stride; ++i) {
      std::swap(top[i], bottom[i]);
    }
  }

  if (!stbi_write_png(impl_->options.output_file.c_str(),
                      impl_->options.width,
                      impl_->options.height,
                      4,
                      impl_->pixels.data(),
                      stride)) {
    std::cerr << "Failed to write PNG output: " << impl_->options.output_file << std::endl;
    return false;
  }

  impl_->scene_root->unref();
  impl_->scene_root = nullptr;

  std::cout << "Rendered scene to " << impl_->options.output_file << '\n';
  return true;
}

} // namespace CoinRenderTests
