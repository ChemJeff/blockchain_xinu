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
byte main_exited;   //主线程是否已经退出

//不同线程的消息缓冲区
struct Message send_buf;
struct Message recv_buf;
struct Message contract_buf;

//用于进行线程同步的信号量
sid32 send_sem;
sid32 recv_sem;
sid32 contract_sem;

extern int32 list_update_time;
extern struct LocalInfo local_info;
extern struct Log local_log[];

status init_sem() { //初始化线程相关的信号量
    sid32 retval;
    retval = semcreate(1);
    if (retval == SYSERR)
        return SYSERR;
    send_sem = retval;
    retval = semcreate(1);
    if (retval == SYSERR)
        return SYSERR;
    recv_sem = retval;
    retval = semcreate(1);
    if (retval == SYSERR)
        return SYSERR;
    contract_sem = retval;
    return OK;
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

process sendp() { //实际的使用方式是作为普通函数进行调用，这里分开写为了逻辑上清晰
    while(TRUE) {
        if (clktime - list_update_time > 300) {
            // 需要重新扫描设备列表
            printf("Refresh the device list...\n");
            arp_scan();
        }
        list_device();
        // 读入命令行(可能是一条退出指令，也可能是一条交易指令，或者错误指令(重新输入))
        while(TRUE) {

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
    int32 stime;
    uint32 retval;
    char cmdbuf[128];

    stime = clktime;
    printf("Initializing...\n");
    printf("Creating worker threads...\n");

    send_info.procid = getpid();
    main_exited = FALSE;
    recv_info.procid = create(recvp, SHELL_CMDSTK, SHELL_CMDPRIO, "BCrecv", 0);
    recv_info.exited = FALSE;
    contract_info.procid = create(contractp, SHELL_CMDSTK, SHELL_CMDPRIO, "BCcontract", 0);
    contract_info.exited = FALSE;
    udp_procid = create(udp_recvp, SHELL_CMDSTK, SHELL_CMDPRIO, "BCudp", 0);

    if (recv_info.procid == SYSERR || contract_info.procid == SYSERR || udp_procid == SYSERR) {
        printf("    Failed, exit...\n");
        kill(recv_info.procid);
        kill(contract_info.procid);
        kill(udp_procid);
        return -1;
    }

    printf("Creating semaphores...\n");

    retval = init_sem();
    if (retval == SYSERR) {
        printf("    Failed, exit...\n");
        return -1;
    }

    printf("Initializing data structures...\n");

    retval = init_local(&local_info);
    if (retval == SYSERR) {
        printf("    Failed, exit...\n");
        return -1;
    }    

    arp_scan();
    list_update_time = clktime;

    printf("Starting worker threads...\n");

    resume(recv_info.procid);
    resume(contract_info.procid);
    resume(udp_procid);

    printf("All done! time elapsed: %d s\n", clktime - stime);

    sendp();    //逻辑上是线程，实际上还是主线程中的一个调用

    main_exited = TRUE; //主线程退出之前需要自己设置这个标记，否则其他线程无法在完成本轮工作后依照安全的顺序退出
    return 0;
}
