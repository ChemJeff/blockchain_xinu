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
extern sid32 global_print_lock; //为了打印记录的可读性
extern struct LocalInfo local_info;
extern struct Log local_log[];

status init_sem() { //初始化线程相关以及打印相关的信号量
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
    struct Log logbuf;
    uint32 remoteip;
    int32 udp_len, retval;
    uint16 remoteport;
    while(TRUE){ //使用udp_recvaddr，接收所有发送至本机1024端口的udp包
        //然后根据当前本机几个线程的执行情况进行分发处理或者丢弃
        //首先检查其他线程是不是都退出了(udp线程最后退出)
        if (main_exited == TRUE && 
            (recv_info.exited == TRUE || recv_flag == FALSE) &&
            (contract_info.exited == TRUE || contract_flag == FALSE)) {
            if (recv_flag == FALSE)
                kill(recv_info.procid);
            if (contract_flag == FALSE)
                kill(contract_info.procid);
            return 0;
        }
        //kprintf("DEUBG: UDP Checkpoint #1\n");
        udp_len = udp_recvaddr(udp_slot, &remoteip, &remoteport, udp_message, MAX_MSG_LEN - 1, UDP_TIMEOUT);
        //kprintf("DEUBG: UDP Checkpoint #2\n");
        //返回的udp_len包含'\0'的长度，需要注意，不然容易出现general protection violation
        //BUG: 这里会接受到本地发送出去的包，且字节序存在问题
        if (udp_len == SYSERR || udp_len == TIMEOUT) {  //本次没有成功接收到udp包，进入下个循环接收
            continue;
        }
        // kprintf("DEUBG: UDP Checkpoint #3\n");

        /* IP network prefix in dotted decimal & hex */

        // uint32 ipprefix = NetData.ipprefix;
        // char	str[40];		/* Temporary used for formatting*/
        // sprintf(str, "%d.%d.%d.%d",
        //     (ipprefix>>24)&0xff, (ipprefix>>16)&0xff,
        //     (ipprefix>>8)&0xff,        ipprefix&0xff);
        // printf("   %-16s  %-16s  0x%08x\n",
        //     "IP prefix:", str, ipprefix);

        /* IP network mask in dotted decimal & hex */

        // uint32 ipmask = NetData.ipmask;
        // uint32 ipaddr = NetData.ipucast;
        // sprintf(str, "%d.%d.%d.%d",
        //     (ipmask>>24)&0xff, (ipmask>>16)&0xff,
        //     (ipmask>>8)&0xff,        ipmask&0xff);
        // printf("   %-16s  %-16s  0x%08x\n",
        //     "Address mask:", str, ipmask);

        // sprintf(str, "%d.%d.%d.%d",
        //     (remoteip>>24)&0xff, (remoteip>>16)&0xff,
        //     (remoteip>>8)&0xff,        remoteip&0xff);
        // printf("   %-16s  %-16s  0x%08x\n",
        //     "Remote IP:", str, remoteip);

        if ((remoteip&NetData.ipmask) != NetData.ipprefix) { //发出UDP包的地址不在子网内，也可能是字节序的问题，丢弃
            remoteip = ntohl(remoteip);
            remoteport = ntohs(remoteport);
            // kprintf("DEUBG: UDP Checkpoint #3.5\n");
            if ((remoteip&NetData.ipmask) != NetData.ipprefix) { //换一个字节序尝试
                kprintf("In bc_udprecv: UDP packet from outer network, filtered\n");
                continue;
            }
        }
        //kprintf("DEUBG: UDP Checkpoint #4\n");
        if (remoteip == local_info.local_ipaddr) { //发出UDP包的地址为本机，过滤
            kprintf("In bc_udprecv: UDP packet from local host, filtered\n");
            continue;
        }
        // 如果本机不能同时作为矿机部分代码约束注释掉了，这里对应也要注释掉
        // kprintf("DEUBG: UDP Checkpoint #5\n");
        kprintf("In bc_udprecv: UDP message received from %d.%d.%d.%d : %d, length = %d\n",
            (remoteip >> 24)&0xff,
            (remoteip >> 16)&0xff,
            (remoteip >> 8)&0xff,
            remoteip&0xff,
            (int32)remoteport, udp_len);
        // kprintf("DEUBG: UDP Checkpoint #6\n");
        udp_message[udp_len - 1] = '\0';
        // kprintf("DEUBG: UDP Checkpoint #7\n");
        kprintf("\tmessage received: %s\n", udp_message);
        // kprintf("DEUBG: UDP Checkpoint #8\n");
        retval = str2msg(udp_message, udp_len - 1, &msgbuf); //将收到的字符串转换为协议消息
        // kprintf("DEUBG: UDP Checkpoint #9\n");
        if (retval == SYSERR) { //收到的消息错误，进入下个循环接收
            kprintf("In bc_udprecv: discard incorrect UDP packet\n");
            continue;
        }
        //kprintf("DEUBG: UDP Checkpoint #10\n");
        //根据协议消息的具体类型以及对方IP，结合本机工作线程的状态，分发或丢弃该协议消息
        switch(msgbuf.protocol_type) {
            case MSG_DEAL_REQ: { //本机是作为交易收到方
            // kprintf("DEUBG: UDP Checkpoint #11\n");
                if (recv_flag == TRUE) { //当前已经有一个recv过程在处理
                    kprintf("In bc_udprecv: discard late MSG_DEAL_REQ\n");
                    continue;
                }
                // kprintf("DEUBG: UDP Checkpoint #12\n");
                recv_buf = msgbuf;
                recv_buf.senderip = remoteip; //需要手动设置
                // kprintf("DEUBG: UDP Checkpoint #13\n");
                kprintf("In bc_udprecv: start a new receiving transaction\n");
                signal(recv_sem); //通知等待的recv进程有新的请求
                // kprintf("DEUBG: UDP Checkpoint #14\n");
                break;
            }
            case MSG_CONTRACT_REQ: { //本机作为候选矿机
            // kprintf("DEUBG: UDP Checkpoint #15\n");
                if (contract_flag == TRUE) { //当前已经有一个contract过程在处理
                    kprintf("In bc_udprecv: discard late MSG_CONTRACT_REQ\n");
                    continue;
                }
                if (msgbuf.ipaddr1 == local_info.local_ipaddr ||
                    msgbuf.ipaddr2 == local_info.local_ipaddr) { //本机不能作为矿机处理本机相关的交易
                    kprintf("In bc_udprecv: ignore MSG_CONTRACT_REQ as sender or receiver\n");
                    continue;
                }
                // kprintf("DEUBG: UDP Checkpoint #16\n");
                contract_buf = msgbuf;
                contract_buf.senderip = remoteip;
                // kprintf("DEUBG: UDP Checkpoint #17\n");
                kprintf("In bc_udprecv: start a new contract transaction\n");
                signal(contract_sem);
                // kprintf("DEUBG: UDP Checkpoint #18\n");
                break;
            }
            case MSG_CONTRACT_OFFER: { //本机作为交易收到方
            // kprintf("DEUBG: UDP Checkpoint #19\n");
                if (recv_flag != TRUE || 
                    recv_info.last_protocol != MSG_CONTRACT_REQ ||
                    recv_info.ipaddr1 != msgbuf.ipaddr1 ||
                    recv_info.ipaddr2 != msgbuf.ipaddr2 ||
                    recv_info.amount != msgbuf.amount) {
                    kprintf("In bc_udprecv: discard wrong MSG_CONTRACT_OFFER\n");
                    continue;
                }
                // kprintf("DEUBG: UDP Checkpoint #20\n");
                recv_buf = msgbuf;
                recv_buf.senderip = remoteip;
                // kprintf("DEUBG: UDP Checkpoint #21\n");
                send(recv_info.procid, OK);
                // kprintf("DEUBG: UDP Checkpoint #22\n");
                break;
            }
            case MSG_CONTRACT_CONFIRM: { //本机作为被选中的矿机
            // kprintf("DEUBG: UDP Checkpoint #23\n");
                if (contract_flag != TRUE ||
                    contract_info.last_protocol != MSG_CONTRACT_OFFER ||
                    contract_info.ipaddr1 != msgbuf.ipaddr1 ||
                    contract_info.ipaddr2 != msgbuf.ipaddr2 ||
                    contract_info.amount != msgbuf.amount) {
                    kprintf("In bc_udprecv: discard wrong MSG_CONTRACT_CONFIRM\n");
                    continue;
                }
                // kprintf("DEUBG: UDP Checkpoint #24\n");
                contract_buf = msgbuf;
                contract_buf.senderip = remoteip;
                // kprintf("DEUBG: UDP Checkpoint #25\n");
                send(contract_info.procid, OK);
                // kprintf("DEUBG: UDP Checkpoint #26\n");
                break;
            }
            case MSG_DEAL_SUCC: { //本机作为交易发起方
            // kprintf("DEUBG: UDP Checkpoint #27\n");
                if (send_flag != TRUE ||
                    send_info.last_protocol != MSG_DEAL_REQ ||
                    send_info.ipaddr1 != msgbuf.ipaddr1 ||
                    send_info.ipaddr2 != msgbuf.ipaddr2 ||
                    send_info.amount != msgbuf.amount) {
                    kprintf("In bc_udprecv: discard wrong MSG_DEAL_SUCC\n");
                    continue;
                }
                // kprintf("DEUBG: UDP Checkpoint #28\n");
                send_buf = msgbuf;
                send_buf.senderip = remoteip;
                // kprintf("DEUBG: UDP Checkpoint #29\n");
                send(send_info.procid, OK);
                // kprintf("DEUBG: UDP Checkpoint #30\n");
                break;
            }
            case MSG_DEAL_BDCAST: { //需要结合具体情况区分本机的角色(复杂)
            // kprintf("DEUBG: UDP Checkpoint #31\n");
                if (msgbuf.ipaddr1 == local_info.local_ipaddr ||
                    msgbuf.ipaddr2 == local_info.local_ipaddr) { 
                    //本机是发送方或者接受方，记录日志的工作在工作线程中已经处理
                    kprintf("In bc_udprecv: ignore unnecessary MSG_DEAL_BDCAST\n");
                    continue;
                }
                byte found_flag=FALSE, slot_flag=FALSE;
                int32 ii;
                msg2log(&msgbuf, &logbuf);
                for (ii = 0; ii < MAX_LOG_BUF; ii++) {
                    if (local_log_buf[ii].valid == FALSE)
                        continue;
                    if (log_equal(&local_log_buf[ii].log, &logbuf) == TRUE) { //集齐两条消息
                        local_log_buf[ii].valid = FALSE;
                        wait(local_info.log_lock);
                        local_log[local_info.local_log_count++] = local_log_buf[ii].log;
                        signal(local_info.log_lock);
                        found_flag = TRUE;
                        break;
                    }
                }
                if (found_flag == TRUE) {
                    continue;
                }
                //存入一条新的log_buf
                for (ii =0; ii < MAX_LOG_BUF; ii++) {
                    if (local_log_buf[ii].valid == TRUE)
                        continue;
                    local_log_buf[ii].valid = TRUE;
                    local_log_buf[ii].log = logbuf;
                    slot_flag = TRUE;
                    break;
                }
                if (slot_flag == FALSE) {
                    kprintf("In bc_udprecv: no buffer for new log, may lose some infomation\n");
                }
                //这里需要日志记录
                //注意，如果是矿机那么会接收到一次消息
                //如果是吃瓜机器会接受到两次消息
                //因此在矿机的工作线程中需要自己提前记录一次消息
                //在log_buf里面的消息需要成对配对了才能被正式记录到log中
                // kprintf("DEUBG: UDP Checkpoint #32\n");
                break;
            }
            default: continue;
        }
    }
}

