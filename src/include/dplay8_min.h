// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// Self-contained minimal DirectPlay 8 COM definitions (replaces legacy DX8 SDK dplay8.h).
// Every GUID, COM vtable slot, struct size, field offset, and message ID below is verified
// directly against retail Freelancer 1.1 DALib.dll and Microsoft dpnet.dll.

#pragma once

#include <windows.h>
#include <objbase.h>
#include <stddef.h>

#ifdef INITGUID
#include <initguid.h>
#else
#include <guiddef.h>
#endif

// ===========================================================================
// DirectPlay 8 GUIDs (as in retail DALib.dll .rdata)
// ===========================================================================

// {743F1DC6-5ABA-429F-8BDF-C54D03253DC2}
DEFINE_GUID(CLSID_DirectPlay8Client,
    0x743f1dc6, 0x5aba, 0x429f, 0x8b, 0xdf, 0xc5, 0x4d, 0x03, 0x25, 0x3d, 0xc2);

// {DA825E1B-6830-43D7-835D-0B5AD82956A2}
DEFINE_GUID(CLSID_DirectPlay8Server,
    0xda825e1b, 0x6830, 0x43d7, 0x83, 0x5d, 0x0b, 0x5a, 0xd8, 0x29, 0x56, 0xa2);

// {934A9523-A3CA-4BC5-ADA0-D6D95D979421}
DEFINE_GUID(CLSID_DirectPlay8Address,
    0x934a9523, 0xa3ca, 0x4bc5, 0xad, 0xa0, 0xd6, 0xd9, 0x5d, 0x97, 0x94, 0x21);

// {EBFE7BA0-628D-11D2-AE0F-006097B01411}
DEFINE_GUID(CLSID_DP8SP_TCPIP,
    0xebfe7ba0, 0x628d, 0x11d2, 0xae, 0x0f, 0x00, 0x60, 0x97, 0xb0, 0x14, 0x11);

// {5102DACD-241B-11D3-AEA7-006097B01411}
DEFINE_GUID(IID_IDirectPlay8Client,
    0x5102dacd, 0x241b, 0x11d3, 0xae, 0xa7, 0x00, 0x60, 0x97, 0xb0, 0x14, 0x11);

// {5102DACE-241B-11D3-AEA7-006097B01411}
DEFINE_GUID(IID_IDirectPlay8Server,
    0x5102dace, 0x241b, 0x11d3, 0xae, 0xa7, 0x00, 0x60, 0x97, 0xb0, 0x14, 0x11);

// {83783300-4063-4C8A-9DB3-82830A7FEB31}
DEFINE_GUID(IID_IDirectPlay8Address,
    0x83783300, 0x4063, 0x4c8a, 0x9d, 0xb3, 0x82, 0x83, 0x0a, 0x7f, 0xeb, 0x31);

// ===========================================================================
// Basic DirectPlay 8 types & constants
// ===========================================================================

typedef DWORD DPNID;
typedef DWORD DPNHANDLE;
typedef DPNHANDLE* PDPNHANDLE;

typedef HRESULT (WINAPI* PFNDPNMESSAGEHANDLER)(PVOID pvUserContext, DWORD dwMessageType, PVOID pMessage);

#ifndef DPNSUCCESS_PENDING
#define DPNSUCCESS_PENDING ((HRESULT)0x0015800EL)
#endif

// The two connect failures freelancer.exe tells apart (connect_handshake.cpp): the SDK's
// MAKE_DPNHRESULT(0x410) / (0x610). Freelancer reports them with resources.dll 0x73f ("the
// password you entered is not correct") and 0x73e ("that server is currently full").
#ifndef DPNERR_INVALIDPASSWORD
#define DPNERR_INVALIDPASSWORD ((HRESULT)0x80158410L)
#endif
#ifndef DPNERR_SESSIONFULL
#define DPNERR_SESSIONFULL ((HRESULT)0x80158610L)
#endif

#ifndef DPNA_DATATYPE_STRING
#define DPNA_DATATYPE_STRING 0x00000001UL
#define DPNA_DATATYPE_DWORD  0x00000002UL
#define DPNA_DATATYPE_GUID   0x00000003UL
#define DPNA_DATATYPE_BINARY 0x00000004UL
#endif

#ifndef DPNSESSION_REQUIREPASSWORD
#define DPNSESSION_CLIENT_SERVER    0x00000001UL
#define DPNSESSION_REQUIREPASSWORD  0x00000080UL
#endif

