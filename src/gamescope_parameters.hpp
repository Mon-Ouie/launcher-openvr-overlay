#pragma once

#include <cstddef>
#include <string>
#include <algorithm>
#include <imgui.h>

struct ResolutionPreset {
  const char *name;
  size_t width, height;

  ResolutionPreset(const char *name, size_t width, size_t height):
    name(name), width(width), height(height)
    {}
};

const ResolutionPreset DEFAULT_RESOLUTIONS[] = {
  ResolutionPreset("720p", 1280, 720),
  ResolutionPreset("1080p", 1920, 1080),
  ResolutionPreset("1440p", 2560, 1440),
  ResolutionPreset("4k", 3840, 2160),
  ResolutionPreset("5k", 5120, 2880),
  ResolutionPreset("8k", 7680, 4320),
};

static size_t resolution_index(size_t w, size_t h) {
  return std::find_if(std::begin(DEFAULT_RESOLUTIONS),
                      std::end(DEFAULT_RESOLUTIONS),
                      [w, h](const auto &res) {
                        return res.width == w && res.height == h;
                      }) -
         std::begin(DEFAULT_RESOLUTIONS);
}

struct GamescopeParameters {
  size_t width, height;
  float physical_width;
  std::string extra_options;

  GamescopeParameters():
    width(1920), height(1080), physical_width(2.0)
    {}

  void draw() {
    size_t res_id = resolution_index(width, height);
    size_t num_resolutions = std::size(DEFAULT_RESOLUTIONS);

    const char *resolution_name = "Custom";
    if (res_id < num_resolutions) {
      resolution_name = DEFAULT_RESOLUTIONS[res_id].name;
    }

    if (ImGui::BeginCombo("Resolution", resolution_name)) {
      for (size_t i = 0; i < num_resolutions; i++) {
        if (ImGui::Selectable(DEFAULT_RESOLUTIONS[i].name, res_id == i)) {
          width  = DEFAULT_RESOLUTIONS[i].width;
          height = DEFAULT_RESOLUTIONS[i].height;
        }
      }

      ImGui::Selectable("Custom", res_id == num_resolutions);

      ImGui::EndCombo();
    }

    int width_int = width, height_int = height;
    ImGui::SliderInt("Width [px]", &width_int, 1, 8192);
    ImGui::SliderInt("Height [px]", &height_int, 1, 8192);

    width = width_int;
    height = height_int;

    ImGui::Separator();

    ImGui::SliderFloat("Physical Width [m]", &physical_width, 0.1, 5.0);

    ImGui::Separator();

    char buf[1024] = {0};
    memcpy(
      buf, extra_options.c_str(),
      std::min(sizeof(buf), extra_options.size() + 1));
    buf[sizeof(buf) - 1] = 0;

    ImGui::Text("Additional Gamescope Options");
    ImGui::InputTextMultiline("", buf, sizeof(buf));

    extra_options = buf;
  }
};
