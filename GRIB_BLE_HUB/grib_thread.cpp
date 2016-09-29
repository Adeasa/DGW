/* ********** ********** ********** ********** ********** ********** ********** ********** ********** **********
shbaek: Include File
********** ********** ********** ********** ********** ********** ********** ********** ********** ********** */
#include "include/grib_thread.h"

/* ********** ********** ********** ********** ********** ********** ********** ********** ********** **********
shbaek: Global Variable
********** ********** ********** ********** ********** ********** ********** ********** ********** ********** */
int gDebugThread;
char* gHubID;

Grib_DbAll* 	  	gDbAll;
Grib_HubThreadInfo*		gHubThread;
Grib_HubThreadInfo*		gResetThread;
Grib_DeviceThreadInfo** gDevThreadList;

#define TEST_SKIP_USE_ONEM2M		OFF

/* ********** ********** ********** ********** ********** ********** ********** ********** ********** **********
shbaek: Function
********** ********** ********** ********** ********** ********** ********** ********** ********** ********** */
int Grib_SetThreadConfig(void)
{
	int iRes = GRIB_ERROR;
	Grib_ConfigInfo pConfigInfo;

	MEMSET(&pConfigInfo, 0x00, sizeof(Grib_ConfigInfo));

	iRes = Grib_LoadConfig(&pConfigInfo);
	if(iRes != GRIB_DONE)
	{
		GRIB_LOGD("LOAD CONFIG ERROR !!!\n");
		return iRes;
	}

	gDebugThread = pConfigInfo.debugThread;
	if(gHubID != NULL)
	{
		FREE(gHubID);
		gHubID = NULL;
	}

	gHubID = STRDUP(pConfigInfo.hubID);
	GRIB_LOGD("# SET THREAD DEBUG: %s\n", GRIB_BOOL_TO_STR(gDebugThread));

	return GRIB_DONE;
}

int Grib_DeviceCheckAttr(Grib_DbRowDeviceInfo* pRowDeviceInfo)
{
	int i = 0;
	int iDBG = gDebugThread;
	int iAttr = 0;
	Grib_DbRowDeviceFunc* pRowDeviceFunc;

	for(i=0; i<pRowDeviceInfo->deviceFuncCount; i++)
	{
		pRowDeviceFunc = pRowDeviceInfo->ppRowDeviceFunc[i];

		iAttr |= pRowDeviceFunc->funcAttr;
		if(iDBG)GRIB_LOGD("# %s-CHECK: %s [%s][%d]\n", 
			pRowDeviceInfo->deviceID, pRowDeviceFunc->funcName, Grib_FuncAttrToStr(pRowDeviceFunc->funcAttr), pRowDeviceFunc->funcAttr);
	}

	if(iDBG)GRIB_LOGD("# %s-CHECK: HAVE [%s][%d]\n", 
		pRowDeviceInfo->deviceID, Grib_FuncAttrToStr(iAttr), iAttr);
	
	return iAttr;
}

