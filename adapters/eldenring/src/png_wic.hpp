#pragma once

// Decodes a PNG file into 8-bit RGBA with the Windows Imaging Component. Used for the locally extracted Minecraft textures
// (never committed, see docs/PORTING_PLAYBOOK.md).

#include <cstdint>
#include <string>
#include <vector>

namespace erov {

// `path` is a UTF-8 path. Returns false (and leaves the outputs empty) when the file is missing or not a PNG.
bool DecodePngFile(const std::string& path, std::vector<uint8_t>& rgba, unsigned& width, unsigned& height);

} // namespace erov
