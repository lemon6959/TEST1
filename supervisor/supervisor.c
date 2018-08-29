#include "supervisor.h"

#include "appConfig.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "log.h"
#include "assertWithHalt.h"
#include "bitMark.h"

#include <string.h>
#include <stdlib.h>

#include "Link_list.h"

#include "uIPDriver.h"
#include "paramStorage.h"


#define MAX_RPT_NUM  32

/*** Definition ***/



//add something;
const TCPConnCbk_t m_xTCPConnCbk_Spv = {
    xSPV_TCPCbk_EstbConn,
    xSPV_TCPCbk_LostConn,
    xSPV_TCPCbk_RcvMsg,
    0,
    xSPV_TCPCbk_Poll
};

typedef struct spvMsg_noBody_s
{
    uint32_t        m_ulMsgID;
}spvMsg_noBody_t;

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
    (TCPConnCbk_t*)&m_xTCPConnCbk_Spv
};
static rptID_t m_axOnlineSpvRpt[MAX_RPT_NUM] = {0};

/*** global variable ***/
Link_List* pxAlarmMsg_list;
SemaphoreHandle_t l_xMutex;

/*** functions ***/

#if 1   // msg proc
//删除若干故障码
spvMsg_warn_t *SPV_DelFaultCode(spvMsg_warn_t *pxSpvMsg_warn, uint8_t ucDelLen, uint8_t* pucDelBuf)
{
    int8_t i, j;
    for(i = 0; i < ucDelLen; i+=2)
    {
        for(j = 0; j < pxSpvMsg_warn->m_ucBufLen; j+=2)
        {
            if(pxSpvMsg_warn->m_aucBuf[j] == pucDelBuf[i] && pxSpvMsg_warn->m_aucBuf[j+1] == pucDelBuf[i+1])
            {
                for(int k = j +2; k < pxSpvMsg_warn->m_ucBufLen - 1; k+= 2)
                {
                    pxSpvMsg_warn->m_aucBuf[k - 2] = pxSpvMsg_warn->m_aucBuf[k];
                    pxSpvMsg_warn->m_aucBuf[k - 1] = pxSpvMsg_warn->m_aucBuf[k + 1];                                        
                }
                pxSpvMsg_warn->m_ucBufLen -= 2;     
                break;
            }            
        }
    }
    
    spvMsg_warn_t *pxSpvMsg_listNode = (spvMsg_warn_t*)malloc(sizeof(spvMsg_warn_t) + pxSpvMsg_warn->m_ucBufLen);
    memcpy(pxSpvMsg_listNode, pxSpvMsg_warn, sizeof(spvMsg_warn_t) + pxSpvMsg_warn->m_ucBufLen);
    
    return  pxSpvMsg_listNode;
}

