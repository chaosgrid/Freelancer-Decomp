// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CServerConfigDlg — "Server Configuration" (dialog 0x84 in ServerResources.dll): server name,
// description, password, player limit and three options. OnInitDialog loads the last settings
// from FLServer.cfg in the local user directory; OnOK validates them, hands them to the app's
// FServerControl and writes FLServer.cfg back.
// Retail: the only instance is a local of CFLServerApp::InitInstance, built inline there
// (.text:0040bd6c, vtable .rdata:0041d0e0); dtor .text:0040be80 (just CDialog's), scalar
// deleting dtor 0040be60. No message map of its own; DoDataExchange is the ICF-folded empty
// function at .text:00407040. No RTTI or string names the class; the name follows the dialog
// caption.
#pragma once

class CServerConfigDlg : public CDialog
{
public:
	enum { IDD = 0x84 };
	// Control ids of the dialog template.
	enum EControl
	{
		kControlServerName = 1006,
		kControlDescription = 1007,
		kControlPassword = 1008,
		kControlAllowNewPlayers = 1010,
		kControlMaxPlayersSpin = 1011,
		kControlMaxPlayersEdit = 1012,
		kControlInternetAccessible = 1014,
		kControlAllowPvp = 1059,
		kControlLegalNotice = 1070,
	};

	explicit CServerConfigDlg(CWnd* Parent = nullptr);

protected:
	virtual void DoDataExchange(CDataExchange* DX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();

	// Controls fetched with GetDlgItem by OnInitDialog (MFC temporary wrappers, as retail).
	CWnd* MaxPlayersSpin;         // +0x60, control 1011 (msctls_updown32)
	CWnd* ServerNameEdit;         // +0x64, control 1006 (RICHEDIT20W)
	CWnd* DescriptionEdit;        // +0x68, control 1007 (RICHEDIT20W)
	CWnd* PasswordEdit;           // +0x6c, control 1008 (RICHEDIT20W, IME disabled)
	CWnd* MaxPlayersEdit;         // +0x70, control 1012
	CWnd* InternetCheck;          // +0x74, control 1014 "Make Your Server Internet Accessible"
	CWnd* AllowNewPlayersCheck;   // +0x78, control 1010 "Allow New Players"
	CWnd* PvpCheck;               // +0x7c, control 1059 "Allow Players to Harm Other Players"
	CWnd* LegalNotice;            // +0x80, control 1070
};