process sendp() { //实际的使用方式是作为普通函数进行调用，这里分开写为了逻辑上清晰
    char cmdline[MAX_CMDLINE], strbuf[MAX_STRMSG_LEN];
    int32 cmdlen, strlength, retval;
    struct Message msgbuf;
    struct Log logbuf;
    byte refresh_flag;
    while(TRUE) {
        if (clktime - list_update_time > BC_SCAN_INTERVAL) {
            // 需要重新扫描设备列表
            fprintf(dev, "Refresh the device list...\n");
            arp_scan();
        }
        list_device();
        show_balance(dev);
        refresh_flag = FALSE;

        // 读入命令行(可能是一条退出指令，也可能是一条交易指令，或者错误指令(重新输入))
        while(TRUE) { // 直到输入一条正确的指令才继续
            //指令集{'help', 'exit', 直接发送消息}
            //给某个IP地址一定数量的金额的指令格式为'IP2(dot)_amount'
            //IP1字段和协议类型字段都是固定值填入，可以简化输入
            cmdlen = read(dev, cmdline, MAX_CMDLINE);
            cmdline[cmdlen - 1] = '\0'; //必须手动添加结束符
            kprintf("DEBUG: ECHO: %s, length = %d\n", cmdline, cmdlen);
            retval = cmd2msg(cmdline, cmdlen - 1, &msgbuf);
            //kprintf("DEBUG: checkpoint #11\n");
            if (retval == OK) { //是一条正确的发送指令
                break;
            }
            //kprintf("DEBUG: checkpoint #12\n");
            if (strncmp(cmdline, "help", 5) == 0) {
                //kprintf("DEBUG: checkpoint #13\n");
                fprintf(dev, "Description:\n");
                fprintf(dev, "\tUsed for blockchain experiment on XINU\n");
                fprintf(dev, "\tRead commands from user and send money out\n");
                fprintf(dev, "Usage:\n");
                fprintf(dev, "\texit: wait all currently executing processes to finish their working cycles, and exit\n");
                fprintf(dev, "\trefresh: refresh the local balance\n");
                fprintf(dev, "\tlog: show local logs\n");
                fprintf(dev, "\thelp: show this message\n");
                fprintf(dev, "\tor directly input command as 'ip2(dot)_amount'\n");                
            }
            else if (strncmp(cmdline, "log", 4) == 0) {
                list_log();
            }
            else if (strncmp(cmdline, "exit", 5) == 0) {
                //kprintf("DEBUG: checkpoint #14\n");
                udp_release(udp_slot);
                return 0;
            }
            else if (strncmp(cmdline, "refresh", 8) == 0) {
                //kprintf("DEBUG: checkpoint #15\n");
                refresh_flag = TRUE;
                break;
            }
            else {
                //kprintf("DEBUG: checkpoint #16\n");
                fprintf(dev, "In bc_sendp: invalid commandline input\n");
                fprintf(dev, "Usage:\n");
                fprintf(dev, "\texit\n\trefresh\n\tlog\n\thelp\n\tor directly input command as 'ip2(dot)_amount'\n");
            }
        }

        if (refresh_flag == TRUE)
            continue;
        
        //这个工作进程是先主动发送udp包，然后定时等待回应
        fprintf(dev, "In bc_sendp: start a new sending transaction\n");
        retval = expend_balance(msgbuf.amount);
        if (retval != OK) { //余额不足
            fprintf(dev, "In bc_sendp: Insuficient money, payment failed\n");
            continue;
        }

        msg2str(strbuf, MAX_STRMSG_LEN, &msgbuf, &strlength);
        retval = udp_sendto(udp_slot, msgbuf.ipaddr2, BLKCHAIN_UDP_PORT, strbuf, strlength);
        if (retval != OK) { //发送失败，直接进入下一个工作循环
            arg2log(&logbuf, msgbuf.ipaddr1, msgbuf.ipaddr2, FLAG_FAIL, ROLE_SEND, msgbuf.amount, TRUE);
            income_balance(msgbuf.amount); //钱没有被花出去，补上
            continue;
        }
        send_info.ipaddr1 = msgbuf.ipaddr1;    //IP地址1为本机IP
        send_info.ipaddr2 = msgbuf.ipaddr2;
        send_info.amount = msgbuf.amount;
        send_info.last_protocol = msgbuf.protocol_type;
        send_flag = TRUE;
        fprintf(dev, "In bc_sendp: send MSG_DEAL_REQ \n");
        fprintf(dev, "\tmessage sent: %s, length = %d\n", strbuf, strlength);

        retval = recvtime(RECV_TIMEOUT*3); //等待udp线程分发协议消息，超时时间的具体倍数考虑流程图步骤数
        if (retval != OK || send_buf.protocol_type != MSG_DEAL_SUCC) { //超时或其他错误，直接进入下一个工作循环
            arg2log(&logbuf, send_info.ipaddr1, send_info.ipaddr2, FLAG_FAIL, ROLE_SEND, send_info.amount, TRUE);
            send_flag = FALSE;
            income_balance(send_info.amount); //钱没有被花出去，补上
            fprintf(dev, "In bc_sendp: timeout for receiving MSG_DEAL_SUCC, payment failed\n");
            continue;
        }
        send_info.last_protocol = send_buf.protocol_type;
        fprintf(dev, "In bc_sendp: receive MSG_DEAL_SUCC\n");

        msgbuf.protocol_type = MSG_DEAL_BDCAST;
        msg2str(strbuf, MAX_STRMSG_LEN, &msgbuf, &strlength);
        retval = udp_sendto(udp_slot, IP_BCAST, BLKCHAIN_UDP_PORT, strbuf, strlength);
        fprintf(dev, "In bc_sendp: send money success!\n");
        fprintf(dev, "\tmoney expended: %d\n", send_info.amount);
        if (retval != OK) { //广播通知发送失败，(重试数次)直接进入下一个工作循环
            fprintf(dev, "In bc_sendp: broadcast MSG_DEAL_BDCAST failed\n");
            fprintf(dev, "\tlogs on other hosts may be influenced\n");
            send_flag = FALSE;
            continue;
        }
        send_info.last_protocol = msgbuf.protocol_type;
        fprintf(dev, "In bc_sendp: broadcast MSG_DEAL_BDCAST\n");
        fprintf(dev, "\tmessage sent: %s, length = %d\n", strbuf, strlength);
        arg2log(&logbuf, send_info.ipaddr1, send_info.ipaddr2, FLAG_SUCC, ROLE_SEND, send_info.amount, TRUE);

        send_flag = FALSE;
    }
}

