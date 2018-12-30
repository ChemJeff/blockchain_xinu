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
struct ProcInfo send_info;
struct ProcInfo recv_info;
struct ProcInfo contract_info;
pid32 udp_procid;

//不同线程的消息缓冲区
struct Message send_buf;
struct Message recv_buf;
struct Message contract_buf;

//用于进行线程同步的信号量
sid32 send_sem;
sid32 recv_sem;
sid32 contract_sem;

extern struct LocalInfo local_info;
extern struct Log local_log[];

int32 init_semp() {
    // foo
}

process udp_recvp() {
    int i, count = local_info.local_alive_count;
    for (i = 0; i < count; i++){
	udp_register(local_info.local_device_list[i].ipaddr, BLKCHAIN_UDP_PORT, BLKCHAIN_UDP_PORT);//登记arpscan扫描到的每一个主机
    }
    udp_register(IP_BCAST, BLKCHAIN_UDP_PORT, BLKCHAIN_UDP_PORT);//登记广播
    char* message;
    struct Message *msgbuf;
    int retval;
    struct udpentry *udptr;
    while(1){//循环扫描每一个登记，如果有message到达，就按照类型分发给对应进程
	    for (i = 0; i < UDP_SLOTS; i++){
	        udptr = &udptab[i];
	        if (udptr->udstate != UDP_FREE){
		        retval = udp_recv(i, message, MAX_MSG_LEN, WAIT_TIME);
	            if (retval != SYSERR && retval != OK){
		            str2msg(message, retval, msgbuf);
		            if (msgbuf->protocol_type == MSG_DEAL_REQ || msgbuf->protocol_type == MSG_DEAL_SUCC || msgbuf->protocol_type == MSG_DEAL_BDCAST){
			            send(recv_info.procid, message);
		            }
		            else{
			            send(contract_info.procid, message);
		            }
		        }
	        }
	    }
    }
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
