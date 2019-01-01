/*------------------------------------------------------------------------
 * EndGame
 * Name = TangJiajun
 * StudentID = 1500011776
 * StudentNo = 10
 * GroupNo = 5
 *------------------------------------------------------------------------
 */

#define MAX_ALIVE 255
#define MAX_LOG 1024
#define MAX_LOG_BUF 64
#define MAX_MSG_LEN 1500 // MTU 1500
#define MAX_STRMSG_LEN 64
#define MAX_CMDLINE 128

// test commit

#define BLKCHAIN_UDP_PORT 1024
#define BC_PROC_STK 32768
#define BC_PROC_PRIO 20

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

#define UDP_TIMEOUT 50  /* in miliseconds */
#define RECV_TIMEOUT 1000   /* in milliseconds */
#define BC_SCAN_INTERVAL 300 /* in seconds */

#define IGNORE 0xffff

#define EPS 1e-6

#define kprintf(...) \
{ \
    wait(global_print_lock); \
    kprintf(__VA_ARGS__); \
    signal(global_print_lock); \
}

#define fprintf(...) \
{ \
    wait(global_print_lock); \
    fprintf(__VA_ARGS__); \
    signal(global_print_lock); \
}

#define printf(...) \
{ \
    wait(global_print_lock); \
    printf(__VA_ARGS__); \
    signal(global_print_lock); \
}

sid32 global_print_lock;    //定义在最前面防止错误
char* flag_str[] = {"", "SUCC", "FAIL"};
char* role_str[] = {"", "SEND", "RECV", "CONTRACT", "OTHER"};

struct list_t{
    uint32 ipaddr;
    byte   mac[ETH_ADDR_LEN];
};

struct LocalInfo{
    sid32 balance_lock;     //对余额的读写保护
    sid32 list_lock;        //对设备列表的读写保护
    sid32 log_lock;         //对log的互斥写保护

    uint32 local_ipaddr;
    uint32 local_subnet_mask;
    int32 local_alive_count;
    int32 local_balance;
    int32 local_log_count;
    struct list_t local_device_list[MAX_ALIVE];

};

status init_local(struct LocalInfo* ptrlocal) {
    sid32 retval;

    retval = semcreate(1);
    if (retval == SYSERR)  { //分配信号量失败
        printf("\tFailed to create a semaphore\n");
        return SYSERR;
    }
    ptrlocal->balance_lock = retval;
    if (retval == SYSERR)  { //分配信号量失败
        printf("\tFailed to create a semaphore\n");
        return SYSERR;
    }
    ptrlocal->list_lock = retval;
    retval = semcreate(1);
    if (retval == SYSERR)  { //分配信号量失败
        printf("\tFailed to create a semaphore\n");
        return SYSERR;
    }
    ptrlocal->log_lock = retval;

    ptrlocal->local_alive_count = 0;
    ptrlocal->local_log_count = 0;
    ptrlocal->local_ipaddr = NetData.ipucast;   //本机IP地址
    ptrlocal->local_subnet_mask = NetData.ipmask;
    ptrlocal->local_balance = 100; //初始钱数为100

    return OK;
}

struct ProcInfo{ //保存每个处理过程的相关信息
    pid32 procid;
    uint32 ipaddr1;
    uint32 ipaddr2;
    uint32 senderip;        //只有需要确认矿机ip地址时才需要
    int32 amount;          // 当前正在处理的交易量
    int32 last_protocol;  // 上一条收到的信息的协议类型
    byte exited;          // 本线程是否已经退出(udp线程应该最后退出)
};

struct Message{
    uint32 ipaddr1;
    uint32 ipaddr2;
    int32 protocol_type;
    int32 amount;
    uint32 senderip;    //只有需要确认矿机ip地址时才需要
};

struct Log{
    uint32 ipaddr1;
    uint32 ipaddr2;
    byte flag;          //交易标志，如是否成功
    byte role;          //本机在本次交易中的角色
    int32 org_amount;  //原始交易中的交易量
    int32 amount;      //对于本机的收支变化
    int32 balance;     //此次交易后本机的余额
};

struct Log_buf {
    struct Log log;
    byte valid;
};

int32 list_update_time;
struct LocalInfo local_info;
struct Log local_log[MAX_LOG];
struct Log_buf local_log_buf[MAX_LOG_BUF];

void init_logbuf() {
    int32 i;
    for (i = 0; i < MAX_LOG_BUF; i++)
        local_log_buf[i].valid = FALSE;
}

int32 log_equal(struct Log* log1, struct Log* log2) {
    if (log1->ipaddr1 != log2->ipaddr1)
        return FALSE;
    if (log1->ipaddr2 != log2->ipaddr2)
        return FALSE;
    if (log1->flag != log2->flag) //事实上这个字段应该都是SUCC
        return FALSE;
    // if (log1->role != log2->role)
    //     return FALSE;
    if (log1->org_amount != log2->org_amount)
        return FALSE;
    // if (log1->amount != log2->amount)
    //     return FALSE;
    // if (log1->balance != log2-> balance)
    //     return FALSE;
    return TRUE;
}

void show_balance(did32 dev) { //打印当前余额
    wait(local_info.balance_lock);
    fprintf(dev, "\nLocal balance: %d\n\n", local_info.local_balance);
    signal(local_info.balance_lock);
}

status expend_balance(int32 amount) { //尝试支出一定金额
    wait(local_info.balance_lock);
    if (local_info.local_balance < amount) {//余额不足
        signal(local_info.balance_lock);
        return SYSERR;
    }
    local_info.local_balance -= amount;  //先扣款，发送失败再返还
    signal(local_info.balance_lock);
    return OK;
}

status income_balance(int32 amount) { //收到金额，增加余额
    wait(local_info.balance_lock);
    local_info.local_balance += amount;  //先扣款，发送失败再返还
    signal(local_info.balance_lock);
    return OK;
}

double atof(const char *str) { //处理写法比较标准的浮点数(用不到)
	double s = 0.0;
	double d = 10.0;
	int base = 0;
	byte sign = FALSE;
 
	while(*str == ' ') str++;
 
	if(*str == '-') { //处理负号
		sign = TRUE;
		str++;
	}
 
	if(!(*str >= '0' && *str <= '9')) //如果一开始非数字则退出，返回0.0
		return s;
 
	while(*str >= '0' && *str <= '9' && *str != '.') { //计算小数点前整数部分
		s = s*10.0 + *str - '0';
		str++;
	}
 
	if(*str == '.') //以后为小数部分
		str++;
 
	while(*str >= '0' && *str <= '9') { //计算小数部分
		s = s + (*str - '0')/d;
		d *= 10.0;
		str++;
	}
 
	if(*str == 'e' || *str == 'E') { //考虑科学计数法
		str++;
		if(*str == '-') {
			str++;
			while(*str >= '0' && *str <= '9') {
				base = base*10 + *str - '0';
				str++;
			}
			while(base > 0) {
				s /= 10;
				base--;
			}
		}
        if(*str == '+')
            str++;
        while(*str >= '0'&& *str <= '9') {
            base = base*10 + *str - '0';
            str++;
        }
        while(base > 0)	{
            s *= 10;
            base--;
        }
	}
 
    return (sign?-s:s);
}

status str2msg(char* buf, int32 length, struct Message* msgbuf) {
    //处理标准形式的消息，即 IP1(dot)_IP2(dot)_protype_amount
    //只有成功解析才返回OK
    uint32 retval;
    char *head, *tail, *ptr;
    head = buf;
    tail = head;

    // kprintf("DEBUG: in str2msg\n");

    while(*tail != '_' && *tail != '\0' && tail < buf + length) 
        tail++;
    if (*tail != '_') //字符串提前结束或溢出
        return SYSERR;    
    *tail = '\0';
    retval = dot2ip(head, &(msgbuf->ipaddr1));
    if (retval != OK) //IP解析错误
        return SYSERR;    
    head = tail + 1;
    tail = head;

    // kprintf("DEBUG: in str2msg, stage 2\n");

    while(*tail != '_' && *tail != '\0' && tail < buf + length) 
        tail++;
    if (*tail != '_') 
        return SYSERR;
    *tail = '\0';
    retval = dot2ip(head, &(msgbuf->ipaddr2));
    if (retval != OK)
        return SYSERR;
    head = tail + 1;
    tail = head;

    // kprintf("DEBUG: in str2msg, stage 3\n");

    while(*tail != '_' && *tail != '\0' && tail < buf + length) 
        tail++;
    if (*tail != '_') 
        return SYSERR;
    *tail = '\0';
    for (ptr = head; ptr < tail; ptr++)
        if (*ptr > '9' || *ptr < '0')   //只允许正的数字，进行检查
            return SYSERR;
    msgbuf->protocol_type = atoi(head);
    head = tail + 1;
    tail = head;

    // kprintf("DEBUG: in str2msg, stage 4\n");

    while(*tail != '_' && *tail != '\0' && tail < buf + length) 
        tail++;
    if (!(*tail == '\0' && tail == buf + length)) //恰好到字符串结尾，没有其他额外分隔符 
        return SYSERR;
    for (ptr = head; ptr < tail; ptr++)
        if (*ptr > '9' || *ptr < '0')
            return SYSERR;
    msgbuf->amount = atoi(head);

    // kprintf("DEUBG: leave str2msg\n");

    return OK;
}

status cmd2msg(char* cmdbuf, int32 length, struct Message* msgbuf) {
    //处理用户交互界面的输入(发送)，即 IP2(dot)_amount
    //只有成功解析才返回OK
    uint32 retval;
    char *head, *tail, *ptr;
    head = cmdbuf;
    tail = head;

    // kprintf("DEBUG: in cmd2msg\n");

    msgbuf->ipaddr1 = NetData.ipucast;  //发送请求的IP1为本机IP

    // kprintf("DEBUG: in cmd2msg, stage 2\n");

    //kprintf("DEBUG: checkpoint #1\n");
    while(*tail != '_' && *tail != '\0' && tail < cmdbuf + length) 
        tail++;
    //kprintf("DEBUG: checkpoint #2\n");
    if (*tail != '_') {
        //kprintf("DEBUG: checkpoint #3\n");
        return SYSERR;
    }
    //kprintf("DEBUG: checkpoint #4\n");
    *tail = '\0';
    //kprintf("DEBUG: checkpoint #5\n");
    retval = dot2ip(head, &(msgbuf->ipaddr2));
    //kprintf("DEBUG: checkpoint #6\n");
    if (retval != OK) {
        //kprintf("DEBUG: checkpoint #7\n");
        return SYSERR;
    }
    //kprintf("DEBUG: checkpoint #8\n");
    head = tail + 1;
    //kprintf("DEBUG: checkpoint #9\n");
    tail = head;
    //kprintf("DEBUG: checkpoint #10\n");

    msgbuf->protocol_type = MSG_DEAL_REQ; //从命令行输入的都是发送请求

    // kprintf("DEBUG: in cmd2msg, stage 3\n");

    while(*tail != '_' && *tail != '\0' && tail < cmdbuf + length) 
        tail++;
    if (!(*tail == '\0' && tail == cmdbuf + length)) //恰好到字符串结尾，没有其他额外分隔符 
        return SYSERR;
    for (ptr = head; ptr < tail; ptr++)
        if (*ptr > '9' || *ptr < '0')
            return SYSERR;
    msgbuf->amount = atoi(head);

    // kprintf("DEUBG: leave cmd2msg\n");

    return OK;
}

status msg2str(char* strbuf, int32 buflen, struct Message* msg, int32* strlen) {
    //将本地的结构体转换为字符串形式，其中strlen所指向的位置用于保存转换完成的字符串长度
    int32 count = 0;
    char* ptr = strbuf;
    sprintf(strbuf, "%d.%d.%d.%d_%d.%d.%d.%d_%d_%d",
        ((msg->ipaddr1)>>24)&0xff,
        ((msg->ipaddr1)>>16)&0xff,
        ((msg->ipaddr1)>>8)&0xff,
        (msg->ipaddr1)&0xff,
        ((msg->ipaddr2)>>24)&0xff,
        ((msg->ipaddr2)>>16)&0xff,
        ((msg->ipaddr2)>>8)&0xff,
        (msg->ipaddr2)&0xff,
        msg->protocol_type,
        msg->amount);
    while(*ptr != '\0' && count < buflen) {
        ptr++;
        count++;
    }
    *strlen = count + 1; //整个字符串长度在此处将'\0'计算在内
    return OK;
}

status msg2log(struct Message* msg, struct Log* logbuf) {  //由接收到的消息填写日志项
    if (msg->ipaddr2 == local_info.local_ipaddr ||
        msg->ipaddr2 == local_info.local_ipaddr) {  //只有矿机和其他机器需要处理
        return IGNORE;
    }
    logbuf->ipaddr1 = msg->ipaddr1;
    logbuf->ipaddr2 = msg->ipaddr2;
    logbuf->org_amount = msg->amount;
    logbuf->flag = FLAG_SUCC;
    logbuf->role = ROLE_OTHER;  //默认字段,下同
    logbuf->amount = 0;

    wait(local_info.balance_lock); //这个数字并不重要
    logbuf->balance = local_info.local_balance;
    signal(local_info.balance_lock);

    return OK;
}

status arg2log(
    struct Log* logbuf, int32 ipaddr1, int32 ipaddr2, byte flag, byte role, double org_amount,
    byte store) {
    //主动通过指定参数填写日志项,store为TRUE或者FALSE,是否直接写入log
    wait(local_info.log_lock);
    wait(local_info.balance_lock); //在这里的balance比较重要，因此加锁

    logbuf->ipaddr1 = ipaddr1;
    logbuf->ipaddr2 = ipaddr2;
    logbuf->flag = flag;
    logbuf->role = role;
    logbuf->org_amount = org_amount;
    logbuf->balance = local_info.local_balance;

    if (flag != FLAG_SUCC) {
        logbuf->amount = 0;
        signal(local_info.balance_lock);
        signal(local_info.log_lock);
        return OK;
    }

    switch(role) {
        case ROLE_SEND: {
            logbuf->amount = -org_amount; 
            break;
        }
        case ROLE_RECV: {
            logbuf->amount = (org_amount/10)*9; 
            break;
        }
        case ROLE_CONTRACT: {
            logbuf->amount = (org_amount/10); 
            break;
        }
        // case ROLE_OTHER: { //实际上OTHER机器不应该主动记录
        //     logbuf->amount = 0; 
        //     break;
        // }
        default: {
            signal(local_info.balance_lock);
            signal(local_info.log_lock);
            return SYSERR;
        }
    }

    if (store == TRUE) {
        local_log[local_info.local_log_count++] = *logbuf;
    }
    signal(local_info.balance_lock);
    signal(local_info.log_lock);

    return OK;
}

/*------------------------------------------------------------------------
 * arp_scan - scan the subnet using ARP (for endGame)
 *------------------------------------------------------------------------
 */
void arp_scan () {
    uint32  scanip, stime;
    int32   retval, j, count = 0;
    byte	mac[ETH_ADDR_LEN];

	/* Scan devices in the subnet using ARP and save to the list */
    stime = clktime;
    printf("Now scanning devices in the subnet ");
    printf("%3d.", (NetData.ipprefix & 0xFF000000) >> 24);
    printf("%3d.", (NetData.ipprefix & 0x00FF0000) >> 16);
    printf("%3d.", (NetData.ipprefix & 0x0000FF00) >> 8);
    printf("%3d\n",  (NetData.ipprefix & 0x000000FF));

    wait(local_info.list_lock);
    for (scanip = NetData.ipprefix + 1; scanip < NetData.ipprefix + 255; scanip++) {
        // assmue the address of the subnet is x.x.x.0
        // and possible ip address of devices are x.x.x.1~x.x.x.254
        retval = arp_resolve(scanip, mac);
        if (retval != OK) 
            continue;
        local_info.local_device_list[count].ipaddr = scanip;
        memcpy(local_info.local_device_list[count].mac, mac, ETH_ADDR_LEN);
        count++;   
    }
    local_info.local_alive_count = count;
    signal(local_info.list_lock);

    list_update_time = clktime;
	printf("\nFinished. time elapsed: %d s\n", list_update_time - stime);
	return;
}

/*------------------------------------------------------------------------
 * list_device - print all devices in the subnet (for endGame)
 *------------------------------------------------------------------------
 */
void list_device() {
    int32 i, j;
    printf("\nDevice list:\n");
    printf("No.     IP Address        Hardware Address   \n");
	printf("---  ---------------     -----------------   \n");

    wait(local_info.list_lock);
    for (i = 0; i < local_info.local_alive_count; i++) {
        printf("%3d", i);
        printf("  ");
		printf("%3d.", (local_info.local_device_list[i].ipaddr & 0xFF000000) >> 24);
		printf("%3d.", (local_info.local_device_list[i].ipaddr & 0x00FF0000) >> 16);
		printf("%3d.", (local_info.local_device_list[i].ipaddr & 0x0000FF00) >> 8);
		printf("%3d",  (local_info.local_device_list[i].ipaddr & 0x000000FF));
		printf("     %02X", local_info.local_device_list[i].mac[0]);
		for (j = 1; j < ETH_ADDR_LEN; j++) {
			printf(":%02X", local_info.local_device_list[i].mac[j]);
		}
		printf("\n");   
    }
    signal(local_info.list_lock);

    printf("\n");
	return;
}

/*------------------------------------------------------------------------
 * list_log - print all logs storaged in the local host (for endGame)
 *------------------------------------------------------------------------
 */
void list_log() {
    int32 i, j;
    printf("\nLogs:\n");
    printf("No.    IP Address 1     IP Address 2   Flag    Role    Amount  Revenue  Balance\n");
	printf("---  ---------------  ---------------  ----  --------  ------  -------  -------\n");

    wait(local_info.log_lock);
    for (i = 0; i < local_info.local_log_count; i++) {
        printf("%3d", i);
        printf("  ");
		printf("%3d.", (local_log[i].ipaddr1 & 0xFF000000) >> 24);
		printf("%3d.", (local_log[i].ipaddr1 & 0x00FF0000) >> 16);
		printf("%3d.", (local_log[i].ipaddr1 & 0x0000FF00) >> 8);
		printf("%3d",  (local_log[i].ipaddr1 & 0x000000FF));
        printf("  ");
		printf("%3d.", (local_log[i].ipaddr2 & 0xFF000000) >> 24);
		printf("%3d.", (local_log[i].ipaddr2 & 0x00FF0000) >> 16);
		printf("%3d.", (local_log[i].ipaddr2 & 0x0000FF00) >> 8);
		printf("%3d",  (local_log[i].ipaddr2 & 0x000000FF));
        printf("  ");
        printf("%4s", flag_str[local_log[i].flag]);
        printf("  ");
        printf("%8s", role_str[local_log[i].role]);
        printf("  ");
        printf("%6d", local_log[i].org_amount);
        printf("  ");
        printf("%7d", local_log[i].amount);
        printf("  ");
        printf("%7d", local_log[i].balance);

		printf("\n");   
    }
    signal(local_info.log_lock);

    printf("\n");
	return;
}
