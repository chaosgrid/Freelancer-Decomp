// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_goods.h"
#include "../include/common_universe.h"
#include "../include/common_archetype.h"
#include "../include/common_cobject.h"
#include "../include/server_exports.h"
#include "../include/server_cobject.h"
#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"
#include "../include/rpc_iclient.h"
#include "server_internal.h"
#include <windows.h>
#include <new>
#include <vector>

namespace pub {
namespace SpaceObj {

// SpaceObj_BuildTransportLoadout -- retail FUN_06d5f990 (cdecl), sole caller
// pub::SpaceObj::Create (ship archetype, &EquipIdMaker, &ShipInfo+0x5c, &request+0x70).
// Named after its own FDUMP text ("Transport loadout aborted").
// With cargo-pod hardpoints only the first two cargo nodes are used: per node, each hp
// gets a mounted pod plus an unmounted cargo item of Count / hp-count, and the last
// element takes the remainder. A good without pod_appearance aborts the whole loadout.
// Without pod hardpoints every node becomes one internal cargo item.
// Each append is retail's insert(end, 1, EquipDesc()) followed by setters on the new
// last element; add_equipment_item(.., false) never folds.
static void SpaceObj_AppendDefaultEquip(EquipDescVector& Out) {
    EquipDesc const Blank;
    Out.add_equipment_item(Blank, false);
}

void SpaceObj_BuildTransportLoadout(Archetype::Ship const* Arch,
                                           SubObjectID::EquipIdMaker* IdMaker,
                                           OwnerList<CargoDesc> const* Cargo,
                                           EquipDescVector* Out) {
    // Reserve size + cargo node count.
    unsigned int Need = (Out->Begin == nullptr) ? 0u
        : static_cast<unsigned int>(Out->End - Out->Begin);
    for (MetaNode<CargoDesc> const* Node = Cargo->get_head(); Node != nullptr; Node = Node->Next)
        ++Need;
    unsigned int const Capacity = (Out->Begin == nullptr) ? 0u
        : static_cast<unsigned int>(Out->Cap - Out->Begin);
    if (Capacity < Need) {                                          // unsigned compare
        int const AllocCount = static_cast<int>(Need) >= 0 ? static_cast<int>(Need) : 0;
        EquipDesc* const NewBuf = static_cast<EquipDesc*>(
            ::operator new(static_cast<size_t>(AllocCount) * sizeof(EquipDesc)));
        EquipDesc* Dst = NewBuf;
        for (EquipDesc* Src = Out->Begin; Src != Out->End; ++Src, ++Dst)
            new (Dst) EquipDesc(*Src);
        // _Destroy is a no-op: EquipDesc's destructor is trivial.
        unsigned int const OldSize = (Out->Begin == nullptr) ? 0u
            : static_cast<unsigned int>(Out->End - Out->Begin);
        ::operator delete(Out->Begin);                              // FUN_06d5ffb0
        Out->Cap = NewBuf + Need;
        Out->End = NewBuf + OldSize;
        Out->Begin = NewBuf;
    }

    std::vector<CacheString> const* const Hps = Arch->get_legal_hps(HpTypeCargoPod);
    if (Hps != nullptr && !Hps->empty()) {
        int const HpCount = static_cast<int>(Hps->size());
        int Index = 0;
        for (MetaNode<CargoDesc> const* Node = Cargo->get_head(); Node != nullptr;
             Node = Node->Next) {
            CargoDesc const* const Desc = Node->Data;
            GoodInfo const* const Good = GoodList::find_by_id(Desc->GoodId);
            unsigned int const ArchId = Good->ArchId;
            Archetype::Equipment* const Eq = Archetype::GetEquipment(ArchId);
            Archetype::CargoPod* const Pod = Eq->get_pod_appearance();
            if (Pod == nullptr) {
                FDUMP(kFdumpWarning,
                      "%s(%d) : *** WARNING: Equipment(%s) has no pod_appearance.  Transport loadout aborted.",
                      "E:\\FL\\Scratch\\Source\\Server\\SrvMisc.cpp", 0x3a,
                      Eq->Nickname.get_string());
                return;
            }
            // Retail divides via FILD/FIDIV/ftol; both operands fit a double exactly and
            // ftol truncates, so this is the same integer.
            int const PerHp = static_cast<int>(
                static_cast<double>(Desc->Count) / static_cast<double>(HpCount));
            int Left = static_cast<int>(Desc->Count);
            for (std::vector<CacheString>::const_iterator Hp = Hps->begin();
                 Hp != Hps->end(); ++Hp) {
                SpaceObj_AppendDefaultEquip(*Out);
                EquipDesc* Last = Out->End - 1;
                Last->set_id(IdMaker->CreateEquipID());
                Last->set_arch_id(Pod->Nickname.Id);
                Last->set_count(1);
                Last->set_status(1.0f);
                Last->set_hardpoint(*Hp);
                Last->set_equipped(true);

                SpaceObj_AppendDefaultEquip(*Out);
                Last = Out->End - 1;
                Last->set_id(IdMaker->CreateEquipID());
                Last->set_arch_id(ArchId);
                Last->set_status(1.0f);
                Last->set_hardpoint(*Hp);
                Last->set_equipped(false);
                Last->set_count(PerHp);
                Left -= PerHp;
            }
            (Out->End - 1)->set_count(Left + PerHp);
            if (++Index > 1)                                        // first two nodes only
                return;
        }
        return;
    }

    // No cargo-pod hardpoint: every node.
    for (MetaNode<CargoDesc> const* Node = Cargo->get_head(); Node != nullptr; Node = Node->Next) {
        CargoDesc const* const Desc = Node->Data;
        SpaceObj_AppendDefaultEquip(*Out);
        EquipDesc* const Last = Out->End - 1;
        Last->set_id(IdMaker->CreateEquipID());
        Last->set_arch_id(Good2Arch(Desc->GoodId));
        Last->set_count(static_cast<int>(Desc->Count));
        Last->set_status(1.0f);
        Last->make_internal();
        Last->set_equipped(false);
    }
}

} // namespace SpaceObj
} // namespace pub