// Message IDs (retail DALib.dll jump tables at 0x065C1D58 / 0x065C4904)
#define DPN_MSGID_OFFSET                    0xFFFF0000UL
#define DPN_MSGID_ADD_PLAYER_TO_GROUP       (DPN_MSGID_OFFSET | 0x0001UL)
#define DPN_MSGID_APPLICATION_DESC          (DPN_MSGID_OFFSET | 0x0002UL)
#define DPN_MSGID_ASYNC_OP_COMPLETE         (DPN_MSGID_OFFSET | 0x0003UL)
#define DPN_MSGID_CLIENT_INFO               (DPN_MSGID_OFFSET | 0x0004UL)
#define DPN_MSGID_CONNECT_COMPLETE          (DPN_MSGID_OFFSET | 0x0005UL)
#define DPN_MSGID_CREATE_GROUP              (DPN_MSGID_OFFSET | 0x0006UL)
#define DPN_MSGID_CREATE_PLAYER             (DPN_MSGID_OFFSET | 0x0007UL)
#define DPN_MSGID_DESTROY_GROUP             (DPN_MSGID_OFFSET | 0x0008UL)
#define DPN_MSGID_DESTROY_PLAYER            (DPN_MSGID_OFFSET | 0x0009UL)
#define DPN_MSGID_ENUM_HOSTS_QUERY          (DPN_MSGID_OFFSET | 0x000AUL)
#define DPN_MSGID_ENUM_HOSTS_RESPONSE       (DPN_MSGID_OFFSET | 0x000BUL)
#define DPN_MSGID_GROUP_INFO                (DPN_MSGID_OFFSET | 0x000CUL)
#define DPN_MSGID_HOST_MIGRATE              (DPN_MSGID_OFFSET | 0x000DUL)
#define DPN_MSGID_INDICATE_CONNECT          (DPN_MSGID_OFFSET | 0x000EUL)
#define DPN_MSGID_INDICATED_CONNECT_ABORTED (DPN_MSGID_OFFSET | 0x000FUL)
#define DPN_MSGID_PEER_INFO                 (DPN_MSGID_OFFSET | 0x0010UL)
#define DPN_MSGID_RECEIVE                   (DPN_MSGID_OFFSET | 0x0011UL)
#define DPN_MSGID_REMOVE_PLAYER_FROM_GROUP  (DPN_MSGID_OFFSET | 0x0012UL)
#define DPN_MSGID_RETURN_BUFFER             (DPN_MSGID_OFFSET | 0x0013UL)
#define DPN_MSGID_SEND_COMPLETE             (DPN_MSGID_OFFSET | 0x0014UL)
#define DPN_MSGID_SERVER_INFO               (DPN_MSGID_OFFSET | 0x0015UL)
#define DPN_MSGID_TERMINATE_SESSION         (DPN_MSGID_OFFSET | 0x0016UL)

// Forward declarations
struct IDirectPlay8Address;
struct IDirectPlay8Client;
struct IDirectPlay8Server;
struct IDirectPlay8LobbiedApplication;

// ===========================================================================
// DirectPlay 8 structures (layouts as retail DALib.dll uses them)
// ===========================================================================

typedef struct _DPN_APPLICATION_DESC
{
    DWORD dwSize;
    DWORD dwFlags;
    GUID guidInstance;
    GUID guidApplication;
    DWORD dwMaxPlayers;
    DWORD dwCurrentPlayers;
    WCHAR* pwszSessionName;
    WCHAR* pwszPassword;
    PVOID pvReservedData;
    DWORD dwReservedDataSize;
    PVOID pvApplicationReservedData;
    DWORD dwApplicationReservedDataSize;
} DPN_APPLICATION_DESC, *PDPN_APPLICATION_DESC;

static_assert(sizeof(DPN_APPLICATION_DESC) == 0x48, "DPN_APPLICATION_DESC sizeof 0x48");
static_assert(offsetof(DPN_APPLICATION_DESC, guidApplication) == 0x18, "guidApplication offset");
static_assert(offsetof(DPN_APPLICATION_DESC, pwszSessionName) == 0x30, "pwszSessionName offset");

typedef struct _BUFFERDESC
{
    DWORD dwBufferSize;
    BYTE* pBufferData;
} BUFFERDESC, DPN_BUFFER_DESC, *PDPN_BUFFER_DESC;

