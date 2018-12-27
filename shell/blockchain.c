/*------------------------------------------------------------------------
 * EndGame
 * Name = TangJiajun
 * StudentID = 1500011776
 * StudentNo = 10
 * GroupNo = 5
 *------------------------------------------------------------------------
 */

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
pid32 udp_procid;

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

shellcmd xsh_blockchain() {
    // main process
    int32 list_update_time, stime;
    char cmdbuf[128];
    printf("Initializing...\n");
    printf("Creating worker threads...\n");

    send_info.procid = getpid();
    recv_info.procid = create(recvp, SHELL_CMDSTK, SHELL_CMDPRIO, "BCrecv", 0);
    contract_info.procid = create(contractp, SHELL_CMDSTK, SHELL_CMDPRIO, "BCcontract", 0);
    udp_procid = create(udp_recvp, SHELL_CMDSTK, SHELL_CMDPRIO, "BCudp", 0);

    printf("Creating semaphores...\n");

    printf("Initializing data structures...\n");

    arp_scan();
    list_update_time = clktime;

    printf("Starting worker threads...\n");

    resume(recv_info.procid);
    resume(contract_info.procid);
    resume(udp_procid);

    printf("All done! time elapsed: %d s\n", clktime - stime);

    while(TRUE) {
        if (clktime - list_update_time > 300) {
            // 需要重新扫描设备列表
            printf("Refresh the device list...\n");
            arp_scan();
            list_update_time = clktime;
        }
        list_device();
        // 读入命令行(可能是一条退出指令，也可能是一条交易指令，或者错误指令(重新输入))
        while(TRUE) {

        }

    }
    // foo
}