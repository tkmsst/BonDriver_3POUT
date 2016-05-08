#include "BonDriver_3POUT.h"

namespace BonDriver_3POUT {

static stChannel g_stChannels[3][MAX_CH];
static char g_Device[32] = "";

static int Convert(char *src, char *dst, size_t dstsize)
{
	iconv_t d = ::iconv_open("UTF-16LE", "UTF-8");
	if (d == (iconv_t)-1)
		return -1;
	size_t srclen = ::strlen(src) + 1;
	size_t dstlen = dstsize - 2;
	size_t ret = ::iconv(d, &src, &srclen, &dst, &dstlen);
	*dst = *(dst + 1) = '\0';
	::iconv_close(d);
	if (ret == (size_t)-1)
		return -2;
	return 0;
}

static BOOL IsTagMatch(const char *line, const char *tag, char **value)
{
	const int taglen = ::strlen(tag);
	const char *p;

	if (::strncmp(line, tag, taglen) != 0)
		return FALSE;
	p = line + taglen;
	while (*p == ' ' || *p == '\t')
		p++;
	if (value == NULL && *p == '\0')
		return TRUE;
	if (*p++ != '=')
		return FALSE;
	while (*p == ' ' || *p == '\t')
		p++;
	*value = const_cast<char *>(p);
	return TRUE;
}

static int Init()
{
	FILE *fp;
	char *p, buf[512];

	Dl_info info;
	if (::dladdr((void *)Init, &info) == 0)
		return -1;
	::strncpy(buf, info.dli_fname, sizeof(buf) - 8);
	buf[sizeof(buf) - 8] = '\0';
	::strcat(buf, ".conf");

	fp = ::fopen(buf, "r");
	if (fp == NULL)
		return -2;
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < MAX_CH; j++)
			g_stChannels[i][j].bUnused = TRUE;
	}

	int idx = 0;
	BOOL bdFlag = FALSE;
	while (::fgets(buf, sizeof(buf), fp))
	{
		if (buf[0] == ';')
			continue;
		p = buf + ::strlen(buf) - 1;
		while ((p >= buf) && (*p == '\r' || *p == '\n'))
			*p-- = '\0';
		if (p < buf)
			continue;
		if ((idx != 0) && IsTagMatch(buf, "#ISDB_T", NULL))
			idx = 0;
		else if ((idx != 1) && IsTagMatch(buf, "#ISDB_BS", NULL))
			idx = 1;
		else if ((idx != 2) && IsTagMatch(buf, "#ISDB_CS", NULL))
			idx = 2;
		else if (!bdFlag && IsTagMatch(buf, "#DEVICE", &p))
		{
			::strncpy(g_Device, p, sizeof(g_Device) - 1);
			g_Device[sizeof(g_Device) - 1] = '\0';
			bdFlag = TRUE;
		}
		else
		{
			int n = 0;
			char *cp[5];
			BOOL bOk = FALSE;
			p = cp[n++] = buf;
			while (1)
			{
				p = ::strchr(p, '\t');
				if (p)
				{
					*p++ = '\0';
					cp[n++] = p;
					if (n > 3)
					{
						bOk = TRUE;
						break;
					}
				}
				else
					break;
			}
			if (bOk)
			{
				DWORD dw = ::atoi(cp[1]);
				if (dw < MAX_CH)
				{
					if (Convert(cp[0], g_stChannels[idx][dw].strChName, MAX_CN_LEN) < 0)
					{
						::fclose(fp);
						return -3;
					}
					g_stChannels[idx][dw].freq.frequencyno = ::atoi(cp[2]);
					g_stChannels[idx][dw].freq.tsid = ::strtoul(cp[3], NULL, 16);
					g_stChannels[idx][dw].bUnused = FALSE;
				}
			}
		}
	}
	::fclose(fp);
	return 0;
}

static int check_usbdevId(const unsigned int* desc)
{
	//if(0xeb1a == desc[0] && 0x8178 == desc[1])  return 0;
	if(0x0511 == desc[0]) {
		switch( desc[1] ) {
		//case 0x0029:
		//case 0x003b:
		case 0x0045:
			return 0;
		}
	}
	return 1;   //# not match
}

cBonDriver *cBonDriver::m_spThis = NULL;
cCriticalSection cBonDriver::m_sInstanceLock;
BOOL cBonDriver::m_sbInit = TRUE;

extern "C" IBonDriver *CreateBonDriver()
{
	LOCK(cBonDriver::m_sInstanceLock);
	if (cBonDriver::m_sbInit)
	{
		if (Init() < 0)
			return NULL;
		cBonDriver::m_sbInit = FALSE;
	}

	// 複数読み込み禁止
	cBonDriver *p3POUT = NULL;
	if (cBonDriver::m_spThis == NULL)
		p3POUT = new cBonDriver();
	return p3POUT;
}

