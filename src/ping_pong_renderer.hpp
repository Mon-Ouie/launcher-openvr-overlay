#pragma once

#include <GL/glew.h>
#include <utility>

struct RenderTarget {
  GLuint fbo, tex;

  RenderTarget(size_t w, size_t h) {
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &tex);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
  }

  RenderTarget(RenderTarget &&src): fbo(0), tex(0) {
    std::swap(fbo, src.fbo);
    std::swap(tex, src.tex);
  }

  RenderTarget &operator=(RenderTarget &&src) {
    std::swap(fbo, src.fbo);
    std::swap(tex, src.tex);
    return *this;
  }

  RenderTarget(const RenderTarget &other) = delete;
  RenderTarget &operator=(const RenderTarget &src) = delete;

  ~RenderTarget() {
    if (tex) glDeleteTextures(1, &tex);
    if (fbo) glDeleteFramebuffers(1, &fbo);
  }
};

struct PingPongRenderer {
  bool current_target;
  RenderTarget targets[2];
  size_t w, h;

  PingPongRenderer(size_t w, size_t h):
    current_target(false),
    targets{RenderTarget(w, h), RenderTarget(w, h)},
    w(w), h(h)
    {}

  GLuint current_texture() {
    return targets[current_target ? 0 : 1].tex;
  }

  GLuint current_framebuffer() {
    return targets[current_target ? 1 : 0].fbo;
  }

  void flip() {
    current_target = !current_target;
  }
};
