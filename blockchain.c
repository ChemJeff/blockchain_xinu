#include <xinu.h>
#include <blockchain.h>

// 当前是否有对应过程正在处理中
bool8 send_flag = FALSE;
bool8 recv_flag = FALSE;
bool8 contract_flag = FALSE;

//保存与对应处理过程相关的信息
ProcInfo send_info;
ProcInfo recv_info;
ProcInfo contract_info;

//不同线程的消息缓冲区
Message send_buf;
Message recv_buf;
Message contract_buf;

//用于进行线程同步的信号量
sid32 send_sem;
sid32 recv_sem;
sid32 contract_sem;

extern LocalInfo local_info;
extern Log local_log[];

int32 init_semp() {
    // foo
}

process udp_recvp() {
    // foo
}

process recvp() {
    // foo
}

process contractp() {
    // foo
}

process scanp() {
    // optional
}

process main() {
    // foo
}