void vSPV_NewWarnMsgToList(spvMsg_warn_t *pxSpvMsg_warn, Link_List *pxSpvMsg_warn_list)
{
    rptID_t xRptID = pxSpvMsg_warn->m_xRptID;
    uint8_t ucWarnCode = pxSpvMsg_warn->m_ucWarnCode;
    int8_t bufLen = pxSpvMsg_warn->m_ucBufLen;   
    
    xSemaphoreTake(l_xMutex, portMAX_DELAY);
    if(ucWarnCode == 1)   //停机
    {
        spvMsg_warn_t *pxSpvMsg_listNode = (spvMsg_warn_t*)malloc(sizeof(spvMsg_warn_t) + bufLen);
        memcpy(pxSpvMsg_listNode, pxSpvMsg_warn, sizeof(spvMsg_warn_t) + bufLen);
        Link_List_Insert(pxSpvMsg_warn_list, pxSpvMsg_listNode, -1);
    }
    else if(ucWarnCode == 2)  //故障
    {
        //查询报警消息列表中是否已存在同一台架发生故障
        Link_Node* p;
        int index = 0;
        for(p = pxSpvMsg_warn_list->head; p != NULL; p = p->nextNode)
        {
            spvMsg_warn_t* pdata = (spvMsg_warn_t*)p->data;
            //存在同一台架发生故障，拼成新的完整的故障码
            if(pdata->m_xRptID.m_ucRoomID == xRptID.m_ucRoomID && pdata->m_xRptID.m_ucTestBenchID == xRptID.m_ucTestBenchID && pdata->m_ucWarnCode == 2)
            {
                int8_t newBufLen = pdata->m_ucBufLen + bufLen;
                int8_t *newBuf = (int8_t*)malloc(newBufLen);
                memcpy(newBuf, pdata->m_aucBuf, pdata->m_ucBufLen);
                memcpy(newBuf + pdata->m_ucBufLen, pxSpvMsg_warn->m_aucBuf, bufLen);
                
                spvMsg_warn_t *pNodeData = pdata;
                Link_List_RemoveAt(pxSpvMsg_warn_list, index); 
                free(pNodeData);
                
                spvMsg_warn_t *pxSpvMsg_listNode = (spvMsg_warn_t *)malloc(sizeof(spvMsg_warn_t) + newBufLen);
                memcpy(pxSpvMsg_listNode, pxSpvMsg_warn, sizeof(spvMsg_warn_t) + pxSpvMsg_warn->m_ucBufLen);
                pxSpvMsg_listNode->m_ucBufLen = newBufLen;
                memcpy(pxSpvMsg_listNode->m_aucBuf, newBuf, newBufLen);
                
                free(newBuf);
                
                Link_List_Insert(pxSpvMsg_warn_list, pxSpvMsg_listNode, -1);
                
                break;
            }
            index++;
        }
        if(p == NULL)  //列表中没有相同台架发生故障，直接加入
        {
            spvMsg_warn_t *pxSpvMsg_listNode = (spvMsg_warn_t*)malloc(sizeof(spvMsg_warn_t) + bufLen);
            memcpy(pxSpvMsg_listNode, pxSpvMsg_warn, sizeof(spvMsg_warn_t) + bufLen);
            Link_List_Insert(pxSpvMsg_warn_list, pxSpvMsg_listNode, -1);
        }                       
    }
    else
        assert(0);
    xSemaphoreGive(l_xMutex);
}

void vSPV_ClearWarnMsgFromList(spvMsg_warn_t *pxSpvMsg_warn, Link_List *pxSpvMsg_warn_list)
{
    rptID_t xRptID = pxSpvMsg_warn->m_xRptID;
    uint8_t ucWarnCode = pxSpvMsg_warn->m_ucWarnCode;
    int8_t bufLen = pxSpvMsg_warn->m_ucBufLen;   
    
    xSemaphoreTake(l_xMutex, portMAX_DELAY);
    if(ucWarnCode == 1)   //停机，直接从列表中清除
    {
        Link_Node* p;
        int index = 0;
        for(p = pxSpvMsg_warn_list->head; p != NULL; p = p->nextNode)
        {
            spvMsg_warn_t* pdata = (spvMsg_warn_t*)p->data;
            if(pdata->m_xRptID.m_ucRoomID == xRptID.m_ucRoomID && pdata->m_xRptID.m_ucTestBenchID == xRptID.m_ucTestBenchID && pdata->m_ucWarnCode == 1)
            {                    
                spvMsg_warn_t *pNodeData = pdata;
                Link_List_RemoveAt(pxSpvMsg_warn_list, index); 
                free(pNodeData);
                
                break;
            }
            index++;
        }
    }
    else if(ucWarnCode == 2)   //故障，清除对应的故障码
    {
        Link_Node* p;
        int index = 0;
        for(p = pxSpvMsg_warn_list->head; p != NULL; p = p->nextNode)
        {
            spvMsg_warn_t* pdata = (spvMsg_warn_t*)p->data;
            if(pdata->m_xRptID.m_ucRoomID == xRptID.m_ucRoomID && pdata->m_xRptID.m_ucTestBenchID == xRptID.m_ucTestBenchID && pdata->m_ucWarnCode == 2)
            {                    
                int8_t newBufLen = pdata->m_ucBufLen - bufLen;
                
                if(newBufLen > 0)
                {
                    spvMsg_warn_t *pxSpvMsg_listNode = SPV_DelFaultCode(pdata, bufLen, pxSpvMsg_warn->m_aucBuf);   //新的节点
                    Link_List_Insert(pxSpvMsg_warn_list, pxSpvMsg_listNode, -1); 
                }
                
                spvMsg_warn_t *pNodeData = pdata;
                Link_List_RemoveAt(pxSpvMsg_warn_list, index); 
                free(pNodeData);
                
                break;
            }
            index++;
        }
    }
    xSemaphoreGive(l_xMutex);
}



