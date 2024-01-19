#pragma once

#include "gl_texture.hpp"
#include "icon_fetcher.hpp"
#include "gamescope_parameters.hpp"

#include <algorithm>
#include <giomm.h>

struct Application {
  Glib::RefPtr<Gio::AppInfo> app;

  Application(const Glib::RefPtr<Gio::AppInfo> &app): app(app)
    {}

  bool matches(const Glib::ustring &search) const {
    return Glib::ustring(app->get_name()).lowercase().find(search) !=
      Glib::ustring::npos;
  }

  std::optional<GLTexture*> icon(IconFetcher &fetcher) const {
    return fetcher.fetch_texture(app->get_icon());
  }
};

class ApplicationLauncher {
  std::vector<Application> applications;
  char search[2048];
  bool use_gamescope;
public:
  ApplicationLauncher(): use_gamescope(true) {
    for (auto &&app : Gio::AppInfo::get_all()) {
      if (!app->should_show())
        continue;

      applications.emplace_back(app);
    }

    std::sort(applications.begin(), applications.end(),
              [](const auto &a, const auto &b) {
                const Glib::ustring name_a =
                    Glib::ustring(a.app->get_name()).casefold();
                const Glib::ustring name_b =
                    Glib::ustring(b.app->get_name()).casefold();

                return name_a.compare(name_b) < 0;
              });
  }

  std::vector<const Application*> selected_applications() const {
    Glib::ustring search_string(search);
    std::vector<const Application*> selected_apps;
    for (auto &app : applications) {
      if (app.matches(search_string))
        selected_apps.push_back(&app);
    }

    return selected_apps;
  }

  void draw(IconFetcher &icons, GamescopeParameters &gamescope_params) {
    if (ImGui::BeginTable("launcher_table", 2)) {
      ImGui::TableSetupColumn("applications",
                              ImGuiTableColumnFlags_WidthStretch, 1.0);
      ImGui::TableSetupColumn("gamescope", ImGuiTableColumnFlags_WidthFixed,
                              770);

      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      ImGui::BeginGroup();

      ImGui::InputText("Search", search, sizeof(search));

      Glib::ustring search_string(search);
      std::vector<Application *> selected_apps;
      for (auto &app : applications) {
        if (app.matches(search_string))
          selected_apps.push_back(&app);
      }

      if (ImGui::BeginTable("applications", 5, ImGuiTableFlags_ScrollY)) {
        ImGui::TableNextRow();
        for (Application *app : selected_apps) {
          ImGui::TableNextColumn();

          float width = ImGui::GetContentRegionAvail().x;
          width -= ImGui::GetStyle().FramePadding.x * 2.0;

          ImVec2 button_size(width, width);

          auto icon = app->icon(icons);
          bool clicked = false;
          if (icon) {
            ImGui::BeginGroup();
            width -= ImGui::GetTextLineHeightWithSpacing();
            if (icon.value()->draw_button(ImVec2(width, width)))
              clicked = true;
            ImGui::TextUnformatted(app->app->get_name().c_str());

            ImGui::EndGroup();
          } else {
            if (ImGui::Button(app->app->get_name().c_str(), button_size))
              clicked = true;
          }

          if (clicked) {
            if (use_gamescope) {
              auto ms = std::chrono::system_clock::now().time_since_epoch() /
                        std::chrono::milliseconds(1);
              std::stringstream stream;
              stream.imbue(std::locale("C"));
              stream << "gamescope -w " << gamescope_params.width << " -h "
                     << gamescope_params.height << " --openvr"
                     << " --vr-overlay-physical-width "
                     << gamescope_params.physical_width
                     << " --vr-overlay-enable-control-bar"
                     << " --vr-overlay-enable-control-bar-keyboard"
                     << " --vr-overlay-enable-control-bar-close"
                     << " --vr-overlay-key launcher-openvr-overlay-" << ms
                     << " " << gamescope_params.extra_options << " -- "
                     << app->app->get_commandline();

              auto wrapped_app = Gio::AppInfo::create_from_commandline(
                  stream.str(), app->app->get_name() + " [openvr]",
                  Gio::AppInfo::CreateFlags::NONE);
              wrapped_app->launch(nullptr, nullptr);
            } else
              app->app->launch(nullptr, nullptr);
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndGroup();

      ImGui::TableNextColumn();
      ImGui::Checkbox("Run as VR Overlay", &use_gamescope);
      ImGui::Separator();
      gamescope_params.draw();
      ImGui::EndTable();
    }
  }
};
