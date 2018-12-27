/* xsh_arpscan.c - xsh_arpscan */

/*------------------------------------------------------------------------
 * EndGame
 * Name = TangJiajun
 * StudentID = 1500011776
 * StudentNo = 10
 * GroupNo = 5
 *------------------------------------------------------------------------
 */

#include <xinu.h>
#include <stdio.h>
#include <string.h>

static	void	arp_scan();
/*------------------------------------------------------------------------
 * xsh_arp - display the current ARP cache for an interface
 *------------------------------------------------------------------------
 */
shellcmd xsh_arpscan(int nargs, char *args[])
{
	/* For argument '--help', emit help about the 'arp' command	*/

	if (nargs == 2 && strncmp(args[1], "--help", 7) == 0) {
		printf("Use: %s\n\n", args[0]);
		printf("Description:\n");
		printf("\tScan devices in the subnet using ARP\n");
		printf("Options:\n");
		printf("\t--help\t display this help and exit\n");
		return 0;
	}

	/* Dump the Entire ARP cache */
	printf("\n");
	arp_scan();

	return 0;
}


/*------------------------------------------------------------------------
 * arp_scan - scan the subnet using ARP (modified for endGame)
 *------------------------------------------------------------------------
 */
static	void arp_scan ()
{
    uint32  scanip;
    int32   retval, j;
    byte	mac[ETH_ADDR_LEN];

	/* Print devices in the subnet using ARP */

	printf("Device list:\n");
	// printf("   State Pid    IP Address    Hardware Address   Cached Time\n");
	// printf("   ----- --- --------------- -----------------   -----------\n");
    printf("     IP Address    Hardware Address   \n");
	printf("  --------------- -----------------   \n");

    for (scanip = NetData.ipprefix + 1; scanip < NetData.ipprefix + 255; scanip++) {
        // assmue the address of the subnet is x.x.x.0
        // and possible ip address of devices are x.x.x.1~x.x.x.254
        retval = arp_resolve(scanip, mac);
        if (retval != OK) 
            continue;
        printf("  ");
		printf("%3d.", (scanip & 0xFF000000) >> 24);
		printf("%3d.", (scanip & 0x00FF0000) >> 16);
		printf("%3d.", (scanip & 0x0000FF00) >> 8);
		printf("%3d",  (scanip & 0x000000FF));
		printf(" %02X", mac[0]);
		for (j = 1; j < ARP_HALEN; j++) {
			printf(":%02X", mac[j]);
		}
		printf("\n");        
    }
	printf("\n");
	return;
}