static_assert(sizeof(DPN_BUFFER_DESC) == 0x08, "DPN_BUFFER_DESC sizeof 0x08");

typedef struct _DPN_CAPS
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwConnectTimeout;
    DWORD dwConnectRetries;
    DWORD dwKeepAliveTimeout;
} DPN_CAPS, *PDPN_CAPS;

static_assert(sizeof(DPN_CAPS) == 0x14, "DPN_CAPS sizeof 0x14 (DX8.1)");

typedef struct _DPN_SP_CAPS
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwNumThreads;
    DWORD dwDefaultEnumCount;
    DWORD dwDefaultEnumRetryInterval;
    DWORD dwDefaultEnumTimeout;
    DWORD dwMaxEnumPayloadSize;
    DWORD dwBuffersPerThread;
    DWORD dwSystemBufferSize;
} DPN_SP_CAPS, *PDPN_SP_CAPS;

typedef struct _DPN_CONNECTION_INFO
{
    DWORD dwSize;
    DWORD dwRoundTripLatencyMS;
    DWORD dwThroughputBPS;
    DWORD dwPeakThroughputBPS;
    DWORD dwBytesSentGuaranteed;
    DWORD dwPacketsSentGuaranteed;
    DWORD dwBytesSentNonGuaranteed;
    DWORD dwPacketsSentNonGuaranteed;
    DWORD dwBytesRetried;
    DWORD dwPacketsRetried;
    DWORD dwBytesDropped;
    DWORD dwPacketsDropped;
    DWORD dwMessagesTransmittedHighPriority;
    DWORD dwMessagesTimedOutHighPriority;
    DWORD dwMessagesTransmittedNormalPriority;
    DWORD dwMessagesTimedOutNormalPriority;
    DWORD dwMessagesTransmittedLowPriority;
    DWORD dwMessagesTimedOutLowPriority;
    DWORD dwBytesReceivedGuaranteed;
    DWORD dwPacketsReceivedGuaranteed;
    DWORD dwBytesReceivedNonGuaranteed;
    DWORD dwPacketsReceivedNonGuaranteed;
    DWORD dwMessagesReceived;
} DPN_CONNECTION_INFO, *PDPN_CONNECTION_INFO;

static_assert(sizeof(DPN_CONNECTION_INFO) == 0x5C, "DPN_CONNECTION_INFO sizeof 0x5C");

typedef struct _DPN_PLAYER_INFO
{
    DWORD dwSize;
    DWORD dwInfoFlags;
    PWSTR pwszName;
    PVOID pvData;
    DWORD dwDataSize;
    DWORD dwPlayerFlags;
} DPN_PLAYER_INFO, *PDPN_PLAYER_INFO;

typedef struct _DPN_GROUP_INFO
{
    DWORD dwSize;
    DWORD dwInfoFlags;
    PWSTR pwszName;
    PVOID pvData;
    DWORD dwDataSize;
    DWORD dwGroupFlags;
} DPN_GROUP_INFO, *PDPN_GROUP_INFO;

typedef struct _DPN_SERVICE_PROVIDER_INFO
{
    DWORD dwFlags;
    GUID guid;
    PWSTR pwszName;
    PVOID pvReserved;
    DWORD dwReserved;
} DPN_SERVICE_PROVIDER_INFO, *PDPN_SERVICE_PROVIDER_INFO;

typedef struct _DPN_SECURITY_DESC
{
    DWORD dwSize;
    DWORD dwFlags;
} DPN_SECURITY_DESC, *PDPN_SECURITY_DESC;

typedef struct _DPN_SECURITY_CREDENTIALS
{
    DWORD dwSize;
    DWORD dwFlags;
} DPN_SECURITY_CREDENTIALS, *PDPN_SECURITY_CREDENTIALS;

// ===========================================================================
// Callback message structures
// ===========================================================================

typedef struct _DPNMSG_ASYNC_OP_COMPLETE
{
    DWORD dwSize;
    DPNHANDLE hAsyncOp;
    PVOID pvUserContext;
    HRESULT hResultCode;
} DPNMSG_ASYNC_OP_COMPLETE, *PDPNMSG_ASYNC_OP_COMPLETE;

