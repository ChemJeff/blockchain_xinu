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
#define MAX_MSG_LEN 128
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

#define IGNORE 0xffff

#define EPS 1e-6

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
    if (retval == SYSERR)   //分配信号量失败
        return SYSERR;
    ptrlocal->balance_lock = retval;
    retval = semcreate(1);
    if (retval == SYSERR)   //分配信号量失败
        return SYSERR;
    ptrlocal->list_lock = retval;
    retval = semcreate(1);
    if (retval == SYSERR)   //分配信号量失败
        return SYSERR;

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
    int32 amount; 
    int32 last_protocol;  // 上一条收到的信息的协议类型
    byte exited;          // 本线程是否已经退出(udp线程应该最后退出)
};

struct Message{
    uint32 ipaddr1;
    uint32 ipaddr2;
    int32 protocol_type;
    int32 amount;
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

int32 list_update_time;
struct LocalInfo local_info;
struct Log local_log[MAX_LOG];

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

    while(*tail != '_' && *tail != '\0' && tail < buf + length) 
        tail++;
    if (!(*tail == '\0' && tail == buf + length)) //恰好到字符串结尾，没有其他额外分隔符 
        return SYSERR;
    for (ptr = head; ptr < tail; ptr++)
        if (*ptr > '9' || *ptr < '0')
            return SYSERR;
    msgbuf->amount = atoi(head);

    return OK;
}

status msg2log(struct Message* msg, struct Log* logbuf) {  //由接收到的消息填写日志项
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

    switch(logbuf->role) {
        case ROLE_SEND: logbuf->amount = -logbuf->org_amount; break;
        case ROLE_OTHER: logbuf->amount = 0; break;
        default: return SYSERR;
    }
    wait(local_info.balance_lock);      //考虑在外层加锁
    logbuf->balance = local_info.local_balance;
    signal(local_info.balance_lock);

    return OK;
}

status arg2log(
    struct Log* logbuf, int32 ipaddr1, int32 ipaddr2, byte flag, byte role, double org_amount) {
    //主动通过指定参数填写日志项
    logbuf->ipaddr1 = ipaddr1;
    logbuf->ipaddr2 = ipaddr2;
    logbuf->flag = flag;
    logbuf->role = role;
    logbuf->org_amount = org_amount;

    if (flag != FLAG_SUCC) {
        logbuf->amount = 0;
        wait(local_info.balance_lock);      //考虑在外层加锁
        logbuf->balance = local_info.local_balance;
        signal(local_info.balance_lock);
    }

    switch(role) {
        case ROLE_SEND: logbuf->amount = -org_amount; break;
        case ROLE_RECV: logbuf->amount = (org_amount/10)*9; break;
        case ROLE_CONTRACT: logbuf->amount = (org_amount/10); break;
        case ROLE_OTHER: logbuf->amount = 0; break;
        default: return SYSERR;
    }
    wait(local_info.balance_lock);      //考虑在外层加锁
    logbuf->balance = local_info.local_balance;
    signal(local_info.balance_lock);

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
    printf("Device list:\n");
    printf("No.     IP Address        Hardware Address   \n");
	printf("---  ---------------     -----------------   \n");
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
    printf("\n");
	return;
}
