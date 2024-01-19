#pragma once

#include "gl_texture.hpp"
#include "icon.hpp"
#include "video_player_parameters.hpp"

#include <SDL_video.h>
#include <X11/Xlib.h>
#include <atomic>
#include <filesystem>
#include <optional>
#include <iostream>
#include <thread>

struct WindowEntry {
  Window id;
  std::optional<std::string> title;
  std::optional<Icon> icon;
  std::optional<GLTexture> texture;

  std::optional<GLTexture> &get_texture() {
    if (icon.has_value() && !texture.has_value()) {
      texture.emplace();
      texture->load(*icon);
    }

    return texture;
  }
};

static Window last_bad_id = None;

static int on_xlib_error(Display *display, XErrorEvent *ev) {
  (void)display;

  if (ev->error_code == BadWindow) {
    last_bad_id = ev->resourceid;
  }
  return 0;
}

class WindowMonitor {
  std::vector<WindowEntry> window_entries;

  Display *display;

  Atom client_list_atom;
  Atom wm_name_atom;
  Atom utf8_atom;
  Atom icon_atom;

  std::atomic<bool> is_shown;

  std::mutex mutex;
  std::jthread updater_thread;
public:
  WindowMonitor(SDL_Window *window, SDL_GLContext context) {
    display = XOpenDisplay(NULL);
    XInitThreads();

    if (!display) return;

    client_list_atom = XInternAtom(display, "_NET_CLIENT_LIST", False);
    wm_name_atom = XInternAtom(display, "_NET_WM_NAME", False);
    utf8_atom = XInternAtom(display, "UTF8_STRING", False);
    icon_atom = XInternAtom(display, "_NET_WM_ICON", False);

    updater_thread = std::jthread([this, window, context](std::stop_token token) {
      update(token, window, context);
    });
  }

  void show() {
    is_shown.store(true, std::memory_order_release);
  }

  void hide() {
    is_shown.store(true, std::memory_order_release);
  }

  void draw(VideoPlayerParameters &player_params) {
    if (ImGui::BeginTable("app_window", 2)) {
      ImGui::TableSetupColumn("windows", ImGuiTableColumnFlags_WidthStretch,
                              1.0);
      ImGui::TableSetupColumn("video-player", ImGuiTableColumnFlags_WidthFixed,
                              750);

      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      if (ImGui::BeginTable("windows", 2, ImGuiTableFlags_ScrollY)) {
        show();
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 128);
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch,
                                1.0);

        ImGui::TableHeadersRow();

        std::lock_guard<std::mutex> lock(mutex);
        for (WindowEntry &entry : window_entries) {
          ImGui::TableNextRow();

          ImGui::TableNextColumn();
          float width = ImGui::GetContentRegionAvail().x;
          width -= ImGui::GetStyle().FramePadding.x * 2.0;

          ImVec2 button_size(width, width);

          auto &tex = entry.get_texture();
          bool clicked = false;
          if (tex) {
            if (tex->draw_button(button_size))
              clicked = true;
          } else {
            if (ImGui::Button("Capture", button_size))
              clicked = true;
          }

          ImGui::TableNextColumn();

          width = ImGui::GetContentRegionAvail().x;
          width -= ImGui::GetStyle().FramePadding.x * 2.0;
          ImVec2 title_button_size(width, button_size.y);

          if (ImGui::Button(entry.title.value_or("?").c_str(),
                            title_button_size))
            clicked = true;

          if (clicked) {
            auto args = player_params.command_line();
            args.push_back(std::to_string(entry.id));

            try {
              Glib::spawn_async(std::filesystem::current_path(), args);
            } catch (const Glib::SpawnError &e) {
              std::cerr << "spawn error: " << e.what() << "\n";
            }
          }
        }

        ImGui::EndTable();
      } else
        hide();

      ImGui::TableNextColumn();
      player_params.draw();

      ImGui::EndTable();
    }
  }