typedef struct _DPNMSG_CONNECT_COMPLETE
{
    DWORD dwSize;
    DPNHANDLE hAsyncOp;
    PVOID pvUserContext;
    HRESULT hResultCode;
    PVOID pvApplicationReplyData;
    DWORD dwApplicationReplyDataSize;
} DPNMSG_CONNECT_COMPLETE, *PDPNMSG_CONNECT_COMPLETE;

static_assert(offsetof(DPNMSG_CONNECT_COMPLETE, hResultCode) == 0x0C, "hResultCode +0x0C");

typedef struct _DPNMSG_CREATE_PLAYER
{
    DWORD dwSize;
    DPNID dpnidPlayer;
    PVOID pvPlayerContext;
} DPNMSG_CREATE_PLAYER, *PDPNMSG_CREATE_PLAYER;

typedef struct _DPNMSG_DESTROY_PLAYER
{
    DWORD dwSize;
    DPNID dpnidPlayer;
    PVOID pvPlayerContext;
    DWORD dwReason;
} DPNMSG_DESTROY_PLAYER, *PDPNMSG_DESTROY_PLAYER;

typedef struct _DPNMSG_ENUM_HOSTS_QUERY
{
    DWORD dwSize;
    IDirectPlay8Address* pAddressSender;
    IDirectPlay8Address* pAddressDevice;
    PVOID pvReceivedData;
    DWORD dwReceivedDataSize;
    DWORD dwMaxResponseDataSize;
    PVOID pvResponseData;
    DWORD dwResponseDataSize;
    PVOID pvResponseContext;
} DPNMSG_ENUM_HOSTS_QUERY, *PDPNMSG_ENUM_HOSTS_QUERY;

static_assert(offsetof(DPNMSG_ENUM_HOSTS_QUERY, pvResponseData) == 0x18, "pvResponseData +0x18");
static_assert(offsetof(DPNMSG_ENUM_HOSTS_QUERY, dwResponseDataSize) == 0x1C, "dwResponseDataSize +0x1C");

typedef struct _DPNMSG_ENUM_HOSTS_RESPONSE
{
    DWORD dwSize;
    IDirectPlay8Address* pAddressSender;
    IDirectPlay8Address* pAddressDevice;
    const DPN_APPLICATION_DESC* pApplicationDescription;
    PVOID pvResponseData;
    DWORD dwResponseDataSize;
    PVOID pvUserContext;
    DWORD dwRoundTripLatencyMS;
} DPNMSG_ENUM_HOSTS_RESPONSE, *PDPNMSG_ENUM_HOSTS_RESPONSE;

static_assert(sizeof(DPNMSG_ENUM_HOSTS_RESPONSE) == 0x20, "DPNMSG_ENUM_HOSTS_RESPONSE sizeof 0x20");

typedef struct _DPNMSG_INDICATE_CONNECT
{
    DWORD dwSize;
    PVOID pvUserConnectData;
    DWORD dwUserConnectDataSize;
    PVOID pvReplyData;
    DWORD dwReplyDataSize;
    PVOID pvReplyContext;
    PVOID pvPlayerContext;
    IDirectPlay8Address* pAddressPlayer;
    IDirectPlay8Address* pAddressDevice;
} DPNMSG_INDICATE_CONNECT, *PDPNMSG_INDICATE_CONNECT;

static_assert(offsetof(DPNMSG_INDICATE_CONNECT, pvPlayerContext) == 0x18, "pvPlayerContext +0x18");

typedef struct _DPNMSG_INDICATED_CONNECT_ABORTED
{
    DWORD dwSize;
    PVOID pvPlayerContext;
} DPNMSG_INDICATED_CONNECT_ABORTED, *PDPNMSG_INDICATED_CONNECT_ABORTED;

typedef struct _DPNMSG_RECEIVE
{
    DWORD dwSize;
    DPNID dpnidSender;
    PVOID pvPlayerContext;
    PBYTE pReceiveData;
    DWORD dwReceiveDataSize;
    DPNHANDLE hBufferHandle;
} DPNMSG_RECEIVE, *PDPNMSG_RECEIVE;

static_assert(offsetof(DPNMSG_RECEIVE, pReceiveData) == 0x0C, "pReceiveData +0x0C");
static_assert(offsetof(DPNMSG_RECEIVE, dwReceiveDataSize) == 0x10, "dwReceiveDataSize +0x10");
static_assert(offsetof(DPNMSG_RECEIVE, hBufferHandle) == 0x14, "hBufferHandle +0x14");

