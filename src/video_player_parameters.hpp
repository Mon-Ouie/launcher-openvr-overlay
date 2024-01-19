#pragma once

#include <imgui.h>
#include <giomm.h>
#include <chrono>

enum class ProjectionMode { Sphere, Sphere360, Flat, Plane };

static const char *PROJECTION_NAMES[] = {
  "Sphere",
  "Sphere 360",
  "Flat",
  "Plane"
};

struct VideoPlayerParameters {
  bool overlay;
  bool overlay_mouse;
  float zoom;
  bool stretch;
  bool left_to_right;
  ProjectionMode projection;
  float overlay_width;

  VideoPlayerParameters():
    overlay(true),
    overlay_mouse(true),
    zoom(0.0),
    stretch(true),
    left_to_right(true),
    projection(ProjectionMode::Plane),
    overlay_width(2.0)
    {}

  void draw() {
    if (ImGui::BeginCombo("Projection", PROJECTION_NAMES[(size_t)projection])) {
      for (size_t i = 0; i < 4; i++) {
        if (ImGui::Selectable(PROJECTION_NAMES[i], (size_t)projection == i)) {
          projection = (ProjectionMode)i;
        }
      }
      ImGui::EndCombo();
    }

    ImGui::SliderFloat("Zoom", &zoom, 0.0, 5.0);
    ImGui::SliderFloat("Overlay Width [m]", &overlay_width, 0.1, 10.0);

    ImGui::Checkbox("Open as VR Overlay", &overlay);
    ImGui::Checkbox("Overlay Mouse Controls", &overlay_mouse);

    if (projection == ProjectionMode::Flat) {
      ImGui::Checkbox("Swap Left and Right", &left_to_right);
      ImGui::Checkbox("Half-width side-by-side", &stretch);
    }
  }

  std::vector<std::string> command_line() const {
    auto ms = std::chrono::system_clock::now().time_since_epoch() /
              std::chrono::milliseconds(1);

    std::vector<std::string> args = {
      Glib::find_program_in_path("vr-video-player"),
      overlay_mouse ? "--overlay-mouse" : "--no-overlay-mouse",
      "--overlay-key",
      "launcher-openvr-overlay-" + std::to_string(ms),
      "--overlay-width",
      std::to_string(overlay_width),
      "--zoom",
      std::to_string(zoom)
    };

    if (overlay)
      args.push_back("--overlay");

    switch (projection) {
    case ProjectionMode::Sphere: {
      args.push_back("--sphere");
      break;
    }

    case ProjectionMode::Sphere360: {
      args.push_back("--sphere360");
      break;
    }

    case ProjectionMode::Flat: {
      args.push_back("--flat");
      args.push_back(left_to_right ? "--left-right" : "--right-left");
      args.push_back(stretch ? "--stretch" : "--no-stretch");
      break;
    }

    case ProjectionMode::Plane: {
      args.push_back("--plane");
      break;
    }
    }

    return args;
  }
};
