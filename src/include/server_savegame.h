// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Server.dll's character-file entry points: PlayerData members, declared in
// common_server_playerdb.h. Server-internal, not exported. One port per retail function:
//
//   FUN_06d47120  PlayerData::load_save_record       the character-file parser
//   FUN_06d48d50  PlayerData::load_saved_character   login scan: one file -> record
//   FUN_06d4b5a0  PlayerData::reload_saved_character the post-save reload
//   FUN_06d4c800  PlayerData::save_game             named save / "SaveXXXX" search
//   FUN_06d4c600  PlayerData::mission_save          save + copy to AutoSave.fl
//   FUN_06d4c430  PlayerData::auto_save             the autosave
//   FUN_06d4c5f0  PlayerData::skip_next_autosave     the one-shot skip latch
//   FUN_06d4b790  PlayerData::create_new_character   new-character template expansion
//   FUN_06d4a5d0  PlayerData::select_character      head + MP front (server_playerdb.cpp)
//                 PlayerData::load_selected_character its tail (.text:06d4aa85 ..)
//                 PlayerData::apply_saved_character  its record -> slot copy
//   FUN_06d4b520  PlayerData::send_visited_state     IClient +0x170
//   FUN_06d50740  PlayerData::send_mission_log       IClient +0x184 (+ FUN_06d50850)
//                 (.text:06d4ac77 .. .text:06d4b089)
//
// The implementation is src/Server/server_savegame.cpp; the writer they all
// end in is PlayerData::serialize_to_file (retail FUN_06d4ccd0, server_playerdb.cpp).

#include "common_server_playerdb.h"

// server_pub.cpp. Retail FUN_06d45b60 -- the vibe-id bitmap allocator
// FUN_06d4a5d0 calls before Reputation::Vibe::Alloc (.text:06d4ace3).
void Server_VibeID_Alloc(unsigned int* OutId);
