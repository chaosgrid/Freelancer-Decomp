// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once
// network.h - DirectPlay 8 transport layer types shared by RemoteClient.dll and remoteserver.dll
// Both DLLs are built from the same codebase (E:\FL\Scratch\Source\Network\)
// with different #defines (FL_SERVER for RemoteClient.dll).

#include <vector>
#include <windows.h>

// Forward declarations for external DirectPlay and interface types
#include "dalib_net.h"
#include "rpc_packet.h"   // EFlPacket, the packet id space

class IClient;
class IChatClient;
struct IServer;
class IChatServer;
// ============================================================================
// g) RemoteClient.dll server-side exports; declarations live in one public header.
// ============================================================================
#include "remoteclient.h"

// ============================================================================
// h) remoteserver.dll (client-side DirectPlay 8 transport) C exports
//
// Undecorated C exports returning the client's RPC connection to the remote server.
// ============================================================================
extern "C" {
    IServer* GetServerInterface();
    IChatServer* GetChatServerInterface();
}
