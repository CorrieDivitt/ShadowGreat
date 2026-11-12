
// ---- Shadowgreat: scale the menu with the window (patches/menu-scale) ----
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>

static float sg_menu_scale = 1.0f;
static SDL_Renderer *sg_sdl_ren = nullptr;

static bool sg_process_event(const SDL_Event *ev)
{
    if (!ev) return false;
    SDL_Event unscaled_ev = *ev;
    if (sg_sdl_ren) {
        if (ev->type == SDL_MOUSEMOTION) {
            int wx = 0, wy = 0;
            SDL_RenderLogicalToWindow(sg_sdl_ren, (float)ev->motion.x, (float)ev->motion.y, &wx, &wy);
            unscaled_ev.motion.x = wx;
            unscaled_ev.motion.y = wy;
        } else if (ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP) {
            int wx = 0, wy = 0;
            SDL_RenderLogicalToWindow(sg_sdl_ren, (float)ev->button.x, (float)ev->button.y, &wx, &wy);
            unscaled_ev.button.x = wx;
            unscaled_ev.button.y = wy;
        }
    }
    return ImGui_ImplSDL2_ProcessEvent(&unscaled_ev);
}
#define ImGui_ImplSDL2_ProcessEvent sg_process_event

// The menu is laid out for a window of about 830 x 730. Draw it into a
// smaller pretend window and let the renderer enlarge it, so it fills about
// 70 % of the real window's height. Mouse positions arrive in real window
// pixels and are shrunk to match.
static void sg_menu_scale_begin()
{
    ImGuiIO &io = ImGui::GetIO();
    float k = std::min(io.DisplaySize.y * 0.70f / 680.0f, io.DisplaySize.x / 828.0f);
    k = std::max(1.0f, std::min(k, 3.0f));
    sg_menu_scale = k;
    if (k == 1.0f) return;
    io.DisplaySize.x /= k;
    io.DisplaySize.y /= k;
    ImGuiContext &g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.InputEventsQueue.Size; ++i) {
        ImGuiInputEvent &e = g.InputEventsQueue[i];
        if (e.Type == ImGuiInputEventType_MousePos && e.MousePos.PosX > -FLT_MAX) {
            e.MousePos.PosX /= k;
            e.MousePos.PosY /= k;
        }
    }
}
// ---- end Shadowgreat ----
