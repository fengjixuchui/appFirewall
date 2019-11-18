//
//  pcap_sniffer.c
//  com.leith.appFirewall-Helper
//

#include "pcap_sniffer.h"

//globals
static pcap_t *pd;  // pcap listener
static time_t stats_time; // time when last asked pcap for stats
static int p_sock, p_sock2=-1;
static pthread_t listener_thread; // handle to listener thread

void start_sniffer(char* filter_exp) {
	// fire up pcap listener ...
	
	char *intf, ebuf[PCAP_ERRBUF_SIZE];
	
	// get network device
	if ((intf = pcap_lookupdev(ebuf)) == NULL) {
		ERR("pcap couldn't find default device: %s", ebuf);
		//EXITFAIL("Problem listening to network: pcap couldn't find default device: %s", ebuf);
		exit(EXIT_FAILURE);
	}
	//INFO("Listening on device: %s\n", intf);
	bpf_u_int32 mask, net;
	if (pcap_lookupnet(intf, &net, &mask, ebuf) == -1) {
		WARN("Can't get netmask for device %s: %s\n", intf, ebuf);
		net = 0;
		mask = 0;
	}
	
	// create pcap listener
	// args are: char *device, int snaplen, int promisc, int to_ms, char *ebuf
	// nb: to_ms defines reader timeout, snaplen is #bytes kept for each pkt sniffed
#define SNAPLEN 512 // needs to be big enough to capture dns payload
#define TIMEOUT 1
	if ((pd = pcap_open_live(intf, SNAPLEN, 0, TIMEOUT, ebuf)) == NULL) {
		ERR("Couldn't initialize pcap sniffer %s\n",ebuf);
		//EXITFAIL("Couldn't initialize pcap sniffer %s\n",ebuf);
		exit(EXIT_FAILURE);
	}
	
	#define BUFFER_SIZE 2097152*8  // default is 2M=2097152, but we increase it to 16M
	pcap_set_buffer_size(pd, BUFFER_SIZE);
	
	// set the filter ..
	struct bpf_program fp;		/* The compiled filter expression */
	if (pcap_compile(pd, &fp, filter_exp, 0, mask) == -1) {
		ERR("Couldn't parse pcap filter %s: %s\n", filter_exp, pcap_geterr(pd));
		//EXITFAIL("Couldn't parse pcap filter %s: %s\n", filter_exp, pcap_geterr(pd));
		exit(EXIT_FAILURE);
	}
	if (pcap_setfilter(pd, &fp) == -1) {
		ERR("Couldn't install pcap filter %s: %s\n", filter_exp, pcap_geterr(pd));
		//EXITFAIL("Couldn't install pcap filter %s: %s\n", filter_exp, pcap_geterr(pd));
		exit(EXIT_FAILURE);
	}
	
	// we need to specify the link layer header size.  have hard-wired in
	// ethernet value of 14, so check link we have is compatible with this
	int dl;
	if ( (dl=pcap_datalink(pd)) != DLT_EN10MB) { //
		ERR("Device %s not supported: %d\n", intf, dl);
		//EXITFAIL("Device %s not supported: %d\n", intf, dl);
		exit(EXIT_FAILURE);
	}
}

void sniffer_callback(u_char* args, const struct pcap_pkthdr *pkthdr, const u_char* pkt) {
	// send pkt to GUI
	DEBUG2("sniffed pkt, sending to GUI ... %d bytes\n",pkthdr->caplen);
	if (send(p_sock2, pkthdr, sizeof(struct pcap_pkthdr),0)<0) goto err;
	if (send(p_sock2, pkt, pkthdr->caplen,0)<0) goto err;

	// periodically log pcap stats ... we don't want to be seeing too many pkt drops
	time_t stats_now = time(NULL);
	if (stats_now-stats_time > 600) {
		struct pcap_stat stats;
		stats_time = stats_now;
		pcap_stats(pd, &stats);
		INFO("pcap stats: recvd=%d, dropped=%d, if_dropped=%d\n",
		stats.ps_recv,stats.ps_drop,stats.ps_ifdrop);
		fflush(stdout);
	}
	return;
	
err:
	WARN("send: %s\n", strerror(errno));
	// likely helper has shut down connection,
	// in any case close socket and exit pcap listening loop
	pcap_breakloop(pd);
	close(p_sock2);
}

void *listener(void *ptr) {
	// wait in accept() loop to handle connections from GUI to receive pcap info
	struct sockaddr_in remote;
	socklen_t len = sizeof(remote);
	for(;;) {
		INFO("Waiting to accept connection on localhost port %d ...\n", PCAP_PORT);
		if ((p_sock2 = accept(p_sock, (struct sockaddr *)&remote, &len)) <= 0) {
			ERR("Problem accepting new connection on localhost port %d: %s\n", PCAP_PORT, strerror(errno));
			continue;
		}
		INFO("Started new connection on port %d\n", PCAP_PORT);
		// now fire up pcap loop, and will send sniffed pkt info acoss link to GUI client,
		// this will exit when network connection fails/is broken.
		stats_time = time(NULL);
		if (pcap_loop(pd, -1,	sniffer_callback, NULL)==PCAP_ERROR){	// this blocks
			ERR("pcap_loop: %s\n", pcap_geterr(pd));
		}
	}
	return NULL;
}

void start_listener() {
	// start listening for requests to receive pcap info
	p_sock = bind_to_port(PCAP_PORT);
	INFO("Now listening on localhost port %d\n", PCAP_PORT);

	// tcpflags doesn't work for ipv6, sigh.
	// UDP on ports 443 likely to be quic
	start_sniffer("(udp and port 53) or (tcp and (tcp[tcpflags]&tcp-syn!=0) || (ip6[6] == 6 && ip6[53]&tcp-syn!=0)) or (udp and port 443)");
	INFO("pcap initialised\n");
	pthread_create(&listener_thread, NULL, listener, NULL);
}

void stop_listener() {
	pthread_kill(listener_thread, SIGTERM);
}