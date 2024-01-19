#pragma once

#include <GL/glew.h>
#include <cstddef>
#include <imgui.h>
#include <utility>

#include "icon.hpp"

class GLTexture {
  GLuint texture;
  size_t w, h;

public:
  GLTexture() {
    glGenTextures(1, &texture);
    w = h = 0;
  }

  ~GLTexture() {
    if (texture)
      glDeleteTextures(1, &texture);
  }

  GLTexture(const GLTexture&) = delete;
  GLTexture& operator=(const GLTexture&) = delete;

  GLTexture(GLTexture &&source): texture(0) {
    w = source.w;
    h = source.h;
    std::swap(texture, source.texture);
  }

  GLTexture &operator=(GLTexture &&source) {
    w = source.w;
    h = source.h;
    std::swap(texture, source.texture);
    return *this;
  }

  void load(void *rgba, size_t w, size_t h) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    this->w = w;
    this->h = h;
  }

  void load(const Icon &icon) {
    load((void*)icon.rgba_data.data(), icon.width, icon.height);
  }

  GLuint handle() const { return texture; }

  size_t width() const  { return w; }
  size_t height() const { return h; }

  ImVec2 size_to_fit(ImVec2 max_size) {
    float max_scale_x = max_size.x / w;
    float max_scale_y = max_size.y / h;

    if (max_scale_x < max_scale_y)
      return ImVec2(max_size.x, max_scale_x * h);
    else
      return ImVec2(max_scale_y * w, max_size.y);
  }

  bool draw_button(ImVec2 max_size) {
    return ImGui::ImageButton(texture, size_to_fit(max_size));
  }

  void draw(ImVec2 max_size) {
    return ImGui::Image(texture, size_to_fit(max_size));
  }
};