//处理队列中的新消息
void vSPV_DealWithNewMsgOnList(spvMsg_warn_t *pxSpvMsg_warn, Link_List *pxSpvMsg_warn_list)
{
    uint32_t msgID = pxSpvMsg_warn->m_ulMsgID;
   
    if(msgID == SPV_CMD_WARN)    //收到报警命令
    {
        vSPV_NewWarnMsgToList(pxSpvMsg_warn, pxSpvMsg_warn_list);
    }    
    else if(msgID == SPV_CMD_CLEAR_WARN)  //清除报警
    {
        vSPV_ClearWarnMsgFromList(pxSpvMsg_warn, pxSpvMsg_warn_list);
    }                                               
}

//清除离线的reporter的报警消息
void vSPV_DelOfflineRptMsg(Link_List *pxSpvMsg_warn_list)
{
    xSemaphoreTake(l_xMutex, portMAX_DELAY);
    //遍历list
    Link_Node* p = pxSpvMsg_warn_list->head;
    int index = 0;
    while(p)
    {
        spvMsg_warn_t* pdata = (spvMsg_warn_t*)p->data;
        int iRptIdx;
        for(iRptIdx = 0; iRptIdx < MAX_RPT_NUM; iRptIdx++)
        {
            rptID_t xRptID = m_axSpvRpt[iRptIdx].m_xRptID;
            if(pdata->m_ulMsgID != SPV_CMD_WARN || 
               (pdata->m_xRptID.m_ucRoomID == xRptID.m_ucRoomID 
                && pdata->m_xRptID.m_ucTestBenchID == xRptID.m_ucTestBenchID))
            {
                break;
            }
        }        
        if(iRptIdx == MAX_RPT_NUM)
        {           
            spvMsg_warn_t *pNodeData = pdata;
            p = p->nextNode;
            Link_List_RemoveAt(pxSpvMsg_warn_list, index); 
            //index--;
            
            free(pNodeData);
        }
        else
        {
            p = p->nextNode;
            index++;
        }
        
    }
    xSemaphoreGive(l_xMutex);
}

