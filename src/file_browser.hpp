#pragma once

#include "gl_texture.hpp"
#include "icon_fetcher.hpp"
#include "video_player_parameters.hpp"
#include <filesystem>
#include <future>
#include <thread>
#include <giomm.h>
#include <ranges>
#include <iostream>

/* Who would define C as 1? */
#ifdef C
#undef C
#endif

#include <tbb/concurrent_queue.h>

namespace fs = std::filesystem;

struct FileEntry;

struct FileEntry {
  fs::path path;
  bool is_directory;

  std::shared_future<Glib::RefPtr<Gio::FileInfo>> info;

  FileEntry(std::promise<Glib::RefPtr<Gio::FileInfo>> &info_promise,
            const fs::directory_entry &entry):
    path(entry.path()),
    is_directory(entry.is_directory()),
    info(info_promise.get_future())
    {}

  std::optional<GLTexture*> icon(IconFetcher &fetcher) {
    if (info.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      return fetcher.fetch_texture(info.get());
    }

    return std::nullopt;
  }

  std::strong_ordering operator<=>(const FileEntry &b) const {
    auto dir_cmp = is_directory <=> b.is_directory;
    if (dir_cmp == std::strong_ordering::less)
      return std::strong_ordering::greater;
    else if (dir_cmp == std::strong_ordering::greater)
      return std::strong_ordering::less;
    else
      return path <=> b.path;
  };
};

class FileBrowser {
  fs::path path;

  std::mutex mutex;
  std::vector<FileEntry> files;

  char path_buf[8192];

  struct CompareId {
    const std::vector<FileEntry> &files;

    CompareId(const std::vector<FileEntry> &files): files(files) {}

    bool operator()(std::size_t a, std::size_t b) const {
      return files[a] < files[b];
    }
  };

  CompareId comparator;
  std::set<size_t, CompareId> sorted_ids;

  tbb::concurrent_bounded_queue<
    std::optional<std::pair<std::promise<Glib::RefPtr<Gio::FileInfo>>, fs::path>>
    > info_queue;

  std::jthread updater_thread;
  std::jthread info_lookup_thread;

  bool show_hidden, only_show_videos;
public:
  FileBrowser():
    path(fs::current_path()),
    comparator(files),
    sorted_ids(comparator),
    updater_thread([this](std::stop_token token) { load_directory(token); }),
    info_lookup_thread(
      std::jthread([this](std::stop_token token) { lookup_info(token); })),
    show_hidden(false),
    only_show_videos(true)
    {
      std::string path_str(path);
      memcpy(path_buf, path_str.c_str(), path_str.size());
    }

  ~FileBrowser() {
    info_queue.emplace(std::nullopt);
  }

  const fs::path &current_path() const { return path; }

  void set_path(fs::path path) {
    updater_thread.request_stop();
    updater_thread.join();

    this->path = std::move(path);
    files.clear();
    sorted_ids.clear();

    updater_thread =
        std::jthread([this](std::stop_token token) { load_directory(token); });
  }

  std::mutex &get_mutex() { return mutex; }

  auto sorted_files() {
    return sorted_ids | std::views::transform([this](size_t i) {
      return std::ref(this->files[i]);
    });
  }

