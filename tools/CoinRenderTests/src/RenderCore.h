#ifndef COIN_SNAPSHOT_RENDER_CORE_H
#define COIN_SNAPSHOT_RENDER_CORE_H

#include <array>
#include <string>

#include "SpecLoader.h"

namespace CoinRenderTests {

class RenderCore {
public:
  struct Options {
    std::string scene_file;
    std::string output_file;
    std::string backend = "gl";
    int width = 512;
    int height = 512;
    int msaa_samples = 0;
    bool visible_window = false;
    bool width_override = false;
    bool height_override = false;
    bool msaa_override = false;
    bool spec_mode = false;
    CameraSpec camera;
    ViewportSpec viewport;
    StateSpec state;
    std::array<float, 4> clear_color = {0.2f, 0.2f, 0.2f, 1.0f};
    bool background_set = false;
  };

  RenderCore();
  ~RenderCore();

  bool initialize(const Options& options);
  bool run();

private:
  RenderCore(const RenderCore&) = delete;
  RenderCore& operator=(const RenderCore&) = delete;

  struct Impl;
  Impl* impl_ = nullptr;
};

} // namespace CoinRenderTests

#endif // COIN_SNAPSHOT_RENDER_CORE_H
