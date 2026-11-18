/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

// The bridge recorder's files (tests only: the recorder build's rt_d3d12.hpp writes them, the GPU harness's player reads
// the requests back into its shared textures). A recording's folder holds state.bin (the helper's scene, histories and
// learned values between two frames), request_<frame>_<index>.bin (a request as the helper ran it: its block, its new
// cluster sets' data, the inputs that changed since the request before), output_<frame>_<index>.bin (its outputs as the
// helper left them) and digest_<frame>.txt (the scene its trace built). Each file: a FileHeader, then chunks (a
// ChunkHeader, its name, its bytes) up to CHUNK_END.
namespace twattila::dlaa_helper::recording {

constexpr uint32_t MAGIC = 0x43525754;  // "TWRC"
// The files' own layout; the snapshot's fields are checked one by one by their names and sizes (FileHeader::layout).
constexpr uint32_t FORMAT = 1;

struct FileHeader {
  uint32_t magic = MAGIC;
  uint32_t format = FORMAT;
  // dlaa_bridge::VERSION of the helper that wrote it; a player of a later bridge plays it as it answers an older addon
  // (OLDEST_VERSION on: all requests at the first one's block version, the fields past that version's block absent).
  uint32_t bridge_version = 0;
  uint32_t shared_bytes = 0;    // its block's bytes (a request's CHUNK_BLOCK)
  uint64_t layout = 0;          // a hash of the snapshot's field names and sizes (state.bin)
  uint32_t frame = 0;           // the request's frame (state.bin: the first recorded frame)
  uint32_t request = 0;         // 1 trace, 2 Light Bounce, 3 reflections (state.bin: 0)
  char helper[64] = {};         // the writing helper's build (its exe's time stamp and size)
};

enum ChunkKind : uint32_t {
  CHUNK_END,
  CHUNK_BLOCK,    // the request's dlaa_bridge::Shared as the helper read it
  CHUNK_SETS,     // its new cluster sets' data (the second mapping from its start)
  CHUNK_TEXTURE,  // a TextureHeader, then each subresource's rows packed
  CHUNK_BUFFER,   // a BufferHeader, then its bytes
  CHUNK_FIELD,    // a snapshot field's bytes
  CHUNK_MESH,     // a static mesh (rt_d3d12.hpp's recorder)
  CHUNK_SET,      // a cluster set
  CHUNK_NOTE,     // text: why a snapshot was refused, what a request left out
};

struct ChunkHeader {
  uint32_t kind = CHUNK_END;
  uint32_t name_bytes = 0;
  uint64_t bytes = 0;  // past the name
};

// A texture's or its top rows' (the vertex texture's rows a trace read), as D3D12 describes it; its subresources' rows
// packed one after the other.
struct TextureHeader {
  uint32_t format = 0;  // DXGI_FORMAT
  uint32_t width = 0;
  uint32_t height = 0;  // the whole texture's
  uint32_t slices = 0;  // subresources (the atlas: RT_ATLAS_SLICES x its mips)
  uint32_t row_bytes = 0;  // the first subresource's
  uint32_t first_row = 0;
  uint32_t rows = 0;  // the first subresource's recorded rows from first_row
  uint32_t mips = 0;
};

struct BufferHeader {
  uint64_t bytes = 0;   // the buffer's
  uint64_t offset = 0;  // the recorded part's start
};

}  // namespace twattila::dlaa_helper::recording
