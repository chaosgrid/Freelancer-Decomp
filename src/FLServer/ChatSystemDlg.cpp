// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "StdAfx.h"
#include "ChatSystemDlg.h"
#include "ChatRdl.h"
#include "RichEditText.h"
#include "StarSystemList.h"     // GetSystemName
#include "FLServer.h"
#include "server_iserverimpl.h" // Server, g_pSystemChatHook
#include "common_text.h"        // StyleNode, TextNode, BinaryRDLWriter

#include <map>

namespace
{
	// .data:00429e34 — the open system chat windows by system id. Retail's tree compares the
	// keys signed (JL), so the key type is int. Built by the CRT initializer .text:004160c0,
	// destroyed by its atexit thunk 004160f0.
	std::map<int, CChatSystemDlg*> GChatSystemDialogs;

	// .data:00429620 — the encoded outgoing message (0x800 bytes).
	char GChatSystemBuffer[0x800];

	// .data:0042961c — the shared chat-id header's "system" id: word 1 and byte 2 set to 1 by
	// the CRT initializer .text:00416040, byte 3 left at its .data zero. The target of every
	// message this dialog submits; the sender is a zero id built on the stack.
	const CHAT_ID GChatIdSystem(0x00010001u);

	// Style id of the outgoing message's StyleNode (.text:004167e4).
	const unsigned short ChatSystemStyleId = 0xd801;
	const int InputLimitChars = 0x200;
	const int InputTextBytes = 0x800;
	const int TitleChars = 0x400;
}

// .text:004161d0
CChatSystemDlg::CChatSystemDlg(CWnd* Parent)
	: CDialog(IDD, Parent)
	, SystemId(0xffffffffu)
{
}

// .text:00416290 — while SystemId is set, erases its map entry.
CChatSystemDlg::~CChatSystemDlg()
{
	if (SystemId != 0xffffffffu)
	{
		GChatSystemDialogs.erase((int)SystemId);
	}
}

// .text:00416420
void CChatSystemDlg::DoDataExchange(CDataExchange* DX)
{
	DDX_Control(DX, kControlInput, InputEdit);
	DDX_Control(DX, kControlLog, LogEdit);
	DDX_Control(DX, kControlTitle, TitleEdit);
}

// Message map .rdata:0041e710.
BEGIN_MESSAGE_MAP(CChatSystemDlg, CDialog)
	ON_NOTIFY(EN_MSGFILTER, kControlInput, &CChatSystemDlg::OnMsgFilterInput) // 00416760
	ON_BN_CLICKED(kControlClose, &CChatSystemDlg::OnCloseButton)             // 004169e0
END_MESSAGE_MAP()

// .text:00416620
CChatSystemDlg* CChatSystemDlg::FindBySystem(unsigned int SystemId)
{
	std::map<int, CChatSystemDlg*>::iterator It = GChatSystemDialogs.find((int)SystemId);
	if (It == GChatSystemDialogs.end())
	{
		return nullptr;
	}
	return It->second;
}

// .text:00416480
BOOL CChatSystemDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	::SendMessageA(InputEdit.m_hWnd, EM_SETEVENTMASK, 0, ENM_KEYEVENTS);
	::SendMessageA(InputEdit.m_hWnd, EM_EXLIMITTEXT, 0, InputLimitChars);
	GChatSystemDialogs[(int)SystemId] = this;
	wchar_t Title[TitleChars];
	GetSystemName(SystemId, Title, TitleChars);
	SetRichEditText(TitleEdit.m_hWnd, Title);
	LockServer();
	g_pSystemChatHook = &CChatSystemDlg::OnSystemChat;
	UnlockServer();
	return TRUE;
}

// .text:00416750 — empty: Enter in the input box must not close the window.
void CChatSystemDlg::OnOK()
{
}

// .text:00416730 — modeless and self-owned.
void CChatSystemDlg::OnCancel()
{
	DestroyWindow();
	delete this;
}

// .text:004169e0 — calls OnCancel through the vtable.
void CChatSystemDlg::OnCloseButton()
{
	OnCancel();
}

// .text:00416760 — as CChatAccountDlg::OnMsgFilterInput, with StyleNode(0xd801), sender 0,
// target the "system" id and this window's system id.
void CChatSystemDlg::OnMsgFilterInput(NMHDR* Header, LRESULT* Result)
{
	const MSGFILTER* Filter = (const MSGFILTER*)Header;
	if (Filter->wParam != VK_RETURN)
	{
		*Result = 0;
		return;
	}

	if (::SendMessageA(InputEdit.m_hWnd, WM_GETTEXTLENGTH, 0, 0) != 0)
	{
		wchar_t Text[InputTextBytes / sizeof(wchar_t)];
		GetRichEditText(InputEdit.m_hWnd, Text, InputTextBytes);
		LockServer();
		{
			RenderDisplayList Rdl;
			// Double clone as in CChatAccountDlg::OnMsgFilterInput.
			{
				StyleNode Style(ChatSystemStyleId);
				{
					const FChatRdlNodeHolder FirstClone(Style.Clone());
					AppendChatRdlNode(Rdl, FirstClone);
				}
			}
			{
				TextNode Line(Text, -1);
				{
					const FChatRdlNodeHolder FirstClone(Line.Clone());
					AppendChatRdlNode(Rdl, FirstClone);
				}
			}

			BinaryRDLWriter Writer;
			unsigned int Size;
			if (Writer.write_buffer(Rdl, GChatSystemBuffer, sizeof(GChatSystemBuffer), Size))
			{
				const CHAT_ID Sender(0u);
				Server.SubmitChat(Sender, Size, GChatSystemBuffer, GChatIdSystem, (int)SystemId);
			}
			// Retail releases the server lock before the list is destroyed.
			UnlockServer();
		}
	}

	InputEdit.SetSel(0, -1);
	::SendMessageA(InputEdit.m_hWnd, WM_CLEAR, 0, 0);
	*Result = 1;
}

// .text:00416670 — the window is looked up before the server lock is taken; with no window
// the hook returns without locking.
void __cdecl CChatSystemDlg::OnSystemChat(wchar_t* Text, int SystemId)
{
	std::map<int, CChatSystemDlg*>::iterator It = GChatSystemDialogs.find(SystemId);
	if (It == GChatSystemDialogs.end() || It->second == nullptr)
	{
		return;
	}
	CChatSystemDlg* Dlg = It->second;
	LockServer();
	const int LinesBefore = (int)::SendMessageA(Dlg->LogEdit.m_hWnd, EM_GETLINECOUNT, 0, 0);
	AppendRichEditText(Dlg->LogEdit.m_hWnd, Text);
	AppendRichEditText(Dlg->LogEdit.m_hWnd, L"\n");
	const int LinesAfter = (int)::SendMessageA(Dlg->LogEdit.m_hWnd, EM_GETLINECOUNT, 0, 0);
	if (::GetFocus() != Dlg->LogEdit.m_hWnd)
	{
		Dlg->LogEdit.LineScroll(LinesAfter - LinesBefore, 0);
	}
	UnlockServer();
}