void *Grib_ContorolThread(void* threadArg)
{
	int i = 0;
	int iRes = GRIB_ERROR;
	int iDBG = gDebugThread;
	int iTry = 0;

	OneM2M_ReqParam reqParam;
	OneM2M_ResParam resParam;

	Grib_DeviceThreadInfo *pThreadInfo;
	Grib_DbRowDeviceInfo* pDeviceInfo;
	Grib_DbRowDeviceFunc** ppFuncInfo;

	char* deviceID = NULL;
	char* cmdConValue = NULL;
	char* cmdFuncName = NULL;

	char bleSendBuff[BLE_MAX_SIZE_SEND_MSG];
	char bleRecvBuff[BLE_MAX_SIZE_RECV_MSG];

	int iTimeCheck = FALSE;
	time_t sysTimer;
	struct tm *sysTime;

	if(threadArg == NULL)
	{
		GRIB_LOGD("0x%x THREAD INFO IS NULL", pthread_self());
		return NULL;
	}

	//shbaek: Pre-Pare
	pThreadInfo	= (Grib_DeviceThreadInfo *)threadArg;
	pDeviceInfo	= pThreadInfo->pRowDeviceInfo;
	ppFuncInfo	= pDeviceInfo->ppRowDeviceFunc;
	deviceID	= pDeviceInfo->deviceID;

	pThreadInfo->controlThreadStatus = THREAD_STATUS_NONE;

	MEMSET(&reqParam, GRIB_INIT, sizeof(OneM2M_ReqParam));
	MEMSET(&reqParam, GRIB_INIT, sizeof(OneM2M_ResParam));

	STRINIT(reqParam.xM2M_Origin, sizeof(reqParam.xM2M_Origin));
	STRNCPY(reqParam.xM2M_Origin, deviceID, STRLEN(deviceID));

	while(TRUE)
	{
/*
		pthread_mutex_lock(&pThreadInfo->threadMutex);
		pThreadInfo->controlThreadStatus = THREAD_STATUS_POLLING;
		pthread_mutex_unlock(&pThreadInfo->threadMutex);
*/
		GRIB_LOGD("# %s-CTR>: POLLING START\n", deviceID);

		iRes = Grib_LongPolling(&reqParam, &resParam);
		if(iRes != GRIB_DONE)
		{
			if(resParam.http_ResNum == HTTP_STATUS_CODE_REQUEST_TIME_OUT)
			{
				GRIB_LOGD("# %s-CTR>: POLLING TIME OUT\n", deviceID);
			}
			else
			{
				GRIB_LOGD("# %s-CTR>: POLLING ERROR MSG: %s[%d]\n", deviceID, resParam.http_ResMsg, resParam.http_ResNum);
				SLEEP(1);
			}

			//SLEEP(1);//2 shbaek: DELAY FOR TEST
			continue;
		}

		GRIB_LOGD("# %s-CTR>: COMMAND START\n", deviceID);

		cmdFuncName = cmdConValue = NULL;
		for(i=0; i<pDeviceInfo->deviceFuncCount; i++)
		{
			if(STRNCMP(ppFuncInfo[i]->exRsrcID, resParam.xM2M_PrntID, STRLEN(resParam.xM2M_PrntID))==0)
			{//shbaek: Matching Function's Execute Resource ID
				cmdFuncName = ppFuncInfo[i]->funcName;
			}
		}

		if(cmdFuncName == NULL)
		{
			GRIB_LOGD("# %s-CTR>: IN-VALID Ex RESOURCE ID: %s\n", deviceID, resParam.xM2M_PrntID);
			continue;
		}

		cmdConValue = resParam.xM2M_Content;
		if(iDBG)
		{
			GRIB_LOGD("# %s-CTR>: COMMAND PID : %s\n", deviceID, resParam.xM2M_PrntID);
			GRIB_LOGD("# %s-CTR>: COMMAND RID : %s\n", deviceID, resParam.xM2M_RsrcID);
			GRIB_LOGD("# %s-CTR>: COMMAND CON : %s\n", deviceID, resParam.xM2M_Content);
		}

		while(TRUE)
		{
			int checkStatus = THREAD_STATUS_NONE;

			pthread_mutex_lock(&pThreadInfo->threadMutex);
			checkStatus = pThreadInfo->reportThreadStatus;
			pthread_mutex_unlock(&pThreadInfo->threadMutex);

			if(iDBG)GRIB_LOGD("# %s-CTR>: BLE CHECK STATUS: %s[%d]\n", deviceID, Grib_ThreadStatusToStr(checkStatus), checkStatus);

			if(checkStatus == THREAD_STATUS_USE_BLE)
			{//shbaek: Wait for Report Thread.
				SLEEP(1);
				continue;
			}
			else
			{//shbaek: It's My Turn.
				pthread_mutex_lock(&pThreadInfo->threadMutex);
				pThreadInfo->controlThreadStatus = THREAD_STATUS_USE_BLE;
				pthread_mutex_unlock(&pThreadInfo->threadMutex);
				break;
			}
		}

SEND_BLE:
		// TODO: shbaek: SEND COMMAND USE BLE
		if(iDBG)GRIB_LOGD("# %s-CTR>: SEND COMMAND USE BLE\n", deviceID);

		STRINIT(bleSendBuff, sizeof(bleSendBuff));
		STRINIT(bleRecvBuff, sizeof(bleRecvBuff));

		if(iTimeCheck)
		{
			sysTimer = time(NULL);
			sysTime  = localtime(&sysTimer);
			GRIB_LOGD("# %s-CTR>: SEND TIME: %02d:%02d:%02d\n", deviceID, sysTime->tm_hour, sysTime->tm_min, sysTime->tm_sec);
		}

		//2 shbaek: WAIT FOR BLE RE-USE
		SLEEP(GRIB_WAIT_BLE_REUSE_TIME);

		if(iDBG)GRIB_LOGD("# %s-CTR>: COMMAND FUNC: %s\n", deviceID, cmdFuncName);
		if(iDBG)GRIB_LOGD("# %s-CTR>: COMMAND CON : %s\n", deviceID, cmdConValue);

		iRes = Grib_BleSetFuncData(pDeviceInfo->deviceAddr, deviceID, cmdFuncName, cmdConValue, bleRecvBuff);

		if(iTimeCheck)
		{
			sysTimer = time(NULL);
			sysTime  = localtime(&sysTimer);
			GRIB_LOGD("# %s-CTR>: RECV TIME: %02d:%02d:%02d\n", deviceID, sysTime->tm_hour, sysTime->tm_min, sysTime->tm_sec);
		}

		if(iRes == GRIB_DONE)
		{
			char* pRes = NULL;

			pRes = Grib_Split(bleRecvBuff, GRIB_LN, 0);
			if( (pRes == NULL) || (STRNCMP(pRes, BLE_RESPONSE_STR_ERROR, STRLEN(BLE_RESPONSE_STR_ERROR))==0) )
			{//3 shbaek: RECV BLE ERROR MSG
				GRIB_LOGD("# %s-CTR>: RES ERROR RE-TRY CHANCE: %d\n", deviceID, iTry+1);
				if(iTry < 3)
				{
					iTry++;
					SLEEP(GRIB_CONTROL_FAIL_WAIT_TIME_SEC);
					goto SEND_BLE;
				}
				else
				{
					GRIB_LOGD("# %s-CTR>: ##### ##### ##### ##### ##### ##### #####\n", deviceID);
					GRIB_LOGD("# %s-CTR>: #####       CRITICAL ERROR          #####\n", deviceID);
					GRIB_LOGD("# %s-CTR>: ##### ##### ##### ##### ##### ##### #####\n", deviceID);
				}
			}

			GRIB_LOGD("# %s-CTR>: %s SET %s: %s\n", deviceID, cmdFuncName, cmdConValue, pRes);
		}
		else
		{//3 shbaek: BLE LOCAL ERROR
			GRIB_LOGD("# %s-CTR>: BLE ERROR RE-TRY CHANCE: %d\n", deviceID, iTry+1);
			if(iTry < 3)
			{
				iTry++;
				SLEEP(GRIB_CONTROL_FAIL_WAIT_TIME_SEC);
				goto SEND_BLE;
			}
			else
			{
				GRIB_LOGD("# %s-CTR>: ##### ##### ##### ##### ##### ##### #####\n", deviceID);
				GRIB_LOGD("# %s-CTR>: #####       CRITICAL ERROR          #####\n", deviceID);
				GRIB_LOGD("# %s-CTR>: ##### ##### ##### ##### ##### ##### #####\n", deviceID);
			}
		}

		GRIB_LOGD("# %s-CTR>: THREAD_STATUS_NEED_ANSWER !!!\n", deviceID);

		pthread_mutex_lock(&pThreadInfo->threadMutex);
		pThreadInfo->controlThreadStatus = THREAD_STATUS_NEED_ANSWER;
		pthread_mutex_unlock(&pThreadInfo->threadMutex);

		iTry = 0;

	}

	return pThreadInfo;
}

