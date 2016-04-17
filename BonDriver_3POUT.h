#ifndef _BONDRIVER_H_
#define _BONDRIVER_H_
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>
#include <dlfcn.h>
#include <pthread.h>

extern "C" {
#include "usbdevfile.h"
#include "em287x.h"
#include "tc90522.h"
#include "tda20142.h"
#include "mxl136.h"
#include "tsthread.h"
}
#include "verstr.h"
#include "message.h"

#include "typedef.h"
#include "IBonDriver2.h"

namespace BonDriver_3POUT {

#define MAX_CH				128
#define MAX_CN_LEN			64
#define TUNER_NAME			"US-3POUT"

////////////////////////////////////////////////////////////////////////////////

#include "Common.h"

////////////////////////////////////////////////////////////////////////////////

struct stFrequency {
	int frequencyno;
	unsigned int tsid;
};

struct stChannel {
	char strChName[MAX_CN_LEN];
	stFrequency freq;
	BOOL bUnused;
};

class cBonDriver : public IBonDriver2 {
	char m_TunerName[64];

public:
	static cCriticalSection m_sInstanceLock;
	static cBonDriver *m_spThis;
	static BOOL m_sbInit;

	cBonDriver();
	virtual ~cBonDriver();

	// IBonDriver
	const BOOL OpenTuner(void);
	void CloseTuner(void);
	const BOOL SetChannel(const BYTE bCh);
	const float GetSignalLevel(void);
	const DWORD WaitTsStream(const DWORD dwTimeOut = 0);
	const DWORD GetReadyCount(void);
	const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain);
	const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain);
	void PurgeTsStream(void);
	void Release(void);

	// IBonDriver2
	LPCTSTR GetTunerName(void);
	const BOOL IsTunerOpening(void);
	LPCTSTR EnumTuningSpace(const DWORD dwSpace);
	LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel);
	const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel);
	const DWORD GetCurSpace(void);
	const DWORD GetCurChannel(void);

protected:
	DWORD m_dwCurSpace;
	DWORD m_dwCurChannel;

	struct usb_endpoint_st usbep;
	em287x_state pDev;
	void *demodDev;
	void *tunerDev[2];
	int m_selectedTuner;
	tsthread_ptr tsthr;
};

}
#endif	// _BONDRIVER_H_
