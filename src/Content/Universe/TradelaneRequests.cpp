// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU55: trade lane request handlers. See TradelaneRequests.h.

#include "Universe/TradelaneRequests.h"

#include "Core/ContentStaticIds.h"     // TU55 static IDs (DAT_06fd1d4c..DAT_06fd1d10)
#include "Core/ContentInternal.h"     // EObjectType
#include "Core/ContentPlayerState.h"   // FindContentPlayerStateByShip (FUN_06f49bc0)
#include "NPCs/DockQueues.h"           // FGcsDockGrantedEvent (vtable 06fbbd70)
#include "Comms/Gcs.h"                  // GGcsConversations (DAT_06fd1c08)
#include "Universe/SystemInfo.h"           // FindSystemInfo (FUN_06f75f30)
#include "common_server.h"        // pub::SpaceObj::GetType / Activate / LaneResponse

#include <cstdint>

// FUN_06f6fcb0. The response stays 5 unless the object asked for is a trade lane ring (bit 7
// of GetType's low byte). A player ship whose route check (TU46 FUN_06f47ee0) answers 1 or 2
// gets response 1 (no event); any other answer, and every non-player ship, gets response 4
// with the GCS "granted" event (type 2) queued (FUN_06ee8d70, its float dropped).
void HandleTradelaneRequest(FTradelaneRequest const* Request)
{
    // Retail's out slot is the argument's own stack slot, so a failed GetType (it writes
    // nothing on -4/-2) leaves the parameter pointer there and its low byte is tested.
    unsigned int RingType = static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(Request));
    pub::SpaceObj::GetType(Request->RingId, RingType);
    int Response = DOCK_HOST_RESPONSE_NO_TERMINAL;
    if ((RingType & OBJ_TRADELANE_RING) != 0)
    {
        bool bAnswered = false;
        FContentPlayerState* Player = FindContentPlayerStateByShip(&Request->ShipId);
        if (Player != nullptr)
        {
            int const Check = Player->CheckTradelaneRoute(Request->RingId, Request->EndRingId); // FUN_06f47ee0
            if (Check == 1)
            {
                Response = Check;
                bAnswered = true;
            }
            else if (Check == 2)
            {
                Response = DOCK_HOST_RESPONSE_ACCESS_DENIED;
                bAnswered = true;
            }
        }
        if (!bAnswered)
        {
            Response = DOCK_HOST_RESPONSE_PROCEED;
            FGcsDockGrantedEvent Event;
            Event.System = FindSystemInfo(&Request->SystemId);
            Event.Speaker = Request->RingId;
            Event.Listener = Request->ShipId;
            Event.DockIndex = -1;
            Event.DockType = 0;
            Event.bWaitedInQueue = false;
            Event.Response = Response;
            GGcsConversations.QueueConversation(&Event);
        }
    }
    pub::SpaceObj::LaneResponse(Request->ShipId, Response);
    if (Response == DOCK_HOST_RESPONSE_PROCEED)
        pub::SpaceObj::Activate(Request->RingId, true, static_cast<int>(Request->ShipId));
}

// FUN_06f6fc90.
void HandleTradelaneRelease(FTradelaneRequest const* Request)
{
    pub::SpaceObj::Activate(Request->RingId, false, static_cast<int>(Request->ShipId));
}