private:
  void update(std::stop_token token, SDL_Window *sdl_window, SDL_GLContext context) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

    SDL_GL_MakeCurrent(sdl_window, context);
    SDL_GLContext new_context = SDL_GL_CreateContext(sdl_window);

    SDL_GL_MakeCurrent(sdl_window, new_context);

    XSetErrorHandler(on_xlib_error);

    while (!token.stop_requested()) {
      if (!is_shown.load(std::memory_order_acquire)) {
        std::vector<Window> windows = get_window_list();
        std::vector<WindowEntry> entries;
        for (Window window : windows) {
          WindowEntry info = window_info(window);
          if (last_bad_id != window)
            entries.emplace_back() = std::move(info);
        }

        {
          std::lock_guard<std::mutex> lock(mutex);
          window_entries = std::move(entries);
        }
      }

      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
  }

  std::vector<Window> get_window_list() {
    unsigned char *props = NULL;

    unsigned long requested_size = 1024;
    unsigned long num_items = 0;
    unsigned long bytes_after = 1;

    while (bytes_after != 0) {
      int format = 0;

      Atom property_type;
      if (props)
        XFree(props);
      if (XGetWindowProperty(display, DefaultRootWindow(display), client_list_atom,
                             0, requested_size, False, AnyPropertyType,
                             &property_type, &format, &num_items, &bytes_after,
                             &props) != Success) {
        return {};
      }
      requested_size *= 2;

      if (format != 32) { /* 32 means array of long */
        XFree(props);
        return {};
      }
    }

    long *ids = (long *)props;
    std::vector<Window> out_windows(num_items);
    for (unsigned long i = 0; i < num_items; i++)
      out_windows[i] = ids[i];

    XFree(props);

    return out_windows;
  }

  WindowEntry window_info(Window window) {
    WindowEntry entry;
    entry.id = window;
    entry.title = window_name(window);
    entry.icon = best_icon(window);

    return entry;
  }

  std::optional<std::string> window_name(Window window) {
    unsigned char *props = NULL;

    unsigned long requested_size = 1024;
    unsigned long num_items = 0;
    unsigned long bytes_after = 1;

    while (bytes_after != 0) {
      int format = 0;

      Atom property_type;
      if (props)
        XFree(props);
      if (XGetWindowProperty(display, window, wm_name_atom, 0, requested_size,
                             False, utf8_atom, &property_type, &format,
                             &num_items, &bytes_after, &props) != Success) {
        return std::nullopt;
      }
      requested_size *= 2;
    }

    if (!props)
      return std::nullopt;

    std::string name((char *)props);
    XFree(props);
    return name;
  }

  unsigned long best_icon_offset(Window window) {
    unsigned long offset = 0;
    unsigned long best_offset = (unsigned long)-1;
    unsigned long best_size = 0;

    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop_data;

    while (true) {
      if (XGetWindowProperty(display, window, icon_atom, offset, 2, 0,
                             AnyPropertyType, &type, &format, &nitems,
                             &bytes_after, &prop_data) != Success)
        break;

      if (nitems != 2) {
        XFree(prop_data);
        break;
      }

      unsigned long width = ((unsigned long *)prop_data)[0];
      unsigned long height = ((unsigned long *)prop_data)[1];

      unsigned long size = width * height;

      if (size > best_size && size <= 512 * 512) {
        best_offset = offset;
        best_size = size;
      }

      offset += 2 + size;
      XFree(prop_data);
    }

    return best_offset;
  }

  std::optional<Icon> best_icon(Window window) {
    unsigned long best_offset = best_icon_offset(window);

    if (best_offset == (unsigned long)-1)
      return std::nullopt;

    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop_data;

    if (XGetWindowProperty(display, window, icon_atom, best_offset, 2, 0,
                           AnyPropertyType, &type, &format, &nitems,
                           &bytes_after, &prop_data) != Success)
      return std::nullopt;

    if (nitems != 2) {
      XFree(prop_data);
      return std::nullopt;
    }

    unsigned long width = ((unsigned long *)prop_data)[0];
    unsigned long height = ((unsigned long *)prop_data)[1];

    unsigned long n_expected = width * height;
    unsigned char *icon;
    if (XGetWindowProperty(display, window, icon_atom, best_offset + 2,
                           n_expected, 0, AnyPropertyType, &type, &format,
                           &nitems, &bytes_after, &icon) != Success)
      return std::nullopt;

    if (nitems != n_expected) {
      XFree(prop_data);
      XFree(icon);
      return std::nullopt;
    }

    std::vector<uint32_t> icon_data(n_expected);
    for (size_t i = 0; i < n_expected; i++) {
      icon_data[i] = ((unsigned long *)icon)[i] & 0xFFFFFFFFull;
      uint32_t r = (icon_data[i] & 0x000000FF) >> 0;
      uint32_t g = (icon_data[i] & 0x0000FF00) >> 8;
      uint32_t b = (icon_data[i] & 0x00FF0000) >> 16;
      uint32_t a = (icon_data[i] & 0xFF000000) >> 24;

      icon_data[i] = b | (g << 8) | (r << 16) | (a << 24);
    }

    XFree(icon);
    XFree(prop_data);

    return Icon(icon_data, width, height);
  }
};
