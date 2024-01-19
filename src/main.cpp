#include <GL/glew.h>

#include <iostream>
#include <openvr.h>

#include <SDL.h>
#include <SDL_opengl.h>

#include "file_browser.hpp"
#include "application_launcher.hpp"
#include "icon_fetcher.hpp"
#include "imconfig.h"
#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include "color_theme.h"
#include "ping_pong_renderer.hpp"
#include "video_player_parameters.hpp"
#include "source_sans_pro.h"
#include "window_monitor.hpp"

#include <giomm.h>
#include <algorithm>

static const vr::VROverlayFlags VROverlayFlags_EnableControlBar =
  (vr::VROverlayFlags)(1 << 23);
static const vr::VROverlayFlags VROverlayFlags_EnableControlBarKeyboard =
  (vr::VROverlayFlags)(1 << 24);
static const vr::VROverlayFlags VROverlayFlags_EnableControlBarClose =
  (vr::VROverlayFlags)(1 << 25);

static constexpr size_t OVERLAY_WIDTH = 1920;
static constexpr size_t OVERLAY_HEIGHT = 1080;

static bool install_manifest(bool force_reinstall);
static std::pair<vr::VROverlayHandle_t, vr::VROverlayHandle_t> create_overlay();
static void ImGui_ImplOpenVR_ProcessEvent(const vr::VREvent_t &event);

int main(int argc, char *argv[]) {
  Gio::init();

  SDL_Init(SDL_INIT_VIDEO);

  vr::EVRInitError init_error;
  vr::IVRSystem *vr_system = vr::VR_Init(&init_error, vr::VRApplication_Overlay);
  if (!vr_system) return 1;

  bool force_reinstall =
    std::find(argv + 1, argv + argc, std::string_view("--reinstall")) !=
    argv + argc;
  install_manifest(force_reinstall);

  auto [overlay_handle, _thumbnail_handle] = create_overlay();

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

  SDL_Window *window =
      SDL_CreateWindow("launcher-openvr-overlay", 0, 0, 1, 1,
                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);

  SDL_GLContext context = SDL_GL_CreateContext(window);

  SDL_GL_MakeCurrent(window, context);

  glewExperimental = GL_TRUE;
  glewInit();

  PingPongRenderer renderer(OVERLAY_WIDTH, OVERLAY_HEIGHT);

  bool running = true;
  bool shown = true;

  ImGui::CreateContext();

  embraceTheDarkness();

  ImGuiIO &io = ImGui::GetIO();
  io.BackendPlatformName = "imgui_impl_openvr";
  io.DisplaySize = ImVec2(renderer.w, renderer.h);
  io.DisplayFramebufferScale = ImVec2(1, 1);

  ImGui_ImplOpenGL3_Init();

  io.Fonts->AddFontFromMemoryCompressedTTF(
    font_compressed_data, font_compressed_size, 48);

  {
    IconFetcher icons;

    GamescopeParameters gamescope_params;
    VideoPlayerParameters player_params;

    ApplicationLauncher launcher;
    FileBrowser file_browser;
    WindowMonitor window_monitor(window, context);

    uint64_t prev_time = SDL_GetPerformanceCounter();

    while (running) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
          running = false;
        }
      }

      vr::VREvent_t vr_event;
      while (vr_system->PollNextEvent(&vr_event, sizeof(vr_event))) {
        if (vr_event.eventType == vr::VREvent_Quit)
          running = false;
      }

      while (vr::VROverlay()->PollNextOverlayEvent(overlay_handle, &vr_event,
                                                   sizeof(vr_event))) {
        switch (vr_event.eventType) {
        case vr::VREvent_Quit:
        case vr::VREvent_OverlayClosed:
          running = false;
          break;
        case vr::VREvent_OverlayShown:
          shown = true;
          break;
        case vr::VREvent_OverlayHidden:
          shown = false;
          break;
        default:
          ImGui_ImplOpenVR_ProcessEvent(vr_event);
          break;
        }
      }

      ImGui_ImplOpenGL3_NewFrame();
      uint64_t current_time = SDL_GetPerformanceCounter();
      uint64_t frequency = SDL_GetPerformanceFrequency();
      if (current_time <= prev_time)
        current_time = prev_time + 1;
      io.DeltaTime = (double)(current_time - prev_time) / frequency;
      prev_time = current_time;

      ImGui::NewFrame();

      if (shown) {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowPos(ImVec2());
        ImGui::Begin("Launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::SetWindowFontScale(1.0);

        if (ImGui::BeginTabBar("tabs")) {
          if (ImGui::BeginTabItem("Applications")) {
            launcher.draw(icons, gamescope_params);
            ImGui::EndTabItem();
          }

          if (ImGui::BeginTabItem("Windows")) {
            window_monitor.draw(player_params);
            ImGui::EndTabItem();
          }

          if (ImGui::BeginTabItem("Files")) {
            file_browser.draw(icons, player_params);
            ImGui::EndTabItem();
          }

          ImGui::EndTabBar();
        }
      }

      ImGui::End();

      if (shown) {
        ImGui::Render();

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, renderer.current_framebuffer());
        glViewport(0, 0, renderer.w, renderer.h);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);

        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0, 0, 0, 0);
        glDisable(GL_DEPTH_TEST);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        vr::VRTextureBounds_t bounds{0, 0, 1, 1};
        vr::Texture_t tex{
            (void *)(uintptr_t)renderer.current_texture(),
            vr::TextureType_OpenGL,
            vr::ColorSpace_Linear,
        };

        vr::VROverlay()->SetOverlayTexture(overlay_handle, &tex);
        vr::VROverlay()->SetOverlayTextureBounds(overlay_handle, &bounds);

        glFlush();
        renderer.flip();
      }

      vr::VROverlay()->WaitFrameSync(20);
    }
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DeleteContext(context);

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}

