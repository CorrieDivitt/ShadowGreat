
// ---- Shadowgreat: scale the menu with the window (patches/menu-scale) ----
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>
static float sg_menu_scale = 1.0f;
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