cBonDriver::cBonDriver()
{
	m_spThis = this;
	Convert((char *)TUNER_NAME, m_TunerName, sizeof(m_TunerName));

	m_dwCurSpace = 0x7fffffff;	// INT_MAX
	m_dwCurChannel = 0x7fffffff;
	usbep.fd = -1;
	pDev = NULL;
	demodDev = NULL;
	tunerDev[0] = NULL;
	tunerDev[1] = NULL;
	m_selectedTuner = -1;
	tsthr = NULL;
}

cBonDriver::~cBonDriver()
{
	CloseTuner();
	m_spThis = NULL;
}

const BOOL cBonDriver::OpenTuner(void)
{
	//# if already open, close tuner
	CloseTuner();
	if(IsTunerOpening()) return FALSE;

	usbep.fd = usbdevfile_alloc(check_usbdevId, g_Device);
	if(0 > usbep.fd) {
		return FALSE;
	}
	if(em287x_create(&pDev, &usbep) != 0) {
		goto err;
	}
	//# demod
	if(tc90522_create(&demodDev) != 0) {
		goto err;
	}
	struct i2c_device_st* pI2C;
	pI2C = (i2c_device_st*)tc90522_i2c_ptr(demodDev);
	pI2C->addr = 0x20;
	em287x_attach(pDev, pI2C);
	if(tc90522_init(demodDev) != 0) {
		goto err;
	}
	//# tuner 0 terra
	if(mxl136_create(&tunerDev[0]) != 0) {
		goto err;
	}
	pI2C = (i2c_device_st*)mxl136_i2c_ptr(tunerDev[0]);
	pI2C->addr = 0xc0;
	tc90522_attach(demodDev, 0, pI2C);
	if(mxl136_init(tunerDev[0]) != 0) {
		goto err;
	}
	//# tuner 1 BS/CS
	if(tda20142_create(&tunerDev[1]) != 0) {
		goto err;
	}
	pI2C = (i2c_device_st*)tda20142_i2c_ptr(tunerDev[1]);
	pI2C->addr = 0xa8;
	tc90522_attach(demodDev, 1, pI2C);
	if(tda20142_init(tunerDev[1]) != 0) {
		goto err;
	}
	//# demod set params
	if( tc90522_selectDevice(demodDev, 0) ) {
		goto err;
	}
	//# TS receive thread
	if( tsthread_create(&tsthr, &usbep) ) {
		goto err;
	}

	return TRUE;

err:
	CloseTuner();
	return FALSE;
}

void cBonDriver::CloseTuner(void)
{
	if(tsthr) {
		tsthread_stop(tsthr);
		tsthread_destroy(tsthr);
		tsthr = NULL;
	}
	if(tunerDev[0]) {
		mxl136_destroy(tunerDev[0]);
		tunerDev[0] = NULL;
	}
	if(tunerDev[1]) {
		tda20142_destroy(tunerDev[1]);
		tunerDev[1] = NULL;
	}
	if(demodDev) {
		tc90522_destroy(demodDev);
		demodDev = NULL;
	}
	if(pDev) {
		em287x_destroy(pDev);
		pDev = NULL;
	}
	if(0 <= usbep.fd) {
		close(usbep.fd);
		usbep.fd = -1;
	}
}

const BOOL cBonDriver::SetChannel(const BYTE bCh)
{
	return TRUE;
}

const float cBonDriver::GetSignalLevel(void)
{
	if(0 > m_selectedTuner || !demodDev ) return -3.1f;
	unsigned statData[2];
	if(tc90522_readStatistic(demodDev, m_selectedTuner, statData)) return -3.2f;
	return statData[1] * 0.01f;
}

const DWORD cBonDriver::WaitTsStream(const DWORD dwTimeOut)
{
	const int remainTime = (dwTimeOut < 0x10000000) ? dwTimeOut : 0x10000000;
	if(! tsthr) return WAIT_ABANDONED;

	const int r = tsthread_wait(tsthr, remainTime);
	if(0 > r) return WAIT_ABANDONED;
	else if(0 < r) return WAIT_OBJECT_0;
	else return WAIT_TIMEOUT;
}

const DWORD cBonDriver::GetReadyCount(void)
{//# number of call GetTsStream()
	if(! tsthr) return 0;
	const int ret = tsthread_readable(tsthr);
	return (ret > 0) ? 1 : 0;
}

const BOOL cBonDriver::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	BYTE *pSrc = NULL;
	if(GetTsStream(&pSrc, pdwSize, pdwRemain)){
		if(*pdwSize) ::memcpy(pDst, pSrc, *pdwSize);
		return TRUE;
	}
	return FALSE;
}

const BOOL cBonDriver::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	if(! tsthr) return FALSE;
	*pdwSize = tsthread_read(tsthr, (void**)ppDst);
	*pdwRemain = GetReadyCount();
	return TRUE;
}