static bool install_manifest(bool force_reinstall) {
  static constexpr const char *APP_KEY = "org.mon-ouie.launcher-openvr-overlay";

  if (!vr::VRApplications()) {
    std::cerr << "Failed to access VR applications!\n";
    return false;
  }

  bool is_installed = vr::VRApplications()->IsApplicationInstalled(APP_KEY);
  if (!is_installed || force_reinstall) {
    static constexpr const char *MANIFEST_PATH =
      DATA_DIR "/launcher-openvr-overlay/manifest.vrmanifest";

    vr::EVRApplicationError err =
      vr::VRApplications()->AddApplicationManifest(MANIFEST_PATH);

    if (err != vr::VRApplicationError_None) {
      std::cerr << "Failed to install application: " << err << "\n";
      return false;
    }

    std::cout << "Application installed successfully!\n";
  }

  return true;
}

static std::pair<vr::VROverlayHandle_t, vr::VROverlayHandle_t> create_overlay() {
  vr::VROverlayHandle_t overlay_handle, thumbnail_handle;
  vr::VROverlay()->CreateDashboardOverlay("launcher-openvr-overlay", "Launcher",
                                          &overlay_handle, &thumbnail_handle);

  vr::VROverlay()->SetOverlayInputMethod(overlay_handle,
                                         vr::VROverlayInputMethod_Mouse);

  vr::VROverlay()->SetOverlayFlag(
    overlay_handle, VROverlayFlags_EnableControlBar, true);
  vr::VROverlay()->SetOverlayFlag(
    overlay_handle, VROverlayFlags_EnableControlBarClose, true);
  vr::VROverlay()->SetOverlayFlag(
    overlay_handle, VROverlayFlags_EnableControlBarKeyboard, true);
  vr::VROverlay()->SetOverlayFlag(
    overlay_handle, vr::VROverlayFlags_WantsModalBehavior, true);
  vr::VROverlay()->SetOverlayFlag(
    overlay_handle, vr::VROverlayFlags_SendVRSmoothScrollEvents, true);

  vr::HmdVector2_t scale = {OVERLAY_WIDTH, OVERLAY_HEIGHT};
  vr::VROverlay()->SetOverlayMouseScale(overlay_handle, &scale);

  vr::VROverlay()->SetOverlayWidthInMeters(overlay_handle, 2.0);

  static const char *ICON_PATH =
    DATA_DIR "/icons/hicolor/256x256/apps/launcher-openvr-overlay.png";
  vr::VROverlay()->SetOverlayFromFile(thumbnail_handle, ICON_PATH);

  return {overlay_handle, thumbnail_handle};
}