static void vSPV_ProcMsg_Warn(uint8_t *pucMsgBuf, uint16_t usMsgLen, spvRpt_t *pxRpt)
{
    assert(pucMsgBuf != NULL);
    
    spvMsg_warn_t *pxSpvMsg_warn = (spvMsg_warn_t *)pucMsgBuf;
    
    uint8_t msgID = pxSpvMsg_warn->m_ulMsgID;
    rptID_t xRptID = pxSpvMsg_warn->m_xRptID;
    uint8_t ucWarnCode = pxSpvMsg_warn->m_ucWarnCode;
    
    pxRpt->m_xRptID.m_ucRoomID = xRptID.m_ucRoomID;
    pxRpt->m_xRptID.m_ucTestBenchID = xRptID.m_ucTestBenchID;
       
    if(msgID == SPV_CMD_WARN && ucWarnCode == 1)
    {
        vLog(LOG_LEVEL_INFOR, "SPV", "Warning: engine offline @ room #%d, testBench #%d",
             xRptID.m_ucRoomID, xRptID.m_ucTestBenchID);
    }else if(msgID == SPV_CMD_WARN && ucWarnCode == 2)
    {
        vLog(LOG_LEVEL_INFOR, "SPV", "Warning: engine in trouble @ room #%d, testBench #%d",
             xRptID.m_ucRoomID, xRptID.m_ucTestBenchID);
    }else if(msgID == SPV_CMD_CLEAR_WARN && ucWarnCode == 1)
    {
         vLog(LOG_LEVEL_INFOR, "SPV", "Warning cleared: engine offline @ room #%d, testBench #%d",
             xRptID.m_ucRoomID, xRptID.m_ucTestBenchID);
    }else if(msgID == SPV_CMD_CLEAR_WARN && ucWarnCode == 2)
    {
        vLog(LOG_LEVEL_INFOR, "SPV", "Warning cleared: engine trouble @ room #%d, testBench #%d",
             xRptID.m_ucRoomID, xRptID.m_ucTestBenchID);
    }

    uint8_t bufLen = ucWarnCode == 2 ? pxSpvMsg_warn->m_ucBufLen : 0;
    spvMsg_warn_t *pxSpvMsg_listNode = (spvMsg_warn_t*)malloc(sizeof(spvMsg_warn_t) + bufLen);
    memcpy(pxSpvMsg_listNode, pxSpvMsg_warn, sizeof(spvMsg_warn_t) + bufLen);
    
    //operate on alarm msg list
    vSPV_DealWithNewMsgOnList(pxSpvMsg_listNode, pxAlarmMsg_list);
    free(pxSpvMsg_listNode);
}

static void vSPV_ProcMsg_NetworkState(uint8_t *pucMsgBuf, uint16_t usMsgLen, spvRpt_t *pxRpt)
{
    assert(pucMsgBuf != NULL);
    
    xSemaphoreTake(m_xMutex, portMAX_DELAY); 
    for(int iRptIdx = 0; iRptIdx < MAX_RPT_NUM; iRptIdx++)
    {
        spvRpt_t *m_pxRpt = &m_axSpvRpt[iRptIdx];
        if(*((uint32_t*)m_pxRpt->m_aucRptIPAddr) == *((uint32_t*)pxRpt->m_aucRptIPAddr))
        {            
            pxRpt->m_xRptFlag = 1;
        }
    }
    xSemaphoreGive(m_xMutex); 
}

static void vSPV_ProcMsgFromTCP(uint8_t *pucMsgBuf, uint16_t usMsgLen, spvRpt_t *pxRpt)
{
    assert(pucMsgBuf != NULL);
    assert(usMsgLen >= sizeof(uint32_t));
         
    vSPV_ProcMsg_NetworkState(pucMsgBuf, usMsgLen, pxRpt);
    vSPV_ProcMsg_Warn(pucMsgBuf, usMsgLen, pxRpt);   
}

void vSPV_ProcMsg(uint8_t *pucMsgBuf, uint16_t usMsgLen)
{
    vSPV_ProcMsgFromTCP(pucMsgBuf, usMsgLen, NULL);
}
#endif  // msg proc

#if 1   // supervisor task
void vSPV_Init(void)
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
    
    pxAlarmMsg_list = Link_List_Init();
    l_xMutex = xSemaphoreCreateBinary();
    xSemaphoreGive(l_xMutex);
    
    
}

