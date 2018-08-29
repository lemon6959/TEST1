#include "supervisorTest.h"

#include "appConfig.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "log.h"
#include "assertWithHalt.h"
#include "bitMark.h"

#include "uIPDriver.h"
#include "paramStorage.h"

#include <string.h>

/*** Definition ***/
#define LOG_RPT_ID          0

#define LOG_RPT_WARN        0
#define LOG_RPT_CLR_WARN    0

typedef struct{
    uint8_t         m_aucRptIPAddr[4];
    
    rptID_t         m_xRptID;
    
    uIPTcpCtrl_t *m_pxUIPTCPAppData;
    TCPConnCfg_t    *m_pxTCPConnCfg;
}spvRpt_t;

const TCPConnCbk_t m_xTCPConnCbk_Spvt = {
    xSPVT_TCPCbk_EstbConn,
    xSPVT_TCPCbk_LostConn,
    xSPVT_TCPCbk_RcvMsg,
    0,
    xSPVT_TCPCbk_Poll
};

/*** static variable ***/
static spvRpt_t m_axSpvRpt[MAX_RPT_NUM] = {0};
static SemaphoreHandle_t m_xMutex;
static portTickType m_xSystime;
static TCPConnCfg_t m_xTCPConnCfg = {
    "Supervisor",
    {0},
    TCP_CONN_TYPE_SVR,
    0,
    0,
    (TCPConnCbk_t*)&m_xTCPConnCbk_Spvt
};


/*** functions ***/

#if 1   // msg proc
static void vSPVT_ProcMsg_RptID(uint8_t *pucMsgBuf, uint16_t usMsgLen, spvRpt_t *pxRpt)
{
    assert(pucMsgBuf != NULL);
    
    spvtMsg_rptID_t *pxSpvMsg_rptID = (spvtMsg_rptID_t *)pucMsgBuf;
    rptID_t xRptID = pxSpvMsg_rptID->m_xRptID;

#if LOG_RPT_ID
    vLog(LOG_LEVEL_INFOR, "SPVT", "ID msg from reporter @ room #%d, testBench #%d",
             xRptID.m_ucRoomID, xRptID.m_ucTestBenchID);
#endif
}

static void vSPVT_ProcMsg_Warn(uint8_t *pucMsgBuf, uint16_t usMsgLen, spvRpt_t *pxRpt)
{
    assert(pucMsgBuf != NULL);
    
    spvtMsg_warn_t *pxSpvMsg_warn = (spvtMsg_warn_t *)pucMsgBuf;
    
    rptID_t xRptID = pxSpvMsg_warn->m_xRptID;
    uint8_t ucWarnCode = pxSpvMsg_warn->m_ucWarnCode;
    
#if LOG_RPT_WARN
    if(ucWarnCode == 1)
    {
        vLog(LOG_LEVEL_INFOR, "SPVT", "Warning: engine offline @ room #%d, testBench #%d",
             xRptID.m_ucRoomID, xRptID.m_ucTestBenchID);
    }else if(ucWarnCode == 2)
    {
        vLog(LOG_LEVEL_INFOR, "SPVT", "Warning: engine in trouble @ room #%d, testBench #%d",
             xRptID.m_ucRoomID, xRptID.m_ucTestBenchID);
    }
#endif  // LOG_RPT_WARN
}

static void vSPVT_ProcMsg_ClearWarn(uint8_t *pucMsgBuf, uint16_t usMsgLen, spvRpt_t *pxRpt)
{
    assert(pucMsgBuf != NULL);
    
    spvtMsg_warn_t *pxSpvMsg_warn = (spvtMsg_warn_t *)pucMsgBuf;
    
    rptID_t xRptID = pxSpvMsg_warn->m_xRptID;
    uint8_t ucWarnCode = pxSpvMsg_warn->m_ucWarnCode;
    
#if LOG_RPT_CLR_WARN
    if(ucWarnCode == 1)
    {
        vLog(LOG_LEVEL_INFOR, "SPVT", "ClearWarning: engine offline @ room #%d, testBench #%d",
             xRptID.m_ucRoomID, xRptID.m_ucTestBenchID);
    }else if(ucWarnCode == 2)
    {
        vLog(LOG_LEVEL_INFOR, "SPVT", "ClearWarning: engine in trouble @ room #%d, testBench #%d",
             xRptID.m_ucRoomID, xRptID.m_ucTestBenchID);
    }
#endif  // LOG_RPT_CLR_WARN
}