static void ImGui_ImplOpenVR_ProcessEvent(const vr::VREvent_t &event) {
  ImGuiIO &io = ImGui::GetIO();
  float overlay_height = io.DisplaySize.y;

  if (event.eventType == vr::VREvent_MouseMove) {
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMousePosEvent(
      event.data.mouse.x, overlay_height - event.data.mouse.y);
  } else if (event.eventType == vr::VREvent_ScrollSmooth ||
             event.eventType == vr::VREvent_ScrollDiscrete) {
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMouseWheelEvent(event.data.scroll.xdelta,
                          event.data.scroll.ydelta);
  } else if (event.eventType == vr::VREvent_MouseButtonDown ||
             event.eventType == vr::VREvent_MouseButtonUp) {
    int mouse_button = -1;
    if (event.data.mouse.button == 1)
      mouse_button = ImGuiMouseButton_Left;
    else if (event.data.mouse.button == 2)
      mouse_button = ImGuiMouseButton_Right;
    else if (event.data.mouse.button == 3)
      mouse_button = ImGuiMouseButton_Middle;

    if (mouse_button != -1) {
      io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
      io.AddMousePosEvent(
        event.data.mouse.x, overlay_height - event.data.mouse.y);
      io.AddMouseButtonEvent(
        mouse_button, event.eventType == vr::VREvent_MouseButtonDown);
    }
  } else if (event.eventType == vr::VREvent_FocusEnter) {
    io.AddFocusEvent(true);
  } else if (event.eventType == vr::VREvent_FocusLeave) {
    io.AddFocusEvent(false);
  } else if (event.eventType == vr::VREvent_KeyboardCharInput) {
    char text[sizeof(event.data.keyboard.cNewInput) + 1] = {0};
    memcpy(text, event.data.keyboard.cNewInput,
           sizeof(event.data.keyboard.cNewInput));

    std::string_view text_view(text);

    if (text_view == "\n") {
      io.AddKeyEvent(ImGuiKey_Enter, true);
      io.AddKeyEvent(ImGuiKey_Enter, false);
    } else if (text_view == "\b") {
      io.AddKeyEvent(ImGuiKey_Backspace, true);
      io.AddKeyEvent(ImGuiKey_Backspace, false);
    } else if (text_view == "\b") {
      io.AddKeyEvent(ImGuiKey_Backspace, true);
      io.AddKeyEvent(ImGuiKey_Backspace, false);
    } else if (text_view == "\033[A") {
      io.AddKeyEvent(ImGuiKey_UpArrow, true);
      io.AddKeyEvent(ImGuiKey_UpArrow, false);
    } else if (text_view == "\033[B") {
      io.AddKeyEvent(ImGuiKey_DownArrow, true);
      io.AddKeyEvent(ImGuiKey_DownArrow, false);
    } else if (text_view == "\033[C") {
      io.AddKeyEvent(ImGuiKey_RightArrow, true);
      io.AddKeyEvent(ImGuiKey_RightArrow, false);
    } else if (text_view == "\033[D") {
      io.AddKeyEvent(ImGuiKey_LeftArrow, true);
      io.AddKeyEvent(ImGuiKey_LeftArrow, false);
    } else
      io.AddInputCharactersUTF8(text);
  }
}
