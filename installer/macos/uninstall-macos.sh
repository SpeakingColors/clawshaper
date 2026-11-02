#!/bin/sh
# Removes the ClawShaper plugin installed by the package.
set -e
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/ClawShaper.vst3"
sudo pkgutil --forget com.studio87.clawshaper.vst3 >/dev/null 2>&1 || true
echo "Removed the plugin."
