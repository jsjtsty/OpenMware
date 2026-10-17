OpenMware for Linux (x86-64)

Run ./openmware (the launcher). It keeps its library in ~/.local/share/OpenMware and starts ./openmware-player for the game.

Needs Qt 6 (Widgets) from your distribution, plus the usual desktop libraries SDL3 loads at run time (X11 or Wayland, ALSA or PulseAudio, Vulkan):
  Debian/Ubuntu:  sudo apt install libqt6widgets6 libvulkan1 mesa-vulkan-drivers
  Fedora:         sudo dnf install qt6-qtbase-gui vulkan-loader mesa-vulkan-drivers
  Arch:           sudo pacman -S qt6-base vulkan-icd-loader

Game folder, keys and saves are handled by the launcher; the player can also be used directly:
  ./openmware-player GAME_DIR --keys keys.txt

OpenMware contains no game data. FFmpeg (LGPL) is in lib/ and can be replaced by your own build; licence texts are in licenses/.
