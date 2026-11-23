// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CFLServerApp — FLServer.exe's CWinApp (retail ctor .text:0040b370, dtor 0040b420,
// vtable .rdata:0041d038, message map .rdata:0041cff8, the global object theApp at
// .data:00426c58). Retail keeps a second pointer to it in .data:004278b8 (GApp below), set by
// the constructor and cleared by the destructor; code all over the exe reads the app through it.
#pragma once

#include <string>

#include "ServerControl.h"
#include "ShutdownDlg.h"

class CGunWrapper;
class CFLServerDoc;
class CFLSummaryForm;

class CFLServerApp : public CWinApp
{
public:
	CFLServerApp();
	virtual ~CFLServerApp();

	// Overrides (retail vtable slots: InitInstance +0x58 -> 0040b840, OnIdle +0x68 -> 0040d670,
	// ExitInstance +0x70 -> 0040b6f0).
	virtual BOOL InitInstance();
	virtual BOOL OnIdle(LONG Count);
	virtual int ExitInstance();

	// Stops the server and gun threads, starts the Server.dll shutdown thread and runs the
	// shutdown dialog until it has finished.
	bool BeginShutdown();
	// True once the shutdown thread has finished (or none is running).
	bool PollShutdownThread();
	// True once the startup thread has finished (or none is running); on completion runs
	// OnServerStartupFinished and returns its result.
	bool PollStartupThread();
	// .text:0040bf10
	bool IsShuttingDown() const { return bShuttingDown; }
	// While the startup thread runs, closing the frame waits for it in the shutdown dialog.
	void WaitForStartupThread();
	// .text:0040bf00
	bool IsStartupPending() const { return bStartupPending; }

	// .text:0040d650
	CView* GetMainView() const { return MainView; }
	// .text:0040d660
	CFLSummaryForm* GetSummaryForm() const { return SummaryForm; }

	// The window/document title kept by ServerControl (app +0x704, set from "FLServer -" by
	// InitInstance through .text:00401fc0); CFLServerDoc::SetTitle (0040da80) always uses it.
	LPCTSTR GetServerTitle() const;

	// Toggles the frame between the console view and the summary form; returns the view that
	// was active before.
	CView* SwitchView();

protected:
	bool OnServerStartupFinished(bool bStarted);
	// Reads [Freelancer] / [ListServer] / [Server] / [Resources] of an ini file; returns true
	// if a "Data path" was found and opened.
	bool ReadFreelancerIni(const char* Path);
public:
	// Refreshes the gun (matchmaking) report; called by the gun thread.
	bool UpdateGunReport();

protected:
	// Message handlers (message map .rdata:0041cdb8).
	afx_msg void OnAppAbout();
	afx_msg void OnDebugDebug();
	afx_msg void OnDebugAiDebugger();
	afx_msg void OnUpdateDisabled(CCmdUI* CmdUI);
	afx_msg void OnUpdateNeedsServer(CCmdUI* CmdUI);
	afx_msg void OnEditAccounts();
	afx_msg void OnChatPlayer();
	afx_msg void OnChatStarSystem();
	afx_msg void OnChatUniverse();
	afx_msg void OnServerNews();
	afx_msg void OnDebugCrashMe();
	afx_msg void OnUpdateViewConsole(CCmdUI* CmdUI);
	afx_msg void OnUpdateViewStatusSummary(CCmdUI* CmdUI);
	afx_msg void OnSwitchView();

	DECLARE_MESSAGE_MAP()

public:
	// +0xc4 is CWinApp's last field in MFC42; +0xc8 is the first CFLServerApp field.
	CGunWrapper* GunWrapper = nullptr;      // +0xc8: DALib gun (matchmaking) reporter
	FServerControl ServerControl;           // +0xcc (ctor 00401440, dtor 00401730)

	// Strings owned by the app, freed by the destructor. Their only writer in retail,
	// .text:0040c8e0 (add a name unless already present, at most 64), is unreferenced in the
	// image, so the array stays empty; not ported.
	std::string* OwnedStrings[64] = {};     // +0xa7c
	int OwnedStringCount = 0;               // +0xb7c

	RECT ConsoleFrameRect = {};             // +0xb80: frame rect saved when leaving the console
	CView* MainView = nullptr;              // +0xb90: main frame's active view after startup
	CFLSummaryForm* SummaryForm = nullptr;  // +0xb94
	CDocument* MainDocument = nullptr;      // +0xb98: main frame's active document
	HMODULE RichEditModule = nullptr;       // +0xb9c: RICHED20.DLL
	CWinThread* StartupThread = nullptr;    // +0xba0: runs IServerImpl::Startup (0040be90)
	CWinThread* ShutdownThread = nullptr;   // +0xba4: runs IServerImpl::Shutdown (0040b710)
	bool bStartupPending = false;           // +0xba8
	bool bShuttingDown = false;             // +0xba9
	CShutdownDlg ShutdownDlg;               // +0xbac
};

extern CFLServerApp theApp;
// .data:004278b8 — set by the constructor, cleared by the destructor.
extern CFLServerApp* GApp;

extern HANDLE GAppMutex;        // .data:00427868
extern HANDLE GServerMutex;     // .data:0042789c
extern int GServerRunning;      // .data:004278b4
extern HANDLE GServerThread;    // .data:004278ac
extern DWORD GServerThreadId;   // .data:004278b0
extern HANDLE GGunThread;       // .data:004278bc

void LockApp();                 // .text:0040afa0
void UnlockApp();               // .text:0040afb0
void LockServer();              // .text:0040cf90
void UnlockServer();            // .text:0040d040