typedef struct _DPNMSG_SEND_COMPLETE
{
    DWORD dwSize;
    DPNHANDLE hAsyncOp;
    PVOID pvUserContext;
    HRESULT hResultCode;
    DWORD dwSendTime;
} DPNMSG_SEND_COMPLETE, *PDPNMSG_SEND_COMPLETE;

static_assert(offsetof(DPNMSG_SEND_COMPLETE, pvUserContext) == 0x08, "pvUserContext +0x08");
static_assert(offsetof(DPNMSG_SEND_COMPLETE, dwSendTime) == 0x10, "dwSendTime +0x10");

typedef struct _DPNMSG_TERMINATE_SESSION
{
    DWORD dwSize;
    HRESULT hResultCode;
    PVOID pvTerminateData;
    DWORD dwTerminateDataSize;
} DPNMSG_TERMINATE_SESSION, *PDPNMSG_TERMINATE_SESSION;

// ===========================================================================
// COM Interfaces (vtable slot order of Microsoft dpnet.dll)
// ===========================================================================

struct IDirectPlay8Address : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE BuildFromURLW(WCHAR* pwszSourceURL) = 0;                          // +0x0C
    virtual HRESULT STDMETHODCALLTYPE BuildFromURLA(CHAR* pszSourceURL) = 0;                            // +0x10
    virtual HRESULT STDMETHODCALLTYPE Duplicate(IDirectPlay8Address** ppdpaNewAddress) = 0;             // +0x14
    virtual HRESULT STDMETHODCALLTYPE SetEqual(IDirectPlay8Address* pdpaAddress) = 0;                   // +0x18
    virtual HRESULT STDMETHODCALLTYPE IsEqual(IDirectPlay8Address* pdpaAddress) = 0;                    // +0x1C
    virtual HRESULT STDMETHODCALLTYPE Clear() = 0;                                                      // +0x20
    virtual HRESULT STDMETHODCALLTYPE GetURLW(WCHAR* pwszURL, PDWORD pdwNumChars) = 0;                  // +0x24
    virtual HRESULT STDMETHODCALLTYPE GetURLA(CHAR* pszURL, PDWORD pdwNumChars) = 0;                    // +0x28
    virtual HRESULT STDMETHODCALLTYPE GetSP(GUID* pguidSP) = 0;                                         // +0x2C
    virtual HRESULT STDMETHODCALLTYPE GetUserData(void* pvUserData, PDWORD pdwBufferSize) = 0;          // +0x30
    virtual HRESULT STDMETHODCALLTYPE SetSP(const GUID* const pguidSP) = 0;                             // +0x34
    virtual HRESULT STDMETHODCALLTYPE SetUserData(const void* const pvUserData, const DWORD dwDataSize) = 0; // +0x38
    virtual HRESULT STDMETHODCALLTYPE GetNumComponents(PDWORD pdwNumComponents) = 0;                    // +0x3C
    virtual HRESULT STDMETHODCALLTYPE GetComponentByName(const WCHAR* const pwszName, void* pvBuffer,
                                                         PDWORD pdwBufferSize, PDWORD pdwDataType) = 0; // +0x40
    virtual HRESULT STDMETHODCALLTYPE GetComponentByIndex(const DWORD dwComponentID, WCHAR* pwszName,
                                                          PDWORD pdwNameLen, void* pvBuffer,
                                                          PDWORD pdwBufferSize, PDWORD pdwDataType) = 0; // +0x44
    virtual HRESULT STDMETHODCALLTYPE AddComponent(const WCHAR* const pwszName, const void* const lpvData,
                                                   const DWORD dwDataSize, const DWORD dwDataType) = 0; // +0x48
    virtual HRESULT STDMETHODCALLTYPE GetDevice(GUID* pguidDevice) = 0;                                 // +0x4C
    virtual HRESULT STDMETHODCALLTYPE SetDevice(const GUID* const pguidDevice) = 0;                     // +0x50
    virtual HRESULT STDMETHODCALLTYPE BuildFromDirectPlay4Address(LPVOID pvAddress, DWORD dwDataSize) = 0; // +0x54
};

