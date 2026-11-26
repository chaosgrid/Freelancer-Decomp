// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CCreateAccountDlg — "Create Account" (dialog 0x8a in ServerResources.dll): creates an account
// with a password through Players.CreateAccount / SetAccountPassword.
// Retail: ctor .text:00406fb0, dtor 00407090, scalar deleting dtor 00407070, vtable
// .rdata:0041c748, empty message map .rdata:0041c740 (entries 0041c728).
// Retail never constructs it (nothing in the image references the ctor or the vtable): compiled
// in but unused. Ported for structure; nothing creates it here either.
// No RTTI or string names the class; the name follows the dialog caption.
#pragma once

class CAccount;

class CCreateAccountDlg : public CDialog
{
public:
	enum { IDD = 0x8a };
	// Control ids of the dialog template.
	enum EControl
	{
		kControlName = 1037,
		kControlPassword = 1038,
	};

	explicit CCreateAccountDlg(CWnd* Parent = nullptr);

	CRichEditCtrl NameEdit;       // +0x60, control 1037
	CRichEditCtrl PasswordEdit;   // +0xa0, control 1038
	CAccount* CreatedAccount;     // +0xe0, the new account after a successful OnOK

protected:
	virtual void DoDataExchange(CDataExchange* DX);
	virtual void OnOK();

	DECLARE_MESSAGE_MAP()
};