void vSPV_SendHeartBeatToRpt()
{
    spvRpt_t *pxRpt;
    xSemaphoreTake(m_xMutex, portMAX_DELAY); 
    for(int iRptIdx = 0; iRptIdx < MAX_RPT_NUM; iRptIdx++)
    {
        pxRpt = &m_axSpvRpt[iRptIdx];
        if(*((uint32_t*)pxRpt->m_aucRptIPAddr) != 0)
        {
            if(pxRpt->m_xRptFlag == 1)
            {
                spvMsg_noBody_t reqID;
                switch(SPV_CMD_REQ_RPT_ID)
                {
                case 1:
                    reqID.m_ulMsgID = 1;
                    break;
                default:
                    break;                    
                }
                iSendTCPData(pxRpt->m_pxUIPTCPAppData, (uint8_t*)&reqID, sizeof(spvMsg_noBody_t));
                pxRpt->m_xRptFlag = 0;
            }
            else
            {                
                pxRpt->m_xRptState = 0;   //offline
                vLog(LOG_LEVEL_INFOR, "SPV", "Lost heartbeat signal, lost reporter @ %d,%d,%d,%d",
                     pxRpt->m_aucRptIPAddr[0], pxRpt->m_aucRptIPAddr[1], 
                     pxRpt->m_aucRptIPAddr[2], pxRpt->m_aucRptIPAddr[3]);
                                
                memset(pxRpt, 0, sizeof(spvRpt_t));            
                vSPV_DelOfflineRptMsg(pxAlarmMsg_list);
                
            }
        }
    }               
    xSemaphoreGive(m_xMutex); 
}

void vSPV_CheckRptState()
{
    spvRpt_t *pxRpt;
    xSemaphoreTake(m_xMutex, portMAX_DELAY); 
    for(int iRptIdx = 0; iRptIdx < MAX_RPT_NUM; iRptIdx++)
    {
        pxRpt = &m_axSpvRpt[iRptIdx];
        if(*((uint32_t*)pxRpt->m_aucRptIPAddr) != 0)
        {
            if(pxRpt->m_xRptFlag == 1)
            {
                pxRpt->m_xRptState = 1;
                pxRpt->m_xRptFlag = 0;
            }
            else
            {
                pxRpt->m_xRptState = 0;
            }            
        }
    }
    xSemaphoreGive(m_xMutex); 
}

void vTask_Supervisor( void *pvParameters )
{
    while(!bUIP_IsRuning())
    {
        vTaskDelay(200);   // wait for uIP init
    }
    
    vLog(LOG_LEVEL_INFOR, "SPV", "Start supervisor task");
    vSPV_Init();
    
    for( ;; )
    {
        vTaskDelayUntil(&m_xSystime, 5000);
        vSPV_CheckRptState();                  
    }
}

#endif  // supervisor task

#if 1   // tcp callback
TCPOper_e xSPV_TCPCbk_EstbConn(struct uip_conn *pxCurConn, void** ppvAppInst)
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
    uint32_t *pulRptIPAddr = (uint32_t*)aucRptIPAddr;
    
    xSemaphoreTake(m_xMutex, portMAX_DELAY);
    spvRpt_t *pxRpt;
    for(int iRptIdx = 0; iRptIdx < MAX_RPT_NUM; iRptIdx++)
    {
        pxRpt = &m_axSpvRpt[iRptIdx];
        if(*((uint32_t*)aucRptIPAddr) == *((uint32_t*)pxRpt->m_aucRptIPAddr))
        {
            vLog(LOG_LEVEL_ERROR, "SPV", "Multi-connection from reporter %d,%d,%d,%d",
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
            pxRpt->m_xRptFlag = 1;
            pxRpt->m_xRptState = 1;
            break;
        }
    }
    xSemaphoreGive(m_xMutex);
    
    if(pxRpt == NULL)
    {
        vLog(LOG_LEVEL_ERROR, "SPV", "Not enough space for reporter @ %d,%d,%d,%d",
            aucRptIPAddr[0], aucRptIPAddr[1], aucRptIPAddr[2], aucRptIPAddr[3]);
        return TCP_OPER_ABORT;
    }else{
        *ppvAppInst = pxRpt;
        
        vLog(LOG_LEVEL_INFOR, "SPV", "Reporter registered from %d,%d,%d,%d",
            aucRptIPAddr[0], aucRptIPAddr[1], aucRptIPAddr[2], aucRptIPAddr[3]);     
        
        return TCP_OPER_NONE;
    }
}


