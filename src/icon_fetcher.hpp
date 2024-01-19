#pragma once

#include "gl_texture.hpp"
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <giomm.h>

extern "C" {
#include "nkutils-xdg-theme.h"
}

/* Who would define C as 1? */
#ifdef C
#undef C
#endif

#include <tbb/concurrent_queue.h>

static const gchar *const THEMES[] = {
    "default", nullptr
};

static const gchar *const FALLBACK_THEMES[] = {
    "Adwaita", "gnome", "oxygen", nullptr
};

class IconFetcher {
  NkXdgThemeContext *context;

  std::unordered_map<std::string, size_t> name_to_id;

  std::vector<std::optional<std::string>> path_cache;
  std::vector<std::shared_future<std::optional<Icon>>> icon_cache;
  std::vector<std::optional<GLTexture>> texture_cache;

  std::mutex mutex;

  tbb::concurrent_bounded_queue<
    std::optional<std::pair<std::promise<std::optional<Icon>>,
                            std::optional<std::string>>>
    > job_queue;
  std::jthread worker;

public:
  IconFetcher():
    worker([this](std::stop_token token){ load_icons_from_queue(token); })
    {
      context = nk_xdg_theme_context_new(FALLBACK_THEMES, nullptr);
      nk_xdg_theme_preload_themes_icon(context, THEMES);
    }

  ~IconFetcher() {
    nk_xdg_theme_context_free(context);
    job_queue.emplace(std::nullopt);
  }

  size_t request_id(std::string &&name) {
    std::lock_guard<std::mutex> lock(mutex);
    auto [it, inserted] = name_to_id.emplace(name, path_cache.size());
    if (!inserted)
      return it->second;

    if (std::filesystem::path(name).is_absolute()) {
      path_cache.emplace_back(name);
    }
    else {
      gchar *path = nk_xdg_theme_get_icon(context, THEMES, nullptr,
                                          name.c_str(), 512, 1, false);

      auto &path_obj = path_cache.emplace_back();
      if (path)
        path_obj = Glib::convert_const_gchar_ptr_to_stdstring(path);
    }

    if (icon_cache.capacity() < path_cache.capacity())
      icon_cache.reserve(path_cache.capacity());
    icon_cache.resize(path_cache.size());

    if (texture_cache.capacity() < path_cache.capacity())
      texture_cache.reserve(path_cache.capacity());
    texture_cache.resize(path_cache.size());

    return it->second;
  }

  std::optional<const Icon*> fetch_icon(size_t id) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!icon_cache[id].valid()) {
      std::promise<std::optional<Icon>> promise;
      icon_cache[id] = promise.get_future();
      job_queue.emplace(std::make_pair(std::move(promise), path_cache[id]));
    }

    if (icon_cache[id].wait_for(std::chrono::seconds(0)) ==
        std::future_status::ready) {
      const auto &icon = icon_cache[id].get();
      if (icon)
        return &icon.value();
    }

    return std::nullopt;
  }

  std::optional<GLTexture*> fetch_texture(size_t id) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (texture_cache[id])
        return &texture_cache[id].value();
    }

    auto icon = fetch_icon(id);

    std::lock_guard<std::mutex> lock(mutex);
    if (texture_cache[id])
        return &texture_cache[id].value();

    if (icon.has_value()) {
      texture_cache[id].emplace();
      texture_cache[id]->load(*icon.value());
      return &texture_cache[id].value();
    }

    return std::nullopt;
  }

  std::optional<GLTexture*> fetch_texture(std::string &&name) {
    return fetch_texture(request_id(std::forward<std::string>(name)));
  }

  std::optional<GLTexture*> fetch_texture(const Glib::RefPtr<Gio::Icon> &icon) {
    if (!icon)
      return std::nullopt;

    auto themed_icon = std::dynamic_pointer_cast<Gio::ThemedIcon>(icon);
    auto emblemed_icon = std::dynamic_pointer_cast<Gio::EmblemedIcon>(icon);
    if (themed_icon) {
      for (const auto &icon_name : themed_icon->get_names()) {
        auto tex = fetch_texture(icon_name);
        if (tex.has_value())
          return tex;
      }
    }
    else if (emblemed_icon) {
      return fetch_texture(emblemed_icon->get_icon());
    }

    return fetch_texture(icon->to_string());
  }

  std::optional<GLTexture*> fetch_texture(const Glib::RefPtr<Gio::FileInfo> &info) {
    if (!info) return std::nullopt;

    static const std::pair<std::string, std::string>
        thumbnail_attributes[] = {
            {G_FILE_ATTRIBUTE_THUMBNAIL_IS_VALID,
             G_FILE_ATTRIBUTE_THUMBNAIL_PATH},
            {G_FILE_ATTRIBUTE_THUMBNAIL_IS_VALID_NORMAL,
             G_FILE_ATTRIBUTE_THUMBNAIL_PATH_NORMAL},
            {G_FILE_ATTRIBUTE_THUMBNAIL_IS_VALID_LARGE,
             G_FILE_ATTRIBUTE_THUMBNAIL_PATH_LARGE},
            {G_FILE_ATTRIBUTE_THUMBNAIL_IS_VALID_XLARGE,
             G_FILE_ATTRIBUTE_THUMBNAIL_PATH_XLARGE},
        };

    for (const auto &attr : thumbnail_attributes) {
      if (info->get_attribute_boolean(attr.first)) {
        auto tex = fetch_texture(info->get_attribute_as_string(attr.second));
        if (tex.has_value())
          return tex;
      }
    }

    return fetch_texture(info->get_icon());
  }

private:
  void load_icons_from_queue(std::stop_token token) {
    std::optional<std::pair<std::promise<std::optional<Icon>>,
                            std::optional<std::string>>> job;
    while (!token.stop_requested()) {
      job_queue.pop(job);
      if (!job) return;

      job->first.set_value(Icon::load(job->second));
    }
  }
};
