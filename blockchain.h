#include <xinu.h>

#define MAX_ALIVE 255
#define MAX_LOG 1024

#define BLKCHAIN_UDP_PORT 1024

#define PROC_SEND 1
#define PROC_RECV 2
#define PROC_CONTRACT 3

#define FLAG_SUCC 1
#define FLAG_FAIL 2

#define ROLE_SEND 1
#define ROLE_RECV 2
#define ROLE_CONTRACT 3
#define ROLE_OTHER 4

#define MSG_DEAL_REQ 1
#define MSG_CONTRACT_REQ 2
#define MSG_CONTRACT_OFFER 3
#define MSG_CONTRACT_CONFIRM 4
#define MSG_DEAL_SUCC 5
#define MSG_DEAL_BDCAST 6

#define EPS 1e-6

struct LocalInfo {
    sid32 balance_lock;     //对余额的读写保护
    sid32 list_lock;        //对设备列表的读写保护

    int32 local_ipaddr;
    int32 local_subnet_mask;
    int32 local_alive_count;
    int32 local_balance;
    int32 local_device_list[MAX_ALIVE];
    void init() {
        //foo
    }
};

struct ProcInfo { //保存每个处理过程的相关信息
    int32 ipaddr1;
    int32 ipaddr2;
    double amount; 
    byte last_msg;  // 上一条收到的信息状态
};

struct Message {
    int32 ipaddr1;
    int32 ipaddr2;
    byte protocol_type;
    double amount;
};

struct Log {
    int32 ipaddr1;
    int32 ipaddr2;
    byte flag;          //交易标志，如是否成功
    byte role;          //本机在本次交易中的角色
    double org_amount;  //原始交易中的交易量
    double amount;      //对于本机的收支变化
    double balance;     //此次交易后本机的余额
};

LocalInfo local_info;

int32 str2msg(char* buf, int32 length, Message* msgbuf) {
    //foo
    char* ptr;
    ptr = buf;
    msgbuf->ipaddr1 = *(int32*)ptr;
    ptr += 5;
    msgbuf->ipaddr2 = *(int32*)ptr;
    ptr += 5;
    msgbuf->protocol_type = *(byte*)ptr;
    ptr += 2;
    msgbuf->amount = atof(ptr);
    return OK;
}

int32 msg2log(Message* msg, Log* logbuf) {  //由接收到的消息填写日志项
    //foo
    if (msg->ipaddr2 == local_info.local_ipaddr) {      //交易收到方已经主动保存了记录
        return IGNORE;
    }
    logbuf->ipaddr1 = msg->ipaddr1;
    logbuf->ipaddr2 = msg->ipaddr2;
    logbuf->org_amount = msg->amount;
    logbuf->flag = FLAG_SUCC;
    if (logbuf->ipaddr1 == local_info.local_ipaddr) {   //本机是发送方
        logbuf->role = ROLE_SEND;
    }
    else {  //矿机(合约机)是不会收到广播通知的，因为广播通知是由矿机发出的
        logbuf->role = ROLE_OTHER;
    }

    switch(role) {
        case ROLE_SEND: logbuf->amount = -logbug->org_amount; break;
        case ROLE_OTHER: logbuf->amount = 0; break;
        default: return SYSERR;
    }
    wait(local_info.balance_lock);      //考虑在外层加锁
    logbuf->balance = local_info.balance;
    signal(local_info.balance_lock);

    return OK;
}

int32 arg2log(
    Log* logbuf, int32 ipaddr1, int32 ipaddr2, byte flag, byte role, double org_amount) {
    //主动通过指定参数填写日志项
    logbuf->ipaddr1 = ipaddr1;
    logbuf->ipaddr2 = ipaddr2;
    logbuf->flag = flag;
    logbuf->role = role;
    logbuf->org_amount = org_amount;

    if (flag != FLAG_SUCC) {
        logbuf->amount = 0;
        wait(local_info.balance_lock);      //考虑在外层加锁
        logbuf->balance = local_info.balance;
        signal(local_info.balance_lock);
    }

    switch(role) {
        case ROLE_SEND: logbuf->amount = -org_amount; break;
        case ROLE_RECV: logbuf->amount = org_amount*0.9; break;
        case ROLE_CONTRACT: logbuf->amount = org_amount*0.1; break;
        case ROLE_OTHER: logbuf->amount = 0; break;
        default: return SYSERR;
    }
    wait(local_info.balance_lock);      //考虑在外层加锁
    logbuf->balance = local_info.balance;
    signal(local_info.balance_lock);

    return OK;
}