static void vSPVT_ProcMsgFromTCP(uint8_t *pucMsgBuf, uint16_t usMsgLen, spvRpt_t *pxRpt)
{
    assert(pucMsgBuf != NULL);
    assert(usMsgLen >= sizeof(uint32_t));
    
    uint32_t ulMsgCode = *((uint32_t*)pucMsgBuf);
    
    switch(ulMsgCode)
    {
    case SPVT_CMDR_REQ_RPT_ID:
        vSPVT_ProcMsg_RptID(pucMsgBuf, usMsgLen, pxRpt);
        break;
    case SPVT_CMD_WARN:
        vSPVT_ProcMsg_Warn(pucMsgBuf, usMsgLen, pxRpt);
        break;
    case SPVT_CMD_CLEAR_WARN:
        vSPVT_ProcMsg_ClearWarn(pucMsgBuf, usMsgLen, pxRpt);
        break;
    default:
        vLog(LOG_LEVEL_INFOR, "SPVT", "Unknown MsgCode %d", ulMsgCode);
    }
}

void vSPVT_ProcMsg(uint8_t *pucMsgBuf, uint16_t usMsgLen)
{
    vSPVT_ProcMsgFromTCP(pucMsgBuf, usMsgLen, NULL);
}
#endif  // msg proc


#if 1   // supervisor task
void vSPVT_Init(void)
{
    m_xMutex = xSemaphoreCreateBinary();
    xSemaphoreGive(m_xMutex);
    
    uint8_t ucSpvID;
    vPMS_ReadRaramByIdx(PI_LCL_SPV_ID, &ucSpvID);
    
    uint16_t usSpvTCPPort;
    vPMS_ReadRaramByIdx(PI_LCL_SPV_TCP_PORT, (uint8_t*)&usSpvTCPPort);
    
    
    m_xTCPConnCfg.m_usSvrPort = usSpvTCPPort;
    
    vUIP_RegTCPConn(&m_xTCPConnCfg);
    if(!bIsBitMarkSet(m_xTCPConnCfg.m_ucCfg, TCP_CONN_CFG_AUTO_START))
    {
        vUIP_StartTCPConn(&m_xTCPConnCfg);
    }
}


void vTask_SupervisorTest( void *pvParameters )
{
    while(!bUIP_IsRuning())
    {
        vTaskDelay(200);   // wait for uIP init
    }
    
    vLog(LOG_LEVEL_INFOR, "SPVT", "Start supervisor test task");
    vSPVT_Init();
    
    for( ;; )
    {
        vTaskDelayUntil(&m_xSystime, 500);
    }
}

#endif  // supervisor task