process recvp() {
    char strbuf[MAX_STRMSG_LEN];
    int32 strlength, retval;
    struct Message msgbuf;
    struct Log logbuf;
    while(TRUE) { //每个工作循环从udp线程接收到对应协议消息后唤醒本线程开始
        if (main_exited == TRUE) //首先检查主线程是否已经退出
            break;

        wait(recv_sem);
        recv_flag = TRUE;
        recv_info.ipaddr1 = recv_buf.ipaddr1;
        recv_info.ipaddr2 = recv_buf.ipaddr2;
        recv_info.amount = recv_buf.amount;
        recv_info.last_protocol = recv_buf.protocol_type;
        fprintf(dev, "In bc_recvp: receive MSG_DEAL_REQ \n");

        msgbuf.ipaddr1 = recv_info.ipaddr1;
        msgbuf.ipaddr2 = recv_info.ipaddr2;
        msgbuf.protocol_type = MSG_CONTRACT_REQ;
        msgbuf.amount = recv_info.amount;
        msg2str(strbuf, MAX_STRMSG_LEN, &msgbuf, &strlength);
        retval = udp_sendto(udp_slot, IP_BCAST, BLKCHAIN_UDP_PORT, strbuf, strlength);
        if (retval != OK) { //广播通知发送失败，(重试数次)直接进入下一个工作循环
            arg2log(&logbuf, recv_info.ipaddr1, recv_info.ipaddr2, FLAG_FAIL, ROLE_RECV, recv_info.amount, TRUE);
            recv_flag = FALSE;
            continue;
        }
        recv_info.last_protocol = msgbuf.protocol_type;
        fprintf(dev, "In bc_recvp: broadcast MSG_CONTRACT_REQ\n");
        fprintf(dev, "\tmessage sent: %s, length = %d\n", strbuf, strlength);

        retval = recvtime(RECV_TIMEOUT); //等待udp线程分发协议消息，超时时间的具体倍数考虑流程图步骤数
        if (retval != OK || recv_info.protocol_type != MSG_CONTRACT_OFFER) { //超时或其他错误，直接进入下一个工作循环
            arg2log(&logbuf, recv_info.ipaddr1, recv_info.ipaddr2, FLAG_FAIL, ROLE_RECV, recv_info.amount, TRUE);
            recv_flag = FALSE;
            continue;
        }
        //recv_info.senderip = msgbuf.senderip;  //BUG Found!
        recv_info.senderip = recv_buf.senderip;
        recv_info.last_protocol = recv_buf.protocol_type;
        fprintf(dev, "In bc_recvp: receive MSG_CONTRACT_OFFER\n");

        msgbuf.protocol_type = MSG_CONTRACT_CONFIRM;
        msg2str(strbuf, MAX_STRMSG_LEN, &msgbuf, &strlength);
        retval = udp_sendto(udp_slot, recv_info.senderip, BLKCHAIN_UDP_PORT, strbuf, strlength);
        if (retval != OK) { //发送失败，直接进入下一个工作循环
            arg2log(&logbuf, recv_info.ipaddr1, recv_info.ipaddr2, FLAG_FAIL, ROLE_RECV, recv_info.amount, TRUE);
            recv_flag = FALSE;
            continue;
        }
        recv_info.last_protocol = msgbuf.protocol_type;
        income_balance((recv_info.amount/10)*9);   //确认成功以后才加钱，扣除手续费
        fprintf(dev, "In bc_recvp: send MSG_CONTRACT_CONFIRM\n");
        fprintf(dev, "\tmessage sent: %s, length = %d\n", strbuf, strlength);
        fprintf(dev, "In bc_recvp: receive money success!\n");
        
        arg2log(&logbuf, recv_info.ipaddr1, recv_info.ipaddr2, FLAG_SUCC, ROLE_RECV, recv_info.amount, TRUE);
        recv_flag = FALSE;
    }
    recv_info.exited = TRUE; //退出之前需要主动设置这个标记
    return 0;
}

