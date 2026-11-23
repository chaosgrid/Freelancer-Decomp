// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CFLServerDoc — FLServer.exe's document (retail CreateObject .text:0040d800, ctor 0040d900,
// dtor 0040d990, vtable .rdata:0041d340, CRuntimeClass .rdata:0041d2bc, message map
// .rdata:0041d338). A CRichEditDoc that also carries the server load figures and the last
// ServerStats snapshot; the console view, the summary form and the main frame read them.
#pragma once

#include "common_server_types.h"

class CFLServerDoc : public CRichEditDoc
{
protected:
	CFLServerDoc();
	DECLARE_DYNCREATE(CFLServerDoc)

public:
	enum { LoadHistorySize = 64 };

	// The last value given to SetLoad.
	int GetLoad();
	// Mean of the LoadHistorySize history slots; recomputed only when a SetLoad happened since
	// the last call.
	int GetAverageLoad();
	// Records a load sample; fails (returns false) without blocking when the mutex is held
	// elsewhere.
	bool SetLoad(int Load);
	// Stores a ServerStats snapshot; fails without blocking like SetLoad.
	bool SetStats(const ServerStats* Stats);
	// Copies the snapshot out (blocks on the mutex).
	void GetStats(ServerStats* Stats);

	virtual CRichEditCntrItem* CreateClientItem(REOBJECT* Preo) const;
	virtual BOOL OnNewDocument();
	virtual void Serialize(CArchive& Ar);
	virtual void SetTitle(LPCTSTR Title);
	virtual BOOL SaveModified();
	virtual COleClientItem* GetPrimarySelectedItem(CView* View);

	virtual ~CFLServerDoc();

protected:
	DECLARE_MESSAGE_MAP()

	HANDLE Mutex;                        // +0xf4
	int Load;                            // +0xf8
	bool bAverageDirty;                  // +0xfc
	int LoadHistory[LoadHistorySize];    // +0x100
	int LoadHistoryIndex;                // +0x200
	int AverageLoad;                     // +0x204
	ServerStats Stats;                   // +0x208
};