#if 1   // tcp callback
TCPOper_e xSPVT_TCPCbk_EstbConn(struct uip_conn *pxCurConn, void** ppvAppInst)
{
    assert(pxCurConn != NULL);
    assert(ppvAppInst != NULL);
    
    uIPTcpCtrl_t *pxUipTcpCtrl = pxCurConn->appstate;
    TCPConnCfg_t *pxTCPConnCfg = pxUipTcpCtrl->m_pxTCPConnCfg;
    
    uint8_t aucRptIPAddr[4];
    aucRptIPAddr[0] = uip_ipaddr1(pxCurConn->ripaddr);
    aucRptIPAddr[1] = uip_ipaddr2(pxCurConn->ripaddr);
    aucRptIPAddr[2] = uip_ipaddr3(pxCurConn->ripaddr);
    aucRptIPAddr[3] = uip_ipaddr4(pxCurConn->ripaddr);
    
    xSemaphoreTake(m_xMutex, portMAX_DELAY);
    spvRpt_t *pxRpt;
    for(int iRptIdx = 0; iRptIdx < MAX_RPT_NUM; iRptIdx++)
    {
        pxRpt = &m_axSpvRpt[iRptIdx];
        if(*((uint32_t*)aucRptIPAddr) == *((uint32_t*)pxRpt->m_aucRptIPAddr))
        {
            vLog(LOG_LEVEL_ERROR, "SPVT", "Multi-connection from reporter %d,%d,%d,%d",
                 aucRptIPAddr[0], aucRptIPAddr[1], aucRptIPAddr[2], aucRptIPAddr[3]);
            
            xSemaphoreGive(m_xMutex);
            return TCP_OPER_ABORT;
        }
    }
    
    pxRpt = NULL;
    for(int iRptIdx = 0; iRptIdx < MAX_RPT_NUM; iRptIdx++)
    {
        pxRpt = &m_axSpvRpt[iRptIdx];
        if(*((uint32_t*)pxRpt->m_aucRptIPAddr) == 0)
        {
            *((uint32_t*)pxRpt->m_aucRptIPAddr) = *((uint32_t*)aucRptIPAddr);
            pxRpt->m_pxTCPConnCfg = pxTCPConnCfg;
            pxRpt->m_pxUIPTCPAppData = pxUipTcpCtrl;
            break;
        }
    }
    xSemaphoreGive(m_xMutex);
    
    if(pxRpt == NULL)
    {
        vLog(LOG_LEVEL_ERROR, "SPVT", "Not enough space for reporter @ %d,%d,%d,%d",
            aucRptIPAddr[0], aucRptIPAddr[1], aucRptIPAddr[2], aucRptIPAddr[3]);
        return TCP_OPER_ABORT;
    }else{
        *ppvAppInst = pxRpt;
        
        vLog(LOG_LEVEL_INFOR, "SPVT", "Reporter registered from %d,%d,%d,%d",
            aucRptIPAddr[0], aucRptIPAddr[1], aucRptIPAddr[2], aucRptIPAddr[3]);
        
        spvtMsg_noBody_t xSpvMsg_noBody;
        xSpvMsg_noBody.m_ulMsgID = SPVT_CMD_REQ_RPT_ID;
        iSendTCPData(pxUipTcpCtrl, (uint8_t*)&xSpvMsg_noBody, sizeof(xSpvMsg_noBody));
        
        return TCP_OPER_NONE;
    }
}

TCPOper_e xSPVT_TCPCbk_RcvMsg(uint8_t* pucMsgBuf, unsigned int uiMsgLen, void* pvAppInst)
{
    spvRpt_t *pxRpt = (spvRpt_t*)pvAppInst;
    
    vSPVT_ProcMsgFromTCP(pucMsgBuf, uiMsgLen, pxRpt);
    
    return TCP_OPER_NONE;
}

TCPOper_e xSPVT_TCPCbk_LostConn(TCPCbkErrCode_e ucErrCode, void* pvAppInst)
{
    if(pvAppInst != NULL)
    {
        spvRpt_t *pxRpt = (spvRpt_t*)pvAppInst;
        
        vLog(LOG_LEVEL_INFOR, "SPVT", "Lost reporter @ %d,%d,%d,%d",
            pxRpt->m_aucRptIPAddr[0], pxRpt->m_aucRptIPAddr[1], 
            pxRpt->m_aucRptIPAddr[2], pxRpt->m_aucRptIPAddr[3]);
        
        memset(pxRpt, 0, sizeof(spvRpt_t));
    }
    
    return TCP_OPER_NONE;
}

TCPOper_e xSPVT_TCPCbk_Poll(void* pvAppInst)
{
    //spvRpt_t *pxRpt = (spvRpt_t*)pvAppInst;
    
    return TCP_OPER_NONE;
}
#endif  // tcp callback