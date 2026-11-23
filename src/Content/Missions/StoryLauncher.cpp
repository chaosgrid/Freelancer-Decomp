// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU16 (06ea6d50-06ea7750): the "StoryLauncher" controller.
// Retail object is 8 bytes: vptr 06fb43c0 (slot 0 LAB_06ea6f50) + ControllerId at +4.

#include "Core/ContentInternal.h"  // OpenContentDataIni (FUN_06f24280), G_ContentModule
#include "Core/ContentStaticIds.h" // TU16 static IDs 06ea6d50..06ea6f30
#include "NPCs/NpcShips.h"         // FNpcCharacterDesc, LoadNpcCharacterDesc, GSpecificNpcs (DAT_06fd081c)
#include "common_id.h"
#include "common_ini.h"
#include "common_server_controller.h"

#include <new>
#include <utility>

namespace
{
    class FStoryLauncher : public IDLL
    {
    public:
        explicit FStoryLauncher(unsigned int const* InControllerId);
        // Slot 1, FUN_06ea7330 -> FUN_06ea7350.
        ~FStoryLauncher() override;

        void HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms) override;

    private:
        unsigned int ControllerId;
    };

    // FUN_06ea6ff0; Parms is not read. Fills DAT_06fd081c from missions\specific_npc.ini; neither
    // the open nor FUN_06f32b70 is result-tested, and a repeated nickname keeps the first.
    FStoryLauncher::FStoryLauncher(unsigned int const* InControllerId)
        : ControllerId(*InControllerId)
    {
        pub::Controller::SetHeartbeatInterval(ControllerId, -1.0f);
        INI_Reader Reader;
        OpenContentDataIni("missions\\specific_npc.ini", &Reader);
        while (Reader.read_header())
        {
            if (!Reader.is_header("NPC"))
                continue;
            FNpcCharacterDesc Desc;
            ID_String Nickname;
            Nickname.clear();
            LoadNpcCharacterDesc(&Nickname, &Desc, &Reader);
            GSpecificNpcs.insert(std::make_pair(Nickname.Id, Desc));
        }
    }

    // FUN_06ea7350: empties the process-wide table DAT_06fd081c.
    FStoryLauncher::~FStoryLauncher()
    {
        GSpecificNpcs.clear();
    }

    // LAB_06ea6f50: message 7 carries the player id. The parameter object has a vtable
    // (06fb439c).
    void FStoryLauncher::HandleMessage(pub::Controller::EMessage MessageId, void const* MessageParms)
    {
        if (MessageId != pub::Controller::EMessage::PlayerLogin)
            return;

        pub::Controller::FStoryCreateParms StoryParms;
        StoryParms.PlayerId = *static_cast<unsigned int const*>(MessageParms);
        char ModulePath[0x104];
        GetModuleFileNameA(G_ContentModule, ModulePath, 0x104);
        pub::Controller::Create(ModulePath, "Story", &StoryParms, pub::Controller::NORMAL);
    }
}

IDLL* CreateStoryLauncher(char const* /*Name*/, unsigned int ControllerId, void const* /*Parms*/)
{
    // FUN_06eeec50.
    return new (std::nothrow) FStoryLauncher(&ControllerId);
}
