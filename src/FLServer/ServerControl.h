// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// FServerControl -- the 0x9b0-byte server-host settings object FLServer keeps by value inside
// the app at CFLServerApp+0xcc (retail ctor .text:00401440, dtor .text:00401730).
// It owns the server log file, the freelancer.ini / SrvLocal ini paths, the list-server
// host/port, the AppGUID and the SStartupInfo the app hands to IServer::Startup.
// Retail has no RTTI or string naming the class; the name follows the app member
// (CFLServerApp::ServerControl). Field offsets are retail's; the std members are modern
// std types (no retail ABI), so offsets past +0x618 hold only in retail.
#pragma once

#include <string>
#include <vector>
#include <fstream>

#include "common_server_types.h"   // SStartupInfo

// .data:004278c0 -- the exe's packed version, written by CFLServerApp::InitInstance, read by
// SetTitle.
extern unsigned int GServerVersion;

// .text:0040aee0 -- CString::LoadString(Id); on failure the string becomes
// "<Invalid resource id>". Returns whether the load succeeded.
bool LoadStringChecked(CString& Out, UINT Id);

class FServerControl
{
public:
	FServerControl();
	~FServerControl();

	bool SetAppGuid(const GUID* Guid);
	bool SetIniPath(const char* Path);
	bool SetClientModulePath(const char* ModulePath);
	bool SetMaxPlayers(unsigned int MaxPlayers);
	bool OpenLog(const char* FileName);
	bool CloseLog();
	bool WriteLog(const char* Data, int Count);
	bool AddLogCategory(int Category);
	bool SetLogLevel(int Level);
	bool SetMaxConsoleLines(unsigned int Value);
	bool SetConsoleTrimLines(unsigned int Value);
	bool SetUpdateRate(float Rate);
	bool SetTitle(const char* Prefix);
	bool SetListServerHost(const char* HostName);
	bool SetListServerPort(int Port);
	bool SetServerName(const wchar_t* Name);
	bool SetServerDescription(const wchar_t* Text);
	bool SetServerPort(unsigned int Port);              // StartupInfo +0x20c
	void SetBandwidthLogPath(const char* Path);
	void SetAllowNewPlayers(unsigned char Value);       // StartupInfo +0x211
	unsigned char GetAllowNewPlayers() const;
	void SetAllowPvp(unsigned char Value);              // StartupInfo +0x210
	unsigned char GetAllowPvp() const;
	void SetAutoAcceptConfig(bool bValue);

public:
	char IniFileName[0x104];           // +0x000 "freelancer.ini"
	char Field_0x104[0x104];           // +0x104 zeroed by the ctor, never written otherwise. TODO: meaning
	char BandwidthLogPath[0x104];      // +0x208 "FLServerBWLog-%u.txt" from the command line; passed to CMainFrame
	char IniDir[0x100];                // +0x30c _splitpath dir of SetIniPath's argument
	char IniDrive[3];                  // +0x40c _splitpath drive
	char IniPath[0x104];               // +0x40f full freelancer.ini path
	char LocalServerIniPath[0x104];    // +0x513 same directory, "SrvLocal-" + IniFileName
	std::wstring ServerPassword;       // +0x618 empty after the ctor; set by the Server Configuration dialog
	bool bListServerReporting;         // +0x628 NOT touched by the ctor (static object: zero). Set by the
	                                   //        Server Configuration dialog's "Internet Accessible" box; gates
	                                   //        the gun thread and UpdateGunReport
	unsigned int MaxConsoleLines;      // +0x62c 100000; SetMaxConsoleLines accepts 1..100000; ServerLogf trims
	                                   //        the console once its line count exceeds it
	unsigned int ConsoleTrimLines;     // +0x630 200; SetConsoleTrimLines accepts 1..MaxConsoleLines-1; lines
	                                   //        TrimLines deletes
	float UpdatePeriod;                // +0x634 0.05f; SetUpdateRate stores 1/Rate (Rate in 1..60)
	char Title[0x100];                 // +0x638 "<prefix> <version>" formatted by SetTitle
	std::string ListServerHost;        // +0x738 "localhost"; [ListServer] hostname
	int ListServerPort;                // +0x748 0x9988; [ListServer] port
	std::wstring ServerName;           // +0x74c Server Configuration dialog, 0x42-byte buffer
	std::wstring ServerDescription;    // +0x75c Server Configuration dialog, 0x102-byte buffer
	std::ofstream* LogFile;            // +0x76c
	std::vector<int> LogCategories;    // +0x770 starts as { 0x10000 }; command-line pushes; ServerLogf
	                                   //        accepts a non-zero-level line only if Code>>4 is listed
	int LogLevel;                      // +0x780 4; SetLogLevel accepts 1..7
	GUID AppGuid;                      // +0x784 [Freelancer] AppGUID
	bool Field_0x794;                  // +0x794 true. TODO: meaning
	SStartupInfo StartupInfo;          // +0x798 handed to IServer::Startup
	bool bLogOpen;                     // +0x9ac
	bool bUnmaskFpuExceptions;         // +0x9ad false; command line /X; InitInstance and the server thread
	                                   //        unmask zero-divide/overflow/invalid when set
	bool bAutoAcceptConfig;            // +0x9ae false; set by the /C command-line switch; its only reader,
	                                   //        CServerConfigDlg::OnInitDialog, then accepts a loaded
	                                   //        FLServer.cfg through OnOK without waiting for the user
};