struct IDirectPlay8Client : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Initialize(PVOID const pvUserContext, const PFNDPNMESSAGEHANDLER pfn,
                                                 const DWORD dwFlags) = 0;                              // +0x0C
    virtual HRESULT STDMETHODCALLTYPE EnumServiceProviders(const GUID* const pguidServiceProvider,
                                                           const GUID* const pguidApplication,
                                                           DPN_SERVICE_PROVIDER_INFO* const pSPInfoBuffer,
                                                           PDWORD const pcbEnumData, PDWORD const pcReturned,
                                                           const DWORD dwFlags) = 0;                    // +0x10
    virtual HRESULT STDMETHODCALLTYPE EnumHosts(PDPN_APPLICATION_DESC const pApplicationDesc,
                                                IDirectPlay8Address* const pAddrHost,
                                                IDirectPlay8Address* const pDeviceInfo,
                                                PVOID const pUserEnumData, const DWORD dwUserEnumDataSize,
                                                const DWORD dwEnumCount, const DWORD dwRetryInterval,
                                                const DWORD dwTimeOut, PVOID const pvUserContext,
                                                DPNHANDLE* const pAsyncHandle, const DWORD dwFlags) = 0; // +0x14
    virtual HRESULT STDMETHODCALLTYPE CancelAsyncOperation(const DPNHANDLE hAsyncHandle,
                                                           const DWORD dwFlags) = 0;                    // +0x18
    virtual HRESULT STDMETHODCALLTYPE Connect(const DPN_APPLICATION_DESC* const pdnAppDesc,
                                              IDirectPlay8Address* const pHostAddr,
                                              IDirectPlay8Address* const pDeviceInfo,
                                              const DPN_SECURITY_DESC* const pdnSecurity,
                                              const DPN_SECURITY_CREDENTIALS* const pdnCredentials,
                                              const void* const pvUserConnectData,
                                              const DWORD dwUserConnectDataSize,
                                              void* const pvAsyncContext,
                                              DPNHANDLE* const phAsyncHandle, const DWORD dwFlags) = 0; // +0x1C
    virtual HRESULT STDMETHODCALLTYPE Send(const DPN_BUFFER_DESC* const prgBufferDesc,
                                           const DWORD cBufferDesc, const DWORD dwTimeOut,
                                           void* const pvAsyncContext, DPNHANDLE* const phAsyncHandle,
                                           const DWORD dwFlags) = 0;                                    // +0x20
    virtual HRESULT STDMETHODCALLTYPE GetSendQueueInfo(DWORD* const pdwNumMsgs, DWORD* const pdwNumBytes,
                                                       const DWORD dwFlags) = 0;                        // +0x24
    virtual HRESULT STDMETHODCALLTYPE GetApplicationDesc(DPN_APPLICATION_DESC* const pAppDescBuffer,
                                                         DWORD* const pcbDataSize,
                                                         const DWORD dwFlags) = 0;                      // +0x28
    virtual HRESULT STDMETHODCALLTYPE SetClientInfo(const DPN_PLAYER_INFO* const pdpnPlayerInfo,
                                                    PVOID const pvAsyncContext,
                                                    DPNHANDLE* const phAsyncHandle,
                                                    const DWORD dwFlags) = 0;                           // +0x2C
    virtual HRESULT STDMETHODCALLTYPE GetServerInfo(DPN_PLAYER_INFO* const pdpnPlayerInfo,
                                                    DWORD* const pdwSize, const DWORD dwFlags) = 0;     // +0x30
    virtual HRESULT STDMETHODCALLTYPE GetServerAddress(IDirectPlay8Address** const pAddress,
                                                       const DWORD dwFlags) = 0;                        // +0x34
    virtual HRESULT STDMETHODCALLTYPE Close(const DWORD dwFlags) = 0;                                   // +0x38
    virtual HRESULT STDMETHODCALLTYPE ReturnBuffer(const DPNHANDLE hBufferHandle,
                                                   const DWORD dwFlags) = 0;                            // +0x3C
    virtual HRESULT STDMETHODCALLTYPE GetCaps(DPN_CAPS* const pdpCaps, const DWORD dwFlags) = 0;        // +0x40
    virtual HRESULT STDMETHODCALLTYPE SetCaps(const DPN_CAPS* const pdpCaps, const DWORD dwFlags) = 0;  // +0x44
    virtual HRESULT STDMETHODCALLTYPE SetSPCaps(const GUID* const pguidSP,
                                                const DPN_SP_CAPS* const pdpspCaps,
                                                const DWORD dwFlags) = 0;                               // +0x48
    virtual HRESULT STDMETHODCALLTYPE GetSPCaps(const GUID* const pguidSP, DPN_SP_CAPS* const pdpspCaps,
                                                const DWORD dwFlags) = 0;                               // +0x4C
    virtual HRESULT STDMETHODCALLTYPE GetConnectionInfo(DPN_CONNECTION_INFO* const pdpConnectionInfo,
                                                        const DWORD dwFlags) = 0;                       // +0x50
    virtual HRESULT STDMETHODCALLTYPE RegisterLobby(const DPNHANDLE dpnHandle,
                                                    IDirectPlay8LobbiedApplication* const pIDP8LobbiedApplication,
                                                    const DWORD dwFlags) = 0;                           // +0x54
};