void* Grib_ReportThread(void* threadArg)
{
	int i = 0;
	int iRes = GRIB_ERROR;
	int iDBG = gDebugThread;
	int iCycleTime = 0;
	int checkStatus = THREAD_STATUS_NONE;

	OneM2M_ReqParam reqParam;
	OneM2M_ResParam resParam;

	Grib_DeviceThreadInfo *pThreadInfo;
	Grib_DbRowDeviceInfo* pDeviceInfo;
	Grib_DbRowDeviceFunc** ppFuncInfo;

	char* deviceID = NULL;
	char* pFuncName = NULL;
	char* pRes = NULL;

	char bleSendBuff[BLE_MAX_SIZE_SEND_MSG];
	char bleRecvBuff[BLE_MAX_SIZE_RECV_MSG];

	int iTimeCheck = gDebugThread;
	time_t sysTimer;
	struct tm *sysTime;

	if(threadArg == NULL)
	{
		GRIB_LOGD("0x%x THREAD INFO IS NULL", pthread_self());
		return NULL;
	}

	//shbaek: Pre-Pare
	pThreadInfo	= (Grib_DeviceThreadInfo *)threadArg;
	pDeviceInfo	= pThreadInfo->pRowDeviceInfo;
	ppFuncInfo	= pDeviceInfo->ppRowDeviceFunc;
	deviceID	= pDeviceInfo->deviceID;
	iCycleTime	= pDeviceInfo->reportCycle;
	if(iCycleTime < 1)
	{
		iCycleTime = GRIB_DEFAULT_REPORT_CYCLE;
	}

	GRIB_LOGD("# %s-RPT<: THREAD START\n", deviceID);

	MEMSET(&reqParam, GRIB_INIT, sizeof(OneM2M_ReqParam));
	MEMSET(&reqParam, GRIB_INIT, sizeof(OneM2M_ResParam));

	STRINIT(reqParam.xM2M_Origin, sizeof(reqParam.xM2M_Origin));
	STRNCPY(reqParam.xM2M_Origin, deviceID, STRLEN(deviceID));

	STRINIT(&reqParam.xM2M_CNF, sizeof(reqParam.xM2M_CNF));
	SNPRINTF(&reqParam.xM2M_CNF, sizeof(reqParam.xM2M_CNF), "%s:0", HTTP_CONTENT_TYPE_TEXT);

	while(TRUE)
	{
		// TODO: shbaek: GET FUNCTION's REPORT DATA
		for(i=0; i<pDeviceInfo->deviceFuncCount; i++)
		{
			int useReport = FALSE;
			Grib_DbRowDeviceFunc* pFuncInfo;

			pFuncInfo = ppFuncInfo[i];
			pFuncName = pFuncInfo->funcName;
			useReport = FUNC_ATTR_CHECK_REPORT(pFuncInfo->funcAttr);
			if(iDBG)GRIB_LOGD("# %s-RPT<: %s USE REPORT: %s[%d]\n", deviceID, pFuncName, GRIB_BOOL_TO_STR(useReport), pFuncInfo->funcAttr);

			if(useReport == FALSE)
			{//3 shbaek: Attribute Have Not Reporting Flag.
				GRIB_LOGD("# %s-RPT<: %s NEED NOT REPORTING\n", deviceID, pFuncName);
				continue;
			}

			while(TRUE)
			{
				checkStatus = THREAD_STATUS_NONE;

				pthread_mutex_lock(&pThreadInfo->threadMutex);
				checkStatus = pThreadInfo->controlThreadStatus;
				pthread_mutex_unlock(&pThreadInfo->threadMutex);

				if(iDBG)GRIB_LOGD("# %s-RPT<: BLE CHECK STATUS: %s[%d]\n", deviceID, Grib_ThreadStatusToStr(checkStatus), checkStatus);

				if(checkStatus == THREAD_STATUS_USE_BLE)
				{//shbaek: Wait for Control Thread.
					SLEEP(1);
					continue;
				}
				else
				{//shbaek: It's My Turn.
					pthread_mutex_lock(&pThreadInfo->threadMutex);
					pThreadInfo->reportThreadStatus = THREAD_STATUS_USE_BLE;
					pthread_mutex_unlock(&pThreadInfo->threadMutex);
					break;
				}
			}//shbaek: while(TRUE)

			STRINIT(bleSendBuff, sizeof(bleSendBuff));
			STRINIT(bleRecvBuff, sizeof(bleRecvBuff));

			if(iTimeCheck)
			{
				sysTimer = time(NULL);
				sysTime  = localtime(&sysTimer);
				GRIB_LOGD("# %s-RPT<: SEND TIME: %02d:%02d:%02d\n", deviceID, sysTime->tm_hour, sysTime->tm_min, sysTime->tm_sec);
			}

			//2 shbaek: WAIT FOR BLE RE-USE
			if(1 < pDeviceInfo->deviceFuncCount)SLEEP(GRIB_WAIT_BLE_REUSE_TIME);

			iRes = Grib_BleGetFuncData(pDeviceInfo->deviceAddr, deviceID, pFuncName, bleRecvBuff);

			pthread_mutex_lock(&pThreadInfo->threadMutex);
			pThreadInfo->reportThreadStatus = THREAD_STATUS_NONE;
			pthread_mutex_unlock(&pThreadInfo->threadMutex);

			if(iTimeCheck)
			{
				sysTimer = time(NULL);
				sysTime  = localtime(&sysTimer);
				GRIB_LOGD("# %s-RPT<: RECV TIME: %02d:%02d:%02d\n", deviceID, sysTime->tm_hour, sysTime->tm_min, sysTime->tm_sec);
			}

			if(iRes != GRIB_DONE)
			{
				GRIB_LOGD("# %s-RPT<: %s GET STATUS FAIL\n", deviceID, pFuncName);
				continue;
			}

			pRes = Grib_Split(bleRecvBuff, GRIB_LN, 0);
			if( (pRes == NULL) || (STRNCMP(pRes, GRIB_STR_ERROR, STRLEN(GRIB_STR_ERROR))==0) )
			{
				GRIB_LOGD("# %s-RPT<: GET %s STATUS ERROR\n", deviceID, pFuncName);
				continue;
			}
			if(iDBG)GRIB_LOGD("# %s-RPT<: GET %s STATUS: %s\n", deviceID, pFuncName, pRes);

			STRINIT(reqParam.xM2M_URI, sizeof(reqParam.xM2M_URI));
			SNPRINTF(reqParam.xM2M_URI, sizeof(reqParam.xM2M_URI), "%s/%s/%s", deviceID, pFuncName, ONEM2M_URI_CONTENT_STATUS);

			STRINIT(&reqParam.xM2M_CON, sizeof(reqParam.xM2M_CON));
			STRNCPY(reqParam.xM2M_CON, pRes, STRLEN(pRes));

#if (TEST_SKIP_USE_ONEM2M == OFF)
			iRes = Grib_ContentInstanceCreate(&reqParam, &resParam);
#else
			iRes = GRIB_DONE;
#endif
			if(iRes == GRIB_ERROR)
			{
				//3 shbaek: Scenario?
				GRIB_LOGD("# %s-RPT<: REPORT ERROR !!!\n", deviceID);
				continue;
			}
		}
		GRIB_LOGD("# %s-RPT<: REPORT DONE\n", deviceID);
		GRIB_LOGD("\n");

		for(i=0; i<iCycleTime; i++)
		{
			checkStatus = THREAD_STATUS_NONE;

			//shbaek: Check for Control Thread Status
			pthread_mutex_lock(&pThreadInfo->threadMutex);
			checkStatus = pThreadInfo->controlThreadStatus;
			pthread_mutex_unlock(&pThreadInfo->threadMutex);

			if(iDBG)GRIB_LOGD("# %s-RPT<: DELAY CHECK STATUS: %s[%d]\n", deviceID, Grib_ThreadStatusToStr(checkStatus), checkStatus);

			if(checkStatus == THREAD_STATUS_NEED_ANSWER)
			{//shbaek: Answer Me Now
				GRIB_LOGD("# %s-RPT<: ANSWER ME NOW PLEASE ...\n", deviceID);

				//shbaek: Init Answer Flag
				pthread_mutex_lock(&pThreadInfo->threadMutex);
				pThreadInfo->controlThreadStatus = THREAD_STATUS_NONE;
				pthread_mutex_unlock(&pThreadInfo->threadMutex);				
				break;
			}
			else
			{//shbaek: Wait For Next Report Time
				SLEEP(1);
			}
		}
		//shbaek: Wait For Next Report Time

	}

	return pThreadInfo;
}

