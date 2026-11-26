// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The first dword of a group-chat control message (FLPACKET_COMMON_CHAT_MSG to or from the
// group pseudo-id 0x00010004, GGroupChatSourceId). The client sends the first three to the
// server, the server answers each with one of the rest (ChatGroup_Invite / _Join / _Leave,
// FUN_06d19f20 / FUN_06d1a2e0 / FUN_06d1a6b0); the client's handler for them is
// nn_group_control.cpp, which names the text each one shows.
enum EGroupChatMsg
{
    kGroupMsgInvite = 0,                // client -> server: invite the player in the second dword
    kGroupMsgLeave = 1,                 // client -> server: leave the group
    kGroupMsgJoin = 2,                  // client -> server: accept the invitation of that player
    kGroupMsgInviteSent = 3,            // to the inviter
    kGroupMsgInviteReceived = 4,        // to the invitee
    kGroupMsgJoinedGroup = 5,           // to the joiner
    kGroupMsgCannotInviteSelf = 9,
    kGroupMsgGroupFull = 0xa,
    kGroupMsgAlreadyInYourGroup = 0xb,
    kGroupMsgCannotJoinOwnGroup = 0xc,
    kGroupMsgNotInvited = 0xd,
    kGroupMsgAlreadyMember = 0xe,
    kGroupMsgJoinFailedMission = 0xf,   // the other player has a group mission
    kGroupMsgGroupNowFull = 0x10,
    kGroupMsgNotInGroup = 0x12,
    kGroupMsgPlayerDied = 0x14,         // Server_BroadcastGroupControl from EqObj::destroy
};