void vSPV_AddRptLinkToList(spvMsg_warn_t *pxSpvMsg_warn, Link_List* alarmMsg_list, uint32_t linkState)
{
    rptID_t xRptID = pxSpvMsg_warn->m_xRptID;
    uint8_t ucWarnCode = pxSpvMsg_warn->m_ucWarnCode;
    int8_t bufLen = pxSpvMsg_warn->m_ucBufLen;   
    
    xSemaphoreTake(l_xMutex, portMAX_DELAY);
    
    spvMsg_warn_t *pxSpvMsg_listNode = (spvMsg_warn_t*)malloc(sizeof(spvMsg_warn_t) + bufLen);
    memcpy(pxSpvMsg_listNode, pxSpvMsg_warn, sizeof(spvMsg_warn_t) + bufLen);
    if(linkState == 2)
    {
        pxSpvMsg_listNode->m_ulMsgID = 2;
    }
    else if(linkState == 0)
    {
        pxSpvMsg_listNode->m_ulMsgID = 0;
    }
    else
    {
        assert(0);
    }
    Link_List_Insert(alarmMsg_list, pxSpvMsg_listNode, -1);
    
    xSemaphoreGive(l_xMutex);
}

TCPOper_e xSPV_TCPCbk_RcvMsg(uint8_t* pucMsgBuf, unsigned int uiMsgLen, void* pvAppInst)
{
    spvRpt_t *pxRpt = (spvRpt_t*)pvAppInst;
    
    if(pxRpt->m_xRptState)
    {
        //刚上线并且接收到第一个心跳信号，且该rpt处在离线列表中（接收第一条消息前，rptID为0）
        if(pxRpt->m_xRptID.m_ucRoomID == 0) 
        {
            spvMsg_warn_t *pxSpvMsg_warn = (spvMsg_warn_t *)pucMsgBuf;   
            
            //assert(pxSpvMsg_warn->m_ulMsgID == SPV_CMDR_REQ_RPT_ID);  
            
            rptID_t xRptID = pxSpvMsg_warn->m_xRptID;   
            pxRpt->m_xRptID.m_ucRoomID = xRptID.m_ucRoomID;
            pxRpt->m_xRptID.m_ucTestBenchID = xRptID.m_ucTestBenchID;
                                  
            //rpt列表
            int iRptIdx;
            xSemaphoreTake(m_xMutex, portMAX_DELAY); 
            for(iRptIdx = 0; iRptIdx < MAX_RPT_NUM; iRptIdx++)
            {
                rptID_t* m_rpt = &m_axOnlineSpvRpt[iRptIdx];
                if(m_rpt->m_ucRoomID == xRptID.m_ucRoomID && m_rpt->m_ucTestBenchID == xRptID.m_ucTestBenchID)
                {
                   break;
                }
            }
            if(iRptIdx == MAX_RPT_NUM)
            {
                //需要播报
                vLog(LOG_LEVEL_INFOR, "SPV", "reporter linked from %d,%d,%d,%d",
                     pxRpt->m_aucRptIPAddr[0], pxRpt->m_aucRptIPAddr[1], 
                     pxRpt->m_aucRptIPAddr[2], pxRpt->m_aucRptIPAddr[3]);  
                vSPV_AddRptLinkToList(pxSpvMsg_warn, pxAlarmMsg_list, 2);  //2:上线，0：掉线
                for(iRptIdx = 0; iRptIdx < MAX_RPT_NUM; iRptIdx++)
                {
                    rptID_t* m_rpt = &m_axOnlineSpvRpt[iRptIdx];
                    if(m_rpt->m_ucRoomID == 0 && m_rpt->m_ucTestBenchID == 0)
                    {
                        m_rpt->m_ucRoomID = xRptID.m_ucRoomID;
                        m_rpt->m_ucTestBenchID = xRptID.m_ucTestBenchID;
                        break;
                    }
                }
            }
            xSemaphoreGive(m_xMutex);
        }
        vSPV_ProcMsgFromTCP(pucMsgBuf, uiMsgLen, pxRpt);
    }
    else
    {
        vLog(LOG_LEVEL_INFOR, "SPV", "Recevie msg from offline reporter @ %d,%d,%d,%d",
            pxRpt->m_aucRptIPAddr[0], pxRpt->m_aucRptIPAddr[1], 
            pxRpt->m_aucRptIPAddr[2], pxRpt->m_aucRptIPAddr[3]);  
    }
    
    return TCP_OPER_NONE;
}