void cBonDriver::PurgeTsStream(void)
{
	if(! tsthr) return;
	//# purge available data in TS buffer
	tsthread_read(tsthr, NULL);
}

void cBonDriver::Release(void)
{
	LOCK(m_sInstanceLock);
	delete this;
}

LPCTSTR cBonDriver::GetTunerName(void)
{
	return (LPCTSTR)m_TunerName;
}

const BOOL cBonDriver::IsTunerOpening(void)
{
	return (0 < usbep.fd) ? TRUE : FALSE;
}

LPCTSTR cBonDriver::EnumTuningSpace(const DWORD dwSpace)
{
	static char strSpace[32];

	if(0 == dwSpace) Convert((char *)"地デジ", strSpace, sizeof(strSpace));
	else if(1 == dwSpace) Convert((char *)"BS", strSpace, sizeof(strSpace));
	else if(2 == dwSpace) Convert((char *)"CS", strSpace, sizeof(strSpace));
	else return NULL;
	return (LPCTSTR)strSpace;
}

LPCTSTR cBonDriver::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	if (dwSpace > 2)
		return NULL;
	if (dwChannel >= MAX_CH)
		return NULL;
	if (g_stChannels[dwSpace][dwChannel].bUnused)
		return NULL;
	return (LPCTSTR)(g_stChannels[dwSpace][dwChannel].strChName);
}

const BOOL cBonDriver::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	DWORD dwFreqNo = 0;
	DWORD dwFreq = 0;
	int tunerNum = 0;

	if( m_dwCurSpace == dwSpace && m_dwCurChannel == dwChannel ) {
		return TRUE;
	}

	if( 3 > dwSpace && MAX_CH > dwChannel ) {
		if( g_stChannels[dwSpace][dwChannel].bUnused )
			return FALSE;
		dwFreqNo = g_stChannels[dwSpace][dwChannel].freq.frequencyno;

		if( 0 == dwSpace && dwFreqNo < 113 ) {	// Terra
			dwFreq = dwFreqNo * 6000;
			if (dwFreqNo < 12) dwFreq += 93143;
			else if (dwFreqNo < 17) dwFreq += 95143;
			else if (dwFreqNo < 23) dwFreq += 93143;
			else if (dwFreqNo < 27) dwFreq += 95143;
			else if (dwFreqNo < 63) dwFreq += 93143;
			else dwFreq += 95143;
		}else if( dwFreqNo < 12 ) {				//BS
			dwFreq = dwFreqNo * 38360 + 1049480;
		}else if( dwFreqNo < 24 ) {				// CS
			dwFreq = dwFreqNo * 40000 + 1133000;
		}else{
			return FALSE;
		}
	}else if(60000 <= dwChannel && dwChannel < 2456123) {	// kHz
		dwFreq = dwChannel;
	}else{
		return FALSE;
	}
	if( dwFreq >= 900000 ) tunerNum = 1;

	if(tunerNum != m_selectedTuner) {
		if( tc90522_selectDevice(demodDev, tunerNum) ) return FALSE;
		if(tunerNum & 0x1) {
			mxl136_sleep(tunerDev[0]);
		}else{
			mxl136_wakeup(tunerDev[0]);
		}
	}
	if(tunerNum & 0x1) {
		if( tda20142_setFreq(tunerDev[1], dwFreq) ) return FALSE;
		usleep(30000);
		if( tc90522_resetDemod(demodDev, tunerNum ) ) return FALSE;
		usleep(50000);
		if(1471440 >= dwFreq) { // BS only
			for( int i = 20; i > 0; i-- ) {
				int ret = tc90522_selectStream(demodDev, tunerNum, g_stChannels[dwSpace][dwChannel].freq.tsid );
				if(0 == ret) {
					break;
				}else if(0 > ret)
					return FALSE;
				usleep(40000);
			}
		}
	}else{
		if( mxl136_setFreq(tunerDev[0], dwFreq) ) return FALSE;
		usleep(30000);
		if( tc90522_resetDemod(demodDev, tunerNum ) ) return FALSE;
		usleep(40000);
	}
	//# set variables
	m_dwCurSpace = dwSpace;
	m_dwCurChannel = dwChannel;
	m_selectedTuner = tunerNum;

	for( int i = 20; i > 0; i-- ) {
		unsigned statData[4];
		usleep(40000);
		if( tc90522_readStatistic(demodDev, tunerNum, statData) ) continue;
		if( statData[0] & 0x10 ) break;
	}

	PurgeTsStream();

	return TRUE;
}

const DWORD cBonDriver::GetCurSpace(void)
{
	return m_dwCurSpace;
}

const DWORD cBonDriver::GetCurChannel(void)
{
	return m_dwCurChannel;
}

}
