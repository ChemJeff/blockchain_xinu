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

//接收和发送报文共用一个slot
uid32 udp_slot;

//tty设备ID
did32	dev;

extern int32 list_update_time;
extern struct LocalInfo local_info;
extern struct Log local_log[];

status init_sem() { //初始化线程相关的信号量
    sid32 retval;
    retval = semcreate(0);
    if (retval == SYSERR)
        return SYSERR;
    send_sem = retval;
    retval = semcreate(0);
    if (retval == SYSERR)
        return SYSERR;
    recv_sem = retval;
    retval = semcreate(0);
    if (retval == SYSERR)
        return SYSERR;
    contract_sem = retval;
    return OK;
}

process udp_recvp() {
    char udp_message[MAX_MSG_LEN];
    struct Message msgbuf;
    uint32 retval, remoteip;
    int32 udp_len;
    uint16 remoteport;
    while(TRUE){ //使用udp_recvaddr，接收所有发送至本机1024端口的udp包
        //然后根据当前本机几个线程的执行情况进行分发处理或者丢弃
        //首先检查其他线程是不是都退出了(udp线程最后退出)
        if (main_exited == TRUE && recv_info.exited == TRUE && contract_info.exited == TRUE) {
            return 0;
        }
        udp_len = udp_recvaddr(udp_slot, &remoteip, &remoteport, udp_message, MAX_MSG_LEN - 1, UDP_TIMEOUT);
        //返回的udp_len包含'\0'的长度，需要注意，不然容易出现general protection violation
        if (udp_len == SYSERR || udp_len == TIMEOUT) {  //本次没有成功接收到udp包，进入下个循环接收
            continue;
        }
        kprintf("DEBUG: UDP message received from %d.%d.%d.%d : %d, length = %d\n",
            (remoteip >> 24)&0xff, (remoteip >> 16)&0xff, (remoteip >> 8)&0xff, remoteip&0xff,
            (int32)remoteport, udp_len);
        udp_message[udp_len - 1] = '\0';
        retval = str2msg(udp_message, udp_len - 1, &msgbuf); //将收到的字符串转换为协议消息
        if (retval == SYSERR) { //收到的消息错误，进入下个循环接收
            continue;
        }
        //根据协议消息的具体类型以及对方IP，结合本机工作线程的状态，分发或丢弃该协议消息
        switch(msgbuf.protocol_type) {
            case MSG_DEAL_REQ: break;
            case MSG_CONTRACT_REQ: break;
            case MSG_CONTRACT_OFFER: break;
            case MSG_CONTRACT_CONFIRM: break;
            case MSG_DEAL_SUCC: break;
            case MSG_DEAL_BDCAST: break;
            default: continue;
        }
    }
}

process sendp() { //实际的使用方式是作为普通函数进行调用，这里分开写为了逻辑上清晰
    char cmdline[MAX_CMDLINE], strbuf[MAX_STRMSG_LEN];
    int32 cmdlen, strlength;
    struct Message msgbuf;
    uint32 retval;
    while(TRUE) {
        if (clktime - list_update_time > 300) {
            // 需要重新扫描设备列表
            fprintf(dev, "Refresh the device list...\n");
            arp_scan();
        }
        list_device();
        // 读入命令行(可能是一条退出指令，也可能是一条交易指令，或者错误指令(重新输入))
        while(TRUE) { // 直到输入一条正确的指令才继续
            //指令集{'help', 'exit', 直接发送消息}
            len = read(dev, cmdline, MAX_CMDLINE);
            retval = cmd2msg(cmdline, len, &msgbuf);
            if (retval == OK) { //是一条正确的发送指令
                break;
            }
            if (strncmp(cmdline, 'help', 5) == 0) {
                fprintf(dev, "Description:\n");
                fprintf(dev, "\tUsed for blockchain experiment on XINU\n");
                fprintf(dev, "\tRead commands from user and send money out\n");
                fprintf(dev, "Usage:\n");
                fprintf(dev, "\texit: wait all currently executing processes to finish their working cycles, and exit\n");
                fprintf(dev, "\thelp: show this message\n");
                fprintf(dev, "\tor directly input command as 'ip1(dot)_ip2(dot)_amount'\n");                
            }
            else if (strncmp(cmdline, 'exit', 5) == 0) {
                return 0;
            }
            else {
                fprintf(dev, "Usage:\n");
                fprintf(dev, "\texit\n\thelp\n\tor directly input command as 'ip1(dot)_ip2(dot)_amount'\n");
            }
        }
        //这个工作进程是先主动发送udp包，然后定时等待回应
        msg2str(strbuf, MAX_STRMSG_LEN, &msgbuf, &strlength);
        retval = udp_sendto(udp_slot, msgbuf.ipaddr2, BLKCHAIN_UDP_PORT, strbuf, strlength);
        if (retval != OK) { //发送失败，直接进入下一个工作循环
            //这里需要加入日志
            continue;
        }
        send_flag = TRUE;
        fprintf(dev, "In bc_sendp: send MSG_DEAL_REQ \n");

        retval = recvtime(RECV_TIMEOUT*3); //等待udp线程分发协议消息，超时时间的具体倍数考虑流程图步骤数
        if (retval != OK) { //超时或其他错误，直接进入下一个工作循环
            //这里需要加入日志
            continue;
        }
        fprintf(dev, "In bc_sendp: receive MSG_DEAL_SUCC\n");

        msgbuf.protocol_type = MSG_DEAL_BDCAST;
        msg2str(strbuf, MAX_STRMSG_LEN, &msgbuf, &stelength);
        retval = udp_sendto(udp_slot, IP_BCAST, BLKCHAIN_UDP_PORT, strbuf, strlength);
        if (retval != OK) { //发送失败，(重试数次)直接进入下一个工作循环
            //这里需要加入日志
            continue;
        }
        fprintf(dev, "In bc_sendp: broadcast MSG_DEAL_BDCAST\n");
        fprintf(dev, "In bc_sendp: send money success!\n");
        //这里需要加入日志

        send_flag = FALSE;
    }
}