TCPOper_e  xSPV_TCPCbk_LostConn(TCPCbkErrCode_e ucErrCode, void* pvAppInst)
{
    if(pvAppInst != NULL)
    {
        spvRpt_t *pxRpt = (spvRpt_t*)pvAppInst;
        
        vLog(LOG_LEVEL_INFOR, "SPV", "Lost reporter @ %d,%d,%d,%d",
            pxRpt->m_aucRptIPAddr[0], pxRpt->m_aucRptIPAddr[1], 
            pxRpt->m_aucRptIPAddr[2], pxRpt->m_aucRptIPAddr[3]);                
        
        xSemaphoreTake(m_xMutex, portMAX_DELAY);  
        memset(pxRpt, 0, sizeof(spvRpt_t));
        xSemaphoreGive(m_xMutex);
        
        vSPV_DelOfflineRptMsg(pxAlarmMsg_list);
    }
    
    return TCP_OPER_NONE;
}

TCPOper_e xSPV_TCPCbk_Poll(void* pvAppInst)
{
    spvRpt_t *pxRpt = (spvRpt_t*)pvAppInst;
    
    
    //如果链路还在，并且spv已认为reporter处于离线状态，则拆除链路
    
    if(*((uint32_t*)pxRpt->m_aucRptIPAddr) != 0 && pxRpt->m_xRptState == 0)
    {   
               
        vLog(LOG_LEVEL_INFOR, "SPV", "Break down the link of offline reporter @ %d,%d,%d,%d",
             pxRpt->m_aucRptIPAddr[0], pxRpt->m_aucRptIPAddr[1], 
             pxRpt->m_aucRptIPAddr[2], pxRpt->m_aucRptIPAddr[3]);
        
        uint8_t roomID = pxRpt->m_xRptID.m_ucRoomID;
        uint8_t testBenchID = pxRpt->m_xRptID.m_ucTestBenchID;
        
        spvMsg_warn_t *pxSpvMsg_warn = (spvMsg_warn_t *)malloc(sizeof(spvMsg_warn_t));
        pxSpvMsg_warn->m_xRptID.m_ucRoomID =roomID;
        pxSpvMsg_warn->m_xRptID.m_ucTestBenchID = testBenchID;
        
        vSPV_AddRptLinkToList(pxSpvMsg_warn, pxAlarmMsg_list, 0);  //2:上线，0：掉线
        
        //rpt列表置0
        xSemaphoreTake(m_xMutex, portMAX_DELAY); 
        for(int iRptIdx = 0; iRptIdx < MAX_RPT_NUM; iRptIdx++)
        {
            rptID_t* m_rpt = &m_axOnlineSpvRpt[iRptIdx];
            if(m_rpt->m_ucRoomID == roomID && m_rpt->m_ucTestBenchID == testBenchID)
            {
                m_rpt->m_ucRoomID = 0;
                m_rpt->m_ucTestBenchID = 0;
                break;
            }
        }
        xSemaphoreGive(m_xMutex);  
        free(pxSpvMsg_warn);
        return TCP_OPER_ABORT;
    }    
    
    return TCP_OPER_NONE;
}
#endif  // tcp callback