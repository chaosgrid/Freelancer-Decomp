// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Map of the retail Client/Space HUD and effect files in freelancer.exe .text
// ~0x004c98a0-0x00500000 (boundaries from RTTI and the embedded
// "E:\FL\Scratch\Source\Client\..." path strings), with where each lives in the port. This
// header carries no declarations.
//
//   costume / deform helpers  0x004c98a0-0x004ca610  engine_character.{h,cpp}
//   HUD_Comm.cpp              0x004ca640-0x004cefe0  HUD/hud_comm.{h,cpp} (RTTI HUD_Receiver,
//                                                   factory "Receiver")
//   ContactList / HUD_Status  0x004cf000-            HUD/hud_contact_list.*, hud_status_core.*
//   HUD_Core                  0x004d4b00-0x004d5b00  HUD/hud_status_core.{h,cpp}
//   HUD_CruiseProgress        0x004d5b60-0x004d6260  HUD/hud_cruise_progress.{h,cpp}
//   HUD_DamageIndicator       0x004d62a0-0x004d7520  HUD/hud_damage_indicator.* (Create at
//                                                   0x004f3d70)
//   HUD_Maneuvers             0x004d7570-0x004da0c0  HUD/hud_maneuvers.{h,cpp}
//   HUD_Status                0x004da100-0x004df100  HUD/hud_status_core.{h,cpp}; weapon groups
//                                                   in HUD/hud_weapon_groups.{h,cpp}
//   HUD_Target_Base.cpp       ~0x004e70e0-0x004ed9f0 HUD/hud_target.{h,cpp}
//   HUD_Targetable_Objects    ~0x004ed9f0-0x004f8160 HUD/hud_targetable_objects.{h,cpp}; a few
//                                                   of its functions (e.g. FUN_004cf6a0, the
//                                                   "HUD_contactship_3d" mesh) sit earlier
//   Effect.cpp                ~0x004f8160-0x004fd5a0 Effects/effect_base.*, particle_effect.*,
//                                                   beam_effect.*
//   EffectTypes.cpp           ~0x004fd5a0-0x004fda00 Effects/effect_type_info.{h,cpp}
//   FlashLight.cpp            ~0x004fda00-0x004fedd0 Effects/flash_light.* and the CliEquipLight
//                                                   in client_equip_fx.cpp
//   GateTunnel.cpp            ~0x004fedd0-          Effects/gate_tunnel.{h,cpp}
