    SDL_RenderSetScale(s_ren, sg_menu_scale, sg_menu_scale);   // Shadowgreat: enlarge the menu
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), s_ren);
    SDL_RenderSetScale(s_ren, 1.0f, 1.0f);
