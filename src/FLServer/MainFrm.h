// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CMainFrame — FLServer.exe's main frame window (retail CreateObject .text:00411ea0, ctor
// 00411f40, vtable .rdata:0041dda8, CRuntimeClass .rdata:0041dd0c, message map
// .rdata:0041dda0). Owns the status bar with the uptime pane, a 1 s
// timer that samples RemoteClient's per-client traffic counters and the optional bandwidth CSV
// log (+0x20c).
#pragma once

#include <stdio.h>

class CMainFrame : public CFrameWnd
{
protected:
	CMainFrame();
	DECLARE_DYNCREATE(CMainFrame)

public:
	virtual ~CMainFrame();

	// Clears WS_THICKFRAME from the default style, then the base.
	virtual BOOL LoadFrame(UINT IdResource, DWORD DefaultStyle = WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE,
		CWnd* ParentWnd = NULL, CCreateContext* Context = NULL) override;
	virtual BOOL PreCreateWindow(CREATESTRUCT& Cs) override;

	// (Re)opens the bandwidth CSV log; called by CFLServerApp::OnServerStartupFinished.
	void OpenBandwidthLog(const char* Path);
	// Closes the bandwidth log. No caller in retail.
	void CloseBandwidthLog();

	// CCmdUI handlers with no message-map entry and no caller in retail (strings 0xf028..0xf02c).
	void OnUpdateTraffic(CCmdUI* CmdUI);
	void OnUpdateConnects(CCmdUI* CmdUI);
	void OnUpdateThroughput(CCmdUI* CmdUI);
	void OnUpdatePacketsOut(CCmdUI* CmdUI);
	void OnUpdatePacketsIn(CCmdUI* CmdUI);

protected:
	afx_msg int OnCreate(LPCREATESTRUCT CreateStruct);
	afx_msg void OnUpdateUptime(CCmdUI* CmdUI);
	afx_msg void OnTimer(UINT_PTR IdEvent);
	afx_msg void OnClose();

	DECLARE_MESSAGE_MAP()

public:
	CTime StartTime;                 // +0xc0: CTime::GetCurrentTime() in the ctor
	CStatusBar StatusBar;            // +0xc4
	CToolBar ToolBar;                // +0x144 (never created)
	// Traffic summed over every client at the last 1 s sample (OnTimer).
	unsigned int TotalBytes;         // +0x1e4 sent + received
	unsigned int BytesReceived;      // +0x1e8
	unsigned int BytesSent;          // +0x1ec
	unsigned int PrevTotalBytes;     // +0x1f0 TotalBytes of the previous sample
	unsigned int ThroughputBits;     // +0x1f4 bits per second between the last two samples
	unsigned int PacketsSent;        // +0x1f8
	unsigned int PacketsReceived;    // +0x1fc
	unsigned int PrevSampleTime;     // +0x200 timeGetTime of the previous sample
	unsigned int SampleTime;         // +0x204 timeGetTime of the last sample
	int ClientCount;                 // +0x208 GetNumClients at the last sample
	FILE* BandwidthLog;              // +0x20c
};