struct IDirectPlay8Server : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Initialize(PVOID const pvUserContext, const PFNDPNMESSAGEHANDLER pfn,
                                                 const DWORD dwFlags) = 0;                              // +0x0C
    virtual HRESULT STDMETHODCALLTYPE EnumServiceProviders(const GUID* const pguidServiceProvider,
                                                           const GUID* const pguidApplication,
                                                           DPN_SERVICE_PROVIDER_INFO* const pSPInfoBuffer,
                                                           PDWORD const pcbEnumData, PDWORD const pcReturned,
                                                           const DWORD dwFlags) = 0;                    // +0x10
    virtual HRESULT STDMETHODCALLTYPE CancelAsyncOperation(const DPNHANDLE hAsyncHandle,
                                                           const DWORD dwFlags) = 0;                    // +0x14
    virtual HRESULT STDMETHODCALLTYPE GetSendQueueInfo(const DPNID dpnid, DWORD* const pdwNumMsgs,
                                                       DWORD* const pdwNumBytes, const DWORD dwFlags) = 0; // +0x18
    virtual HRESULT STDMETHODCALLTYPE GetApplicationDesc(DPN_APPLICATION_DESC* const pAppDescBuffer,
                                                         DWORD* const pcbDataSize,
                                                         const DWORD dwFlags) = 0;                      // +0x1C
    virtual HRESULT STDMETHODCALLTYPE SetServerInfo(const DPN_PLAYER_INFO* const pdpnPlayerInfo,
                                                    PVOID const pvAsyncContext,
                                                    DPNHANDLE* const phAsyncHandle,
                                                    const DWORD dwFlags) = 0;                           // +0x20
    virtual HRESULT STDMETHODCALLTYPE GetClientInfo(const DPNID dpnid,
                                                    DPN_PLAYER_INFO* const pdpnPlayerInfo,
                                                    DWORD* const pdwSize, const DWORD dwFlags) = 0;     // +0x24
    virtual HRESULT STDMETHODCALLTYPE GetClientAddress(const DPNID dpnid,
                                                       IDirectPlay8Address** const pAddress,
                                                       const DWORD dwFlags) = 0;                        // +0x28
    virtual HRESULT STDMETHODCALLTYPE GetLocalHostAddresses(IDirectPlay8Address** const prgpAddress,
                                                            DWORD* const pcAddress,
                                                            const DWORD dwFlags) = 0;                   // +0x2C
    virtual HRESULT STDMETHODCALLTYPE SetApplicationDesc(const DPN_APPLICATION_DESC* const pad,
                                                         const DWORD dwFlags) = 0;                      // +0x30
    virtual HRESULT STDMETHODCALLTYPE Host(const DPN_APPLICATION_DESC* const pdnAppDesc,
                                           IDirectPlay8Address** const prgpDeviceInfo,
                                           const DWORD cDeviceInfo,
                                           const DPN_SECURITY_DESC* const pdnSecurity,
                                           const DPN_SECURITY_CREDENTIALS* const pdnCredentials,
                                           void* const pvPlayerContext, const DWORD dwFlags) = 0;       // +0x34
    virtual HRESULT STDMETHODCALLTYPE SendTo(const DPNID dpnid, const DPN_BUFFER_DESC* const prgBufferDesc,
                                             const DWORD cBufferDesc, const DWORD dwTimeOut,
                                             void* const pvAsyncContext, DPNHANDLE* const phAsyncHandle,
                                             const DWORD dwFlags) = 0;                                  // +0x38
    virtual HRESULT STDMETHODCALLTYPE CreateGroup(const DPN_GROUP_INFO* const pdpnGroupInfo,
                                                  void* const pvGroupContext, void* const pvAsyncContext,
                                                  DPNHANDLE* const phAsyncHandle, const DWORD dwFlags) = 0; // +0x3C
    virtual HRESULT STDMETHODCALLTYPE DestroyGroup(const DPNID idGroup, PVOID const pvAsyncContext,
                                                   DPNHANDLE* const phAsyncHandle, const DWORD dwFlags) = 0; // +0x40
    virtual HRESULT STDMETHODCALLTYPE AddPlayerToGroup(const DPNID idGroup, const DPNID idClient,
                                                       PVOID const pvAsyncContext,
                                                       DPNHANDLE* const phAsyncHandle,
                                                       const DWORD dwFlags) = 0;                        // +0x44
    virtual HRESULT STDMETHODCALLTYPE RemovePlayerFromGroup(const DPNID idGroup, const DPNID idClient,
                                                            PVOID const pvAsyncContext,
                                                            DPNHANDLE* const phAsyncHandle,
                                                            const DWORD dwFlags) = 0;                   // +0x48
    virtual HRESULT STDMETHODCALLTYPE SetGroupInfo(const DPNID dpnid, DPN_GROUP_INFO* const pdpnGroupInfo,
                                                   PVOID const pvAsyncContext,
                                                   DPNHANDLE* const phAsyncHandle, const DWORD dwFlags) = 0; // +0x4C
    virtual HRESULT STDMETHODCALLTYPE GetGroupInfo(const DPNID dpnid, DPN_GROUP_INFO* const pdpnGroupInfo,
                                                   DWORD* const pdwSize, const DWORD dwFlags) = 0;      // +0x50
    virtual HRESULT STDMETHODCALLTYPE EnumPlayersAndGroups(DPNID* const prgdpnid, DWORD* const pcdpnid,
                                                           const DWORD dwFlags) = 0;                    // +0x54
    virtual HRESULT STDMETHODCALLTYPE EnumGroupMembers(const DPNID dpnid, DPNID* const prgdpnid,
                                                       DWORD* const pcdpnid, const DWORD dwFlags) = 0;  // +0x58
    virtual HRESULT STDMETHODCALLTYPE Close(const DWORD dwFlags) = 0;                                   // +0x5C
    virtual HRESULT STDMETHODCALLTYPE DestroyClient(const DPNID dpnidClient,
                                                    const void* const pvDestroyData,
                                                    const DWORD dwDestroyDataSize,
                                                    const DWORD dwFlags) = 0;                           // +0x60
    virtual HRESULT STDMETHODCALLTYPE ReturnBuffer(const DPNHANDLE hBufferHandle,
                                                   const DWORD dwFlags) = 0;                            // +0x64
    virtual HRESULT STDMETHODCALLTYPE GetPlayerContext(const DPNID dpnid, PVOID* const ppvPlayerContext,
                                                       const DWORD dwFlags) = 0;                        // +0x68
    virtual HRESULT STDMETHODCALLTYPE GetGroupContext(const DPNID dpnid, PVOID* const ppvGroupContext,
                                                      const DWORD dwFlags) = 0;                         // +0x6C
    virtual HRESULT STDMETHODCALLTYPE GetCaps(DPN_CAPS* const pdpCaps, const DWORD dwFlags) = 0;        // +0x70
    virtual HRESULT STDMETHODCALLTYPE SetCaps(const DPN_CAPS* const pdpCaps, const DWORD dwFlags) = 0;  // +0x74
    virtual HRESULT STDMETHODCALLTYPE SetSPCaps(const GUID* const pguidSP,
                                                const DPN_SP_CAPS* const pdpspCaps,
                                                const DWORD dwFlags) = 0;                               // +0x78
    virtual HRESULT STDMETHODCALLTYPE GetSPCaps(const GUID* const pguidSP, DPN_SP_CAPS* const pdpspCaps,
                                                const DWORD dwFlags) = 0;                               // +0x7C
    virtual HRESULT STDMETHODCALLTYPE GetConnectionInfo(const DPNID dpnid,
                                                        DPN_CONNECTION_INFO* const pdpConnectionInfo,
                                                        const DWORD dwFlags) = 0;                       // +0x80
    virtual HRESULT STDMETHODCALLTYPE RegisterLobby(const DPNHANDLE dpnHandle,
                                                    IDirectPlay8LobbiedApplication* const pIDP8LobbiedApplication,
                                                    const DWORD dwFlags) = 0;                           // +0x84
};
