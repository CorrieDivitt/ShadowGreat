    // Shadowgreat: rasterise the fonts 3x so scaled letters stay sharp, and
    // take all queued input each frame (each mouse position is shrunk once).
    for (int i = 0; i < io.Fonts->Sources.Size; ++i) io.Fonts->Sources[i].RasterizerDensity = 3.0f;
    io.ConfigInputTrickleEventQueue = false;