process contractp() {
    char strbuf[MAX_STRMSG_LEN];
    int32 strlength, retval;
    struct Message msgbuf;
    struct Log logbuf;
    while(TRUE) { //每个工作循环从udp线程接收到对应协议消息后唤醒本线程开始
        if (main_exited == TRUE) //首先检查主线程是否已经退出
            break;

        wait(contract_sem);
        contract_flag = TRUE;
        contract_info.ipaddr1 = contract_buf.ipaddr1;
        contract_info.ipaddr2 = contract_buf.ipaddr2;
        contract_info.amount = contract_buf.amount;
        contract_info.last_protocol = contract_buf.protocol_type;
        fprintf(dev, "In bc_contractp: receive MSG_CONTRACT_REQ\n");

        msgbuf.ipaddr1 = contract_info.ipaddr1;
        msgbuf.ipaddr2 = contract_info.ipaddr2;
        msgbuf.protocol_type = MSG_CONTRACT_OFFER;
        msgbuf.amount = contract_info.amount;
        msg2str(strbuf, MAX_STRMSG_LEN, &msgbuf, &strlength);
        retval = udp_sendto(udp_slot, msgbuf.ipaddr2, BLKCHAIN_UDP_PORT, strbuf, strlength);
        if (retval != OK) { //发送失败，直接进入下一个工作循环
            arg2log(&logbuf, contract_info.ipaddr1, contract_info.ipaddr2, FLAG_FAIL, ROLE_CONTRACT, contract_info.amount, TRUE);
            contract_flag = FALSE;
            continue;
        }
        contract_info.last_protocol = msgbuf.protocol_type;
        fprintf(dev, "In bc_contractp: send MSG_CONTRACT_OFFER\n");
        fprintf(dev, "\tmessage sent: %s, length = %d\n", strbuf, strlength);

        retval = recvtime(RECV_TIMEOUT); //等待udp线程分发协议消息，超时时间的具体倍数考虑流程图步骤数
        if (retval != OK || contract_buf.protocol_type != MSG_CONTRACT_CONFIRM) { //超时或其他错误，直接进入下一个工作循环
            arg2log(&logbuf, contract_info.ipaddr1, contract_info.ipaddr2, FLAG_FAIL, ROLE_CONTRACT, contract_info.amount, TRUE);
            contract_flag = FALSE;
            continue;
        }
        contract_info.last_protocol = contract_buf.protocol_type;
        fprintf(dev, "In bc_contractp: receive MSG_CONTRACT_CONFIRM\n");       

        msgbuf.protocol_type = MSG_DEAL_SUCC;
        msg2str(strbuf, MAX_STRMSG_LEN, &msgbuf, &strlength);
        retval = udp_sendto(udp_slot, msgbuf.ipaddr1, BLKCHAIN_UDP_PORT, strbuf, strlength);
        if (retval != OK) { //发送失败，直接进入下一个工作循环
            arg2log(&logbuf, contract_info.ipaddr1, contract_info.ipaddr2, FLAG_FAIL, ROLE_CONTRACT, contract_info.amount, TRUE);
            contract_flag = FALSE;
            continue;
        }
        contract_info.last_protocol = msgbuf.protocol_type;
        fprintf(dev, "In bc_contractp: send MSG_DEAL_SUCC\n");
        fprintf(dev, "\tmessage sent: %s, length = %d\n", strbuf, strlength);
        income_balance((contract_info.amount/10)*1);   //通知成功以后才加钱，收取手续费

        msgbuf.protocol_type = MSG_DEAL_BDCAST;
        msg2str(strbuf, MAX_STRMSG_LEN, &msgbuf, &strlength);
        retval = udp_sendto(udp_slot, IP_BCAST, BLKCHAIN_UDP_PORT, strbuf, strlength);
        if (retval != OK) { //广播发送失败，直接进入下一个工作循环
            fprintf(dev, "In bc_contractp: broadcast MSG_DEAL_BDCAST failed\n");
            fprintf(dev, "\tlogs on other hosts may be influenced\n");
            arg2log(&logbuf, contract_info.ipaddr1, contract_info.ipaddr2, FLAG_SUCC, ROLE_CONTRACT, contract_info.amount, TRUE);
            contract_flag = FALSE;
            continue;
        }
        contract_info.last_protocol = msgbuf.protocol_type;
        fprintf(dev, "In bc_contractp: broadcast MSG_DEAL_BDCAST\n");
        fprintf(dev, "\tmessage sent: %s, length = %d\n", strbuf, strlength);
        fprintf(dev, "In bc_contractp: make contract success!\n");                

        byte found_flag=FALSE, slot_flag=FALSE;
        int32 ii;
        arg2log(&logbuf, contract_info.ipaddr1, contract_info.ipaddr2, FLAG_SUCC, ROLE_CONTRACT, contract_info.amount, FALSE);
        for (ii = 0; ii < MAX_LOG_BUF; ii++) {
            if (local_log_buf[ii].valid == FALSE)
                continue;
            if (log_equal(&local_log_buf[ii].log, &logbuf) == TRUE) { //集齐两条消息
                local_log_buf[ii].valid = FALSE;
                wait(local_info.log_lock);
                local_log[local_info.local_log_count++] = logbuf;
                signal(local_info.log_lock);
                found_flag = TRUE;
                break;
            }
        }
        if (found_flag == TRUE) {
            contract_flag = FALSE;
            continue;
        }
        //给自己的log_buf添加一条记录
        for (ii =0; ii < MAX_LOG_BUF; ii++) {
            if (local_log_buf[ii].valid == TRUE)
                continue;
            local_log_buf[ii].valid = TRUE;
            local_log_buf[ii].log = logbuf;
            slot_flag = TRUE;
            break;
        }
        if (slot_flag == FALSE) {
            kprintf("In bc_contractp: no buffer for new log, may lose some infomation\n");
        }

        contract_flag = FALSE;
    }
    contract_info.exited = TRUE; //退出之前需要主动设置这个标记
    return 0;
}

