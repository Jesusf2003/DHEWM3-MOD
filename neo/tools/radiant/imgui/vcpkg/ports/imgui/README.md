Overlay of the official vcpkg `imgui` port, pinned to Dear ImGui **v1.92.9b**
(`v1.92.9b-docking` with the `docking-experimental` feature).

Changes from upstream (microsoft/vcpkg `ports/imgui`):
- re-added the `sdl2-binding` feature (upstream only has SDL3), since dhewm3's
  default platform backend is SDL2.