  void draw(IconFetcher &icons, VideoPlayerParameters &player_params) {
    if (ImGui::BeginTable("app_window", 2)) {
      ImGui::TableSetupColumn("files", ImGuiTableColumnFlags_WidthStretch, 1.0);
      ImGui::TableSetupColumn(
        "video-player", ImGuiTableColumnFlags_WidthFixed, 750);

      ImGui::TableNextRow();

      ImGui::TableNextColumn();

      ImGui::InputText("Directory", path_buf, sizeof(path_buf));
      fs::path input_path(path_buf);
      if (input_path != current_path() && fs::is_directory(input_path))
        set_path(std::move(input_path));

      if (ImGui::BeginTable("files_config", 3)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Checkbox("Show Hidden Files", &show_hidden);

        ImGui::TableNextColumn();
        ImGui::Checkbox("Only Show Videos", &only_show_videos);

        ImGui::TableNextColumn();

        auto icon = icons.fetch_texture("view-refresh");
        bool clicked = false;
        float height = ImGui::GetTextLineHeightWithSpacing();
        ImVec2 icon_size(height, height);
        if (icon)
          clicked = icon.value()->draw_button(icon_size);
        else
          clicked = ImGui::Button("Refresh");

        if (clicked) set_path(path);

        ImGui::EndTable();
      }

      if (ImGui::BeginTable("files", 2, ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 128);
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch,
                                1.0);

        ImGui::TableHeadersRow();

        if (current_path().has_parent_path()) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();

          float width = ImGui::GetContentRegionAvail().x;
          width -= ImGui::GetStyle().FramePadding.x * 2.0;
          ImVec2 icon_size(width, width);

          auto icon = icons.fetch_texture("folder");
          if (icon) {
            icon.value()->draw(icon_size);
          } else {
            ImGui::Text("Dir");
          }

          ImGui::TableNextColumn();
          float name_width = ImGui::GetContentRegionAvail().x;
          ImVec2 name_size(name_width, width);
          if (ImGui::Button("..", name_size)) {
            set_path(current_path().parent_path());
            std::string path_str = current_path();
            memcpy(path_buf, path_str.c_str(), path_str.size());
            path_buf[path_str.size()] = 0;
          }
        }

        std::lock_guard<std::mutex> lock(mutex);
        for (FileEntry &entry : sorted_files()) {
          if (entry.path.filename().string()[0] == '.' && !show_hidden)
            continue;
          if (only_show_videos && !entry.is_directory) {
            if (entry.info.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
              auto &info = entry.info.get();
              const Glib::ustring video_types[] = {
                  "application/ogg",
                  "application/x-ogg",
                  "application/mxf",
                  "application/sdp",
                  "application/smil",
                  "application/x-smil",
                  "application/streamingmedia",
                  "application/x-streamingmedia",
                  "application/vnd.rn-realmedia",
                  "application/vnd.rn-realmedia-vbr",
                  "audio/aac",
                  "audio/x-aac",
                  "audio/vnd.dolby.heaac.1",
                  "audio/vnd.dolby.heaac.2",
                  "audio/aiff",
                  "audio/x-aiff",
                  "audio/m4a",
                  "audio/x-m4a",
                  "application/x-extension-m4a",
                  "audio/mp1",
                  "audio/x-mp1",
                  "audio/mp2",
                  "audio/x-mp2",
                  "audio/mp3",
                  "audio/x-mp3",
                  "audio/mpeg",
                  "audio/mpeg2",
                  "audio/mpeg3",
                  "audio/mpegurl",
                  "audio/x-mpegurl",
                  "audio/mpg",
                  "audio/x-mpg",
                  "audio/rn-mpeg",
                  "audio/musepack",
                  "audio/x-musepack",
                  "audio/ogg",
                  "audio/scpls",
                  "audio/x-scpls",
                  "audio/vnd.rn-realaudio",
                  "audio/wav",
                  "audio/x-pn-wav",
                  "audio/x-pn-windows-pcm",
                  "audio/x-realaudio",
                  "audio/x-pn-realaudio",
                  "audio/x-ms-wma",
                  "audio/x-pls",
                  "audio/x-wav",
                  "video/mpeg",
                  "video/x-mpeg2",
                  "video/x-mpeg3",
                  "video/mp4v-es",
                  "video/x-m4v",
                  "video/mp4",
                  "application/x-extension-mp4",
                  "video/divx",
                  "video/vnd.divx",
                  "video/msvideo",
                  "video/x-msvideo",
                  "video/ogg",
                  "video/quicktime",
                  "video/vnd.rn-realvideo",
                  "video/x-ms-afs",
                  "video/x-ms-asf",
                  "audio/x-ms-asf",
                  "application/vnd.ms-asf",
                  "video/x-ms-wmv",
                  "video/x-ms-wmx",
                  "video/x-ms-wvxvideo",
                  "video/x-avi",
                  "video/avi",
                  "video/x-flic",
                  "video/fli",
                  "video/x-flc",
                  "video/flv",
                  "video/x-flv",
                  "video/x-theora",
                  "video/x-theora+ogg",
                  "video/x-matroska",
                  "video/mkv",
                  "audio/x-matroska",
                  "application/x-matroska",
                  "video/webm",
                  "audio/webm",
                  "audio/vorbis",
                  "audio/x-vorbis",
                  "audio/x-vorbis+ogg",
                  "video/x-ogm",
                  "video/x-ogm+ogg",
                  "application/x-ogm",
                  "application/x-ogm-audio",
                  "application/x-ogm-video",
                  "application/x-shorten",
                  "audio/x-shorten",
                  "audio/x-ape",
                  "audio/x-wavpack",
                  "audio/x-tta",
                  "audio/AMR",
                  "audio/ac3",
                  "audio/eac3",
                  "audio/amr-wb",
                  "video/mp2t",
                  "audio/flac",
                  "audio/mp4",
                  "application/x-mpegurl",
                  "video/vnd.mpegurl",
                  "application/vnd.apple.mpegurl",
                  "audio/x-pn-au",
                  "video/3gp",
                  "video/3gpp",
                  "video/3gpp2",
                  "audio/3gpp",
                  "audio/3gpp2",
                  "video/dv",
                  "audio/dv",
                  "audio/opus",
                  "audio/vnd.dts",
                  "audio/vnd.dts.hd",
                  "audio/x-adpcm",
                  "application/x-cue",
                  "audio/m3u",
              };

              if (info &&
                  std::find(std::begin(video_types), std::end(video_types),
                            info->get_content_type()) ==
                      std::end(video_types)) {
                continue;
              }
            }
          }

          ImGui::TableNextRow();
          ImGui::TableNextColumn();

          float width = ImGui::GetContentRegionAvail().x;
          width -= ImGui::GetStyle().FramePadding.x * 2.0;
          ImVec2 icon_size(width, width);

          auto icon = entry.icon(icons);
          if (icon) {
            icon.value()->draw(icon_size);
          } else if (entry.is_directory)
            ImGui::Text("Dir");
          else
            ImGui::Text("File");

          ImGui::TableNextColumn();
          float name_width = ImGui::GetContentRegionAvail().x;
          ImVec2 name_size(name_width, width);
          if (ImGui::Button(entry.path.filename().c_str(), name_size)) {
            if (entry.is_directory) {
              set_path(entry.path);
              std::string path_str = current_path();
              memcpy(path_buf, path_str.c_str(), path_str.size());
              path_buf[path_str.size()] = 0;
              break;
            } else {
              auto args = player_params.command_line();
              args.push_back("--video");
              args.push_back(entry.path);

              try {
                Glib::spawn_async(fs::current_path(), args);
              } catch (const Glib::SpawnError &e) {
                std::cerr << "spawn error: " << e.what() << "\n";
              }
            }
          }
        }

        ImGui::EndTable();
      }

      ImGui::TableNextColumn();
      player_params.draw();

      ImGui::EndTable();
    }
  }

private:
  void load_directory(std::stop_token token) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      files.clear();
      sorted_ids.clear();
    }

    try {
      for (const auto &entry : fs::directory_iterator(path)) {
        if (token.stop_requested()) return;

        std::promise<Glib::RefPtr<Gio::FileInfo>> promise;
        FileEntry file(promise, entry);
        info_queue.emplace(std::make_pair(std::move(promise), entry));

        std::lock_guard<std::mutex> lock(mutex);
        files.emplace_back(std::move(file));
        sorted_ids.insert(files.size() - 1);
      }
    }
    catch (const fs::filesystem_error &e) {}
  }

  void lookup_info(std::stop_token token) {
    std::optional<std::pair<std::promise<Glib::RefPtr<Gio::FileInfo>>, fs::path>>
      job;
    while (!token.stop_requested()) {
      info_queue.pop(job);
      if (!job) return;

      auto file = Gio::File::create_for_path(job->second);
      job->first.set_value(file->query_info(
                             G_FILE_ATTRIBUTE_STANDARD_ICON ","
                             G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                             "thumbnail::*"));
    }
  }
};