process recvp() {
    uint32 retval;
    while(TRUE) { //每个工作循环从udp线程接收到对应协议消息后唤醒本线程开始
        wait(recv_sem);
        recv_flag = TRUE;



        recv_flag = FALSE;
    }
    recv_info.exited = TRUE; //退出之前需要主动设置这个标记
    return 0;
}

process contractp() {
    uint32 retval;
    while(TRUE) { //每个工作循环从udp线程接收到对应协议消息后唤醒本线程开始
        wait(contract_sem);
        contract_flag = TRUE;

        contract_flag = FALSE;
    }
    contract_info.exited = TRUE; //退出之前需要主动设置这个标记
    return 0;
}

shellcmd xsh_blockchain() {
    // main process
    int32 stime;
    uint32 retval;
    char cmdbuf[128];

    stime = clktime;
    dev = proctab[getpid()].prdesc[0]; //设置当前的输入输出设备

    fprintf(dev, "Initializing...\n");
    fprintf(dev, "Creating worker threads...\n");

    send_info.procid = getpid();
    main_exited = FALSE;
    recv_info.procid = create(recvp, BC_PROC_STK, BC_PROC_PRIO, "BCrecv", 0);
    recv_info.exited = FALSE;
    contract_info.procid = create(contractp, BC_PROC_STK, BC_PROC_PRIO, "BCcontract", 0);
    contract_info.exited = FALSE;
    udp_procid = create(udp_recvp, BC_PROC_STK, BC_PROC_PRIO, "BCudp", 0);

    if (recv_info.procid == SYSERR || contract_info.procid == SYSERR || udp_procid == SYSERR) {
        fprintf(dev, "    Failed, exit...\n");
        kill(recv_info.procid);
        kill(contract_info.procid);
        kill(udp_procid);
        return -1;
    }

    fprintf(dev, "Creating semaphores...\n");

    retval = init_sem();
    if (retval == SYSERR) {
        fprintf(dev, "    Failed, exit...\n");
        return -1;
    }

    fprintf(dev, "Initializing data structures...\n");

    retval = init_local(&local_info);
    udp_slot = udp_register(0, BLKCHAIN_UDP_PORT, BLKCHAIN_UDP_PORT);
    if (retval == SYSERR || udp_slot == SYSERR) {
        fprintf(dev, "    Failed, exit...\n");
        return -1;
    }    

    arp_scan();

    fprintf(dev, "Starting worker threads...\n");

    resume(recv_info.procid);
    resume(contract_info.procid);
    resume(udp_procid);

    fprintf(dev, "All done! time elapsed: %d s\n", clktime - stime);

    sendp();    //逻辑上是线程，实际上还是主线程中的一个调用

    main_exited = TRUE; //主线程退出之前需要自己设置这个标记，否则其他线程无法在完成本轮工作后依照安全的顺序退出
    return 0;
}
