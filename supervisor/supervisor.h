#ifndef __SUPERVISOR_H__
#define __SUPERVISOR_H__

#include "ethernet.h"

typedef enum
{
    SPV_CMD_REQ_RPT_ID = 1,
    SPV_CMDR_REQ_RPT_ID = 2,
    SPV_CMD_WARN = 3,
    SPV_CMD_CLEAR_WARN = 4
}spvCmd_e;

#ifndef RPT_ID_T
#define RPT_ID_T
typedef struct rptID_s
{
    uint8_t     m_ucRoomID;
    uint8_t     m_ucTestBenchID;
}rptID_t;
#endif

typedef struct spvMsg_rptID_s
{
    uint32_t        m_ulMsgID;
    
    rptID_t         m_xRptID;
}spvMsg_rptID_t;

typedef struct spvMsg_warn_s
{
    uint32_t        m_ulMsgID;
    
    rptID_t         m_xRptID;
    uint8_t         m_ucWarnCode;
    
    uint8_t         m_ucBufLen;
    uint8_t         m_aucBuf[];
}spvMsg_warn_t;


extern void vTask_Supervisor( void *pvParameters );

extern TCPOper_e xSPV_TCPCbk_EstbConn(struct uip_conn *pxCurConn, void** ppvAppInst);
extern TCPOper_e xSPV_TCPCbk_RcvMsg(uint8_t* pucMsgBuf, unsigned int uiMsgLen, void* pvAppInst);
extern TCPOper_e xSPV_TCPCbk_LostConn(TCPCbkErrCode_e ucErrCode, void* pvAppInst);
extern TCPOper_e xSPV_TCPCbk_Poll(void* pvAppInst);


#endif  // __SUPERVISOR_H__