void* Grib_ResetThread(void* threadArg)
{
	int iDBG = gDebugThread;
	time_t sysTimer;
	struct tm *sysTime;

	while(TRUE)
	{
		sysTimer = time(NULL);
		sysTime  = localtime(&sysTimer);
		if(iDBG)GRIB_LOGD("# RESET-THREAD: %02d:%02d:%02d\n", sysTime->tm_hour, sysTime->tm_min, sysTime->tm_sec);
		SLEEP(10);

		if( ((sysTime->tm_hour%6)==0) && (sysTime->tm_min==0) )
		{
			char pBuff[GRIB_MAX_SIZE_BRIEF] = {'\0', };
			char* REBOOT_COMMAND = "sudo reboot";

			GRIB_LOGD("# RESET-THREAD: RESET TIME !!!\n");
			SLEEP(10);
			systemCommand(REBOOT_COMMAND, pBuff, sizeof(pBuff));
		}
	}

	return threadArg;
}

void* Grib_HubThread(void* threadArg)
{
	int i = 0;
	int iRes = GRIB_ERROR;
	int iDBG = gDebugThread;
	int iTry = 0;

	OneM2M_ReqParam reqParam;
	OneM2M_ResParam resParam;

	Grib_HubThreadInfo *pThreadInfo;

	char* deviceID = NULL;
	char* cmdConValue = NULL;
	char* cmdFuncName = NULL;

	int iTimeCheck = FALSE;
	time_t sysTimer;
	struct tm *sysTime;

	if(gHubID == NULL)
	{
		GRIB_LOGD("# 0x%x HUB ID IS NULL", pthread_self());

		iRes = Grib_SetThreadConfig();
		if(iRes != GRIB_DONE)
		{
			return NULL;
		}
	}

	iRes = Grib_HubRegi();
	if(iRes != GRIB_DONE)
	{
		GRIB_LOGD("# %s: HUB TREE CREATE ERROR !!!\n", gHubID);
	}

	iRes = Grib_UpdateHubInfo();
	if(iRes != GRIB_DONE)
	{
		GRIB_LOGD("# %s: HUB INFO UPDATE ERROR !!!\n", gHubID);
	}

	if(threadArg == NULL)
	{
		GRIB_LOGD("# %s: THREAD INFO IS NULL", gHubID);
		return NULL;
	}

	//shbaek: Pre-Pare
	pThreadInfo	= (Grib_HubThreadInfo *)threadArg;
	deviceID	= gHubID;

	MEMSET(&reqParam, GRIB_INIT, sizeof(OneM2M_ReqParam));
	MEMSET(&reqParam, GRIB_INIT, sizeof(OneM2M_ResParam));

	STRINIT(reqParam.xM2M_Origin, sizeof(reqParam.xM2M_Origin));
	STRNCPY(reqParam.xM2M_Origin, deviceID, STRLEN(deviceID));

	while(TRUE)
	{
		GRIB_LOGD("# %s: POLLING START\n", deviceID);

		iRes = Grib_LongPolling(&reqParam, &resParam);
		if(iRes != GRIB_DONE)
		{
			if(resParam.http_ResNum == HTTP_STATUS_CODE_REQUEST_TIME_OUT)
			{
				GRIB_LOGD("# %s: POLLING TIME OUT\n", deviceID);
			}
			else
			{
				GRIB_LOGD("# %s: POLLING ERROR MSG: %s[%d]\n", deviceID, resParam.http_ResMsg, resParam.http_ResNum);
				SLEEP(1);
			}

			continue;
		}

		cmdConValue = resParam.xM2M_Content;
		GRIB_LOGD("# %s: COMMAND: %s\n", deviceID, cmdConValue);

		if(STRCASECMP(cmdConValue, "reboot") == 0)
		{
			char pBuff[GRIB_MAX_SIZE_BRIEF] = {'\0', };
			char* REBOOT_COMMAND = "sudo reboot";

			GRIB_LOGD("# %s: RESET TIME !!!\n", deviceID);
			SLEEP(10);
			systemCommand(REBOOT_COMMAND, pBuff, sizeof(pBuff));
		}
		else
		{
			GRIB_LOGD("# %s: UN-KNOWN COMMAND: %s\n", deviceID, cmdConValue);
		}

	}

	return pThreadInfo;
}

