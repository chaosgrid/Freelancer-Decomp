// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "content.h"
#include "common_archetype.h"   // EObjectType (OBJECT_TYPE bits)
#include "common_iface.h"       // EBehaviorTypeId, TERMINAL_TYPE (TERM_*)
#include "x86math.h"

#include <cstdio>
#include <windows.h>

// Internal Content.dll helpers. Not a DACOM surface.

// The value bits of an id (nickname, system, base, object and ID_String ids): retail tests an
// id for "set" with TEST id,0x3fffffff and compares the masked values.
constexpr unsigned int GIdValueMask = 0x3fffffff;

extern HMODULE G_ContentModule;

// Retail DAT_06fce76c (.data, initial 1): module-wide id generator, post-incremented by
// the timer/handler-id writers of TU21, TU25, TU36, TU38, TU47 and TU53. Content.cpp.
extern unsigned int GContentNextUniqueId;

// Retail FUN_06efec10 (TU33, GameSupport.cpp): DAT_06fcf430, the game time accumulated
// from GameSupport heartbeats.
double __cdecl GetContentGameTime();

// Retail FUN_06efec20 (TU33, GameSupport.cpp): |*A - *B|, or the .rdata default
// DAT_06fb78f0 when either time is the -1.0 sentinel. Callers: GCS, TU54, TU57.
double __cdecl GetContentTimeDistance(double const* A, double const* B);

class Vector;   // common_ini.h

// Retail FUN_06eeaee0 (cdecl, COMDAT emitted in TU25; Populators.cpp): A - B returned by
// value, each component rounded to float. 34 call sites across TU25, TU37, TU40, TU45, TU46,
// TU52, TU53, TU54, TU58 and TU61.
Vector __cdecl SubtractVectors(Vector const& A, Vector const& B);

// Retail FUN_06f5aeb0 (cdecl, TU52; Populators.cpp): y = bits 0x5f3759df - (bits(Value)
// SAR 1); returns (1.5f - ((y * y) * Value) * 0.5f) * y on the x87 stack (double here).
// Callers in TU52 and TU53.
double __cdecl FastReciprocalSqrt(float Value);

// Retail DAT_06fd1c74 (TU53, SpaceSupport.cpp): the SpaceSupport controller id, written by
// its ctor FUN_06f5cd10; timers of the message router are set on it.
extern unsigned int GSpaceSupportControllerId;

// TU36 message router singleton (DAT_06fcf254); class in ContentMessageRouter.h.
class FContentMessageRouter;
extern FContentMessageRouter* GMessageRouter;

I3DMathEngine* __cdecl ContentAcquire3DMathEngine();
// The module's one lazily created engine (a deviation: retail has one per source file).
I3DMathEngine* ContentMathEngine();
void __cdecl ContentSetGcsClampEnabled(unsigned char EnabledByte);
bool __cdecl ContentIsGcsClampEnabled();
void __cdecl SpacePopSetGcsClampEnabled(unsigned char EnabledByte);

class INI_Reader;

// FUN_06f24280 (ContentDataPath.cpp): pub::GetDataPath() + _makepath + INI_Reader::open(path, false).
bool __cdecl OpenContentDataIni(char const* RelativePath, INI_Reader* Reader);

// FUN_06f243d0 (ContentDataPath.cpp; callers TU31 06efc589, TU38 06f16339): Out's position is
// Anchor * (cos Heading * Range, 0, -sin Heading * Range) and its rotation faces away from
// Anchor's position (the z column points from the anchor to Out, y stays in the vertical plane).
void __cdecl PlaceFacingAnchor(FTransform const* Anchor, float const* Heading, float const* Range,
                               FTransform* Out);

IDLL* CreateGameSupport(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateSpaceSupport(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateBaseSupport(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateSpacePop(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateBasePop(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateStoryLauncher(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateStory(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateDestroyMission(char const* Name, unsigned int ControllerId, void const* Parms);
// StoryMissions.cpp: one class per CreateInstance name (FUN_06eeec50 table).
IDLL* CreateGenericMission(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission01a(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission01b(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission02(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission03(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission04(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission05(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission06(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission07(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission08(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission09(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission10(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission11(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission12(char const* Name, unsigned int ControllerId, void const* Parms);
IDLL* CreateMission13(char const* Name, unsigned int ControllerId, void const* Parms);

// SpacePop exclusion spheres (retail DAT_06fd0e58/5c/60).
void __cdecl SpacePopSetPopulationEnabled(unsigned char EnabledByte);
unsigned char __cdecl SpacePopGetPopulationEnabled();
void __cdecl SpacePopSetEncounterEnabled(unsigned char EnabledByte);
unsigned char __cdecl SpacePopGetEncounterEnabled();
void __cdecl SpacePopSetAttackClamp(unsigned char EnabledByte);
bool __cdecl SpacePopPositionIsExcluded(float const* Position);
void __cdecl SpacePopAddExclusionSphere(float X, float Y, float Z, float Radius);
void __cdecl SpacePopRemoveExclusionSphere(float X, float Y, float Z, float Radius);
void __cdecl SpacePopClearExclusionSpheres();