shellcmd xsh_blockchain() {
    // main process
    int32 stime, retval;

    stime = clktime;
    dev = proctab[getpid()].prdesc[0]; //设置当前的输入输出设备
    global_print_lock = semcreate(1);   //屏幕输出有序化

    fprintf(dev, "In main: Initializing...\n");
    fprintf(dev, "In main: Creating worker threads...\n");

    send_info.procid = getpid();
    main_exited = FALSE;
    recv_info.procid = create(recvp, BC_PROC_STK, BC_PROC_PRIO, "BCrecv", 0);
    recv_info.exited = FALSE;
    contract_info.procid = create(contractp, BC_PROC_STK, BC_PROC_PRIO, "BCcontract", 0);
    contract_info.exited = FALSE;
    udp_procid = create(udp_recvp, BC_PROC_STK, BC_PROC_PRIO, "BCudp", 0);

    if (recv_info.procid == SYSERR || contract_info.procid == SYSERR || udp_procid == SYSERR) {
        fprintf(dev, "\tFailed, exit...\n");
        kill(recv_info.procid);
        kill(contract_info.procid);
        kill(udp_procid);
        return -1;
    }

    fprintf(dev, "In main: Creating semaphores...\n");

    retval = init_sem();
    if (retval == SYSERR) {
        fprintf(dev, "\tFailed, exit...\n");
        return -1;
    }

    fprintf(dev, "In main: Initializing data structures...\n");

    retval = init_local(&local_info);
    udp_slot = udp_register(0, BLKCHAIN_UDP_PORT, BLKCHAIN_UDP_PORT);
    if (retval == SYSERR || udp_slot == SYSERR) {
        kill(recv_info.procid);
        kill(contract_info.procid);
        kill(udp_procid);
        fprintf(dev, "\tFailed, exit...\n");
        return -1;
    }    

    init_logbuf();
    arp_scan();

    fprintf(dev, "In main: Starting worker threads...\n");

    resume(recv_info.procid);
    resume(contract_info.procid);
    resume(udp_procid);

    fprintf(dev, "In main: All done! time elapsed: %d s\n", clktime - stime);

    sendp();    //逻辑上是线程，实际上还是主线程中的一个调用

    fprintf(dev, "In main: main thread exited\n");
    fprintf(dev, "\tother threads will exit once transaction finished\n");
    main_exited = TRUE; //主线程退出之前需要自己设置这个标记，否则其他线程无法在完成本轮工作后依照安全的顺序退出
    return 0;
}
