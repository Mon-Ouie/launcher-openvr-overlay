#pragma once

#include <optional>
#include <vector>
#include <cstdint>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

struct Icon {
  std::vector<uint32_t> rgba_data;
  size_t width, height;

  Icon(std::vector<uint32_t> rgba_data, size_t width, size_t height):
    rgba_data(std::move(rgba_data)),
    width(width),
    height(height)
    {}

  static std::optional<Icon> load(const std::optional<std::string> &path) {
    if (!path)
      return std::nullopt;

    int w, h, comp;
    void *data = stbi_load(path->c_str(), &w, &h, &comp, 4);
    if (data) {
      std::vector<uint32_t> icon_data(w * h);
      memcpy(icon_data.data(), data, w * h * 4);

      free(data);

      return Icon(std::move(icon_data), w, h);
    }

    return std::nullopt;
  }
};