int Grib_ThreadStart(void)
{
	int i = 0;
	int iRes = GRIB_ERROR;
	int iDeviceCount = 0;
	int iDeviceMaxCount = 0;

	int iFuncCount = 0;
	int iCheckCount = 0;

	Grib_DbRowDeviceInfo*  pRowDeviceInfo;
	Grib_DbRowDeviceFunc*  pRowDeviceFunc;

	const char* FUNC_TAG = "# THREAD-START:";

	//shbaek: Config
	iRes = Grib_SetThreadConfig();
	if(iRes != GRIB_DONE)
	{
		GRIB_LOGD("%s SERVER CONFIG ERROR\n", FUNC_TAG);
		goto FINAL;
	}
	iRes = Grib_SetServerConfig();
	if(iRes != GRIB_DONE)
	{
		GRIB_LOGD("%s SERVER CONFIG ERROR\n", FUNC_TAG);
		goto FINAL;
	}
	iRes = Grib_BleConfig();
	if(iRes != GRIB_DONE)
	{
		GRIB_LOGD("%s BLE CONFIG ERROR\n", FUNC_TAG);
		goto FINAL;
	}

	//shbaek: Init
	iRes = Grib_BleDetourInit();
	if(iRes != GRIB_DONE)
	{
		GRIB_LOGD("%s BLE INIT FAIL\n", FUNC_TAG);
	}

	iRes = Grib_BleCleanAll();
	if(iRes != GRIB_DONE)
	{
		GRIB_LOGD("%s BLE PIPE CLEAN FAIL\n", FUNC_TAG);
	}

	//shbaek: Pre-Pare
	iRes = Grib_DbCreate();
	if(iRes != GRIB_DONE)
	{
		GRIB_LOGD("%s NEED NOT DB CREATE\n", FUNC_TAG);
		Grib_DbClose();
	}


	gDbAll = (Grib_DbAll *)MALLOC(sizeof(Grib_DbAll));
	MEMSET(gDbAll, GRIB_INIT, sizeof(Grib_DbAll));

	iRes = Grib_DbToMemory(gDbAll);
	if(iRes == GRIB_ERROR)
	{
		GRIB_LOGD("%s GET DATABASE ERROR\n", FUNC_TAG);
		goto FINAL;
	}

	iDeviceMaxCount = gDbAll->deviceCount;
	GRIB_LOGD("%s DEVICE ALL COUNT: %d\n", FUNC_TAG, iDeviceMaxCount);

	if(iDeviceMaxCount == 0)
	{
		GRIB_LOGD("# ########## ########## ########## ##########\n");
		GRIB_LOGD("%s NO REGISTERED DEVICES\n", FUNC_TAG);
		GRIB_LOGD("%s REGISTE YOUR DEVICE ... \n", FUNC_TAG);
		GRIB_LOGD("# ########## ########## ########## ##########\n");
		goto FINAL;
	}
	else
	{
		Grib_UpdateDeviceInfo(gDbAll);
	}

	GRIB_LOGD("%s DEVICE INFO SETTING\n", FUNC_TAG);
	gDevThreadList = (Grib_DeviceThreadInfo**)MALLOC(sizeof(Grib_DeviceThreadInfo*)*iDeviceMaxCount);
	for(iDeviceCount=0; iDeviceCount<iDeviceMaxCount; iDeviceCount++)
	{
		gDevThreadList[iDeviceCount] = (Grib_DeviceThreadInfo*)MALLOC(sizeof(Grib_DeviceThreadInfo));
		MEMSET(gDevThreadList[iDeviceCount], GRIB_INIT, sizeof(Grib_DeviceThreadInfo));

		pRowDeviceInfo = gDevThreadList[iDeviceCount]->pRowDeviceInfo = gDbAll->ppRowDeviceInfo[iDeviceCount];

		GRIB_LOGD("%s DEVICE[%d/%d] ID:%s ADDR:%s\n",
			FUNC_TAG,
			iDeviceCount, iDeviceMaxCount,
			gDevThreadList[iDeviceCount]->pRowDeviceInfo->deviceID,
			gDevThreadList[iDeviceCount]->pRowDeviceInfo->deviceAddr);
	}

	GRIB_LOGD("%s ATTR/COND/MUTEX INIT\n", FUNC_TAG);
	//shbaek: Init
	for(i=0; i<iDeviceMaxCount; i++)
	{
		pthread_attr_init(&gDevThreadList[i]->threadAttr);
		pthread_cond_init(&gDevThreadList[i]->threadCond, GRIB_NOT_USED);
		pthread_mutex_init(&gDevThreadList[i]->threadMutex, GRIB_NOT_USED);		
	}

	//shbaek: Create
	for(i=0; i<iDeviceMaxCount; i++)
	{
		int   iAttr = GRIB_INIT;
		char *strDeviceID = gDevThreadList[i]->pRowDeviceInfo->deviceID;

		iAttr = Grib_DeviceCheckAttr(gDevThreadList[i]->pRowDeviceInfo);

		if(FUNC_ATTR_CHECK_CONTROL(iAttr)==TRUE)
		{
			GRIB_LOGD("%s %s CONTROL THREAD CREATE\n", FUNC_TAG, strDeviceID);
#if (TEST_SKIP_USE_ONEM2M == OFF)
			pthread_create(&gDevThreadList[i]->controlThreadID, NULL, Grib_ContorolThread, (void *)gDevThreadList[i]);
#endif
			SLEEP(1);
		}
		else
		{
			GRIB_LOGD("%s %s CONTROL THREAD SKIP !!!\n", FUNC_TAG, strDeviceID);
		}

		if(FUNC_ATTR_CHECK_REPORT(iAttr)==TRUE)
		{
			GRIB_LOGD("%s %s REPORT THREAD CREATE\n", FUNC_TAG, strDeviceID);
			pthread_create(&gDevThreadList[i]->reportThreadID, NULL, Grib_ReportThread, (void *)gDevThreadList[i]);
			SLEEP(1);
		}
		else
		{
			GRIB_LOGD("%s %s REPORT THREAD SKIP !!!\n", FUNC_TAG, strDeviceID);
		}

	}
	
	//3 shbaek: Hub Thread Init & Create
	gHubThread = (Grib_HubThreadInfo*)MALLOC(sizeof(Grib_HubThreadInfo));
	MEMSET(gHubThread, 0x00, sizeof(Grib_HubThreadInfo));

	pthread_attr_init(&gHubThread->threadAttr);
	pthread_cond_init(&gHubThread->threadCond, GRIB_NOT_USED);
	pthread_mutex_init(&gHubThread->threadMutex, GRIB_NOT_USED);
	pthread_create(&gHubThread->threadID, &gHubThread->threadAttr, Grib_HubThread, (void *)gHubThread);

	//3 shbaek: Reset Thread Init & Create
	gResetThread = (Grib_HubThreadInfo*)MALLOC(sizeof(Grib_HubThreadInfo));
	MEMSET(gResetThread, 0x00, sizeof(Grib_HubThreadInfo));

	pthread_attr_init(&gResetThread->threadAttr);
	pthread_cond_init(&gResetThread->threadCond, GRIB_NOT_USED);
	pthread_mutex_init(&gResetThread->threadMutex, GRIB_NOT_USED);
	pthread_create(&gResetThread->threadID, &gResetThread->threadAttr, Grib_ResetThread, (void *)gResetThread);

	GRIB_LOGD("%s THREAD WAIT\n", FUNC_TAG);
	//shbaek: Wait
	for(i=0; i<iDeviceMaxCount; i++)
	{
		char *strDeviceID = gDevThreadList[i]->pRowDeviceInfo->deviceID;

		if(0 < gDevThreadList[i]->controlThreadID)
		{
			//GRIB_LOGD("%s %s CONTROL THREAD[%d/%d] WAIT\n", FUNC_TAG, strDeviceID, i, iDeviceMaxCount);
			pthread_join(gDevThreadList[i]->controlThreadID, (void **)&gDevThreadList[i]);
		}
		else
		{
			GRIB_LOGD("%s %s CONTROL THREAD NEED NOT WAIT !!!\n", FUNC_TAG, strDeviceID);
		}

		if(0 < gDevThreadList[i]->reportThreadID)
		{
			//GRIB_LOGD("%s %s REPORT THREAD[%d/%d] WAIT\n", FUNC_TAG, strDeviceID, i, iDeviceMaxCount);
			pthread_join(gDevThreadList[i]->reportThreadID, (void **)&gDevThreadList[i]);
		}
		else
		{
			GRIB_LOGD("%s %s REPORT THREAD NEED NOT WAIT !!!\n", FUNC_TAG, strDeviceID);
		}

	}

	//3 shbaek: Other Thread Wait
	pthread_join(gHubThread->threadID, (void **)&gHubThread);
	pthread_join(gResetThread->threadID, (void **)&gResetThread);

	GRIB_LOGD("%s ########## ########## ########## ########## ########## ########## ########## ##########\n", FUNC_TAG);

FINAL:
	GRIB_LOGD("%s THREAD FINAL\n", FUNC_TAG);
	if(gDevThreadList != NULL)
	{
		for(i=0; i<iDeviceMaxCount; i++)
		{
			pthread_mutex_destroy(&gDevThreadList[i]->threadMutex);
			FREE(gDevThreadList[i]);
		}
		FREE(gDevThreadList);
		gDevThreadList = NULL;
	}

	if(gDbAll != NULL)
	{
		for(i=0; i<iDeviceMaxCount; i++)
		{
			pRowDeviceInfo = gDbAll->ppRowDeviceInfo[iDeviceCount];
			Grib_DbFreeRowFunc(pRowDeviceInfo->ppRowDeviceFunc, pRowDeviceInfo->deviceFuncCount);
			if(pRowDeviceInfo!=NULL)FREE(pRowDeviceInfo);
		}
		if(gDbAll->ppRowDeviceInfo!=NULL)FREE(gDbAll->ppRowDeviceInfo);
		FREE(gDbAll);
		gDbAll = NULL;
	}
	Grib_DbClose();

	return iRes;
}


#define __ONEM2M_ETC_FUNC__

