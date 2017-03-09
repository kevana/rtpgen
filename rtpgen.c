//============================================================================
//		RTP packet stream generator
//
// Summary:
//		This tool sends an MPEG-1 RTP stream to the indicated address.
//		Most header bits are zeroed, only Sequence number and timestamp are
//		updated.
//
// Compilation for Windows:
//		cc -Wall -o rtpgen.exe rtpgen.c -D WIN32 -lwsock32
//
// Compilation for UNIX:
//		make (requires included Makefile)
//			OR
//		cc -Wall -g -o rtpgen -lrt rtpgen.c
//
// Compilation for OS X:
//		make osx (requires included Makefile)
//			OR
//		cc -Wall -g -o rtpgen rtpgen.c
//
// Example usage:
//		./rtpgen -a 127.0.0.1 -p 9999 -r 30
//
// Author: Kevan Ahlquist
// 
// License: All Rights Reserved
// 
//============================================================================

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef WIN32
#	include <windows.h>
#	include <Winsock.h>
#	include <Winsock2.h>
#	include <Ws2tcpip.h>
#	else
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <sys/stat.h>
#   include <sys/types.h>
#endif
#ifdef __MACH__
#   include <mach/clock.h>
#   include <mach/mach.h>
#endif

//============================================================================

char address[16];
int DEBUG;
int servPort;
float sendRate;
#ifdef WIN32
WSADATA wsaData;
SOCKET sock;
#else
int sock;
struct timespec to_sleep = {0, 0};
#endif
// 16-bit RTP header field, Version 2, P: 0, X: 0, CSRC: 0, M: 0, PT: 32 (MPV)
uint16_t RtpHeader = 0x8020;
uint16_t networkRtpHeader;
// 32-bit Mpeg Video-specific header (RFC 2250)
uint32_t mpegVideoHeader = 0x00000000;
uint32_t networkMpegVideoHeader;
uint16_t sequenceNumber; // RTP Sequence number
uint16_t networkSequenceNumber;
uint32_t rtpTimestamp;
uint32_t networkRtpTimestamp;
uint32_t ssrc;
uint64_t unixTimestamp;
uint64_t networkUnixTimestamp;

const char ttl = 64;

unsigned char msgLength = 0x3D;
unsigned char packetBuffer[1500];
char *testMessage = "TEST PAYLOAD";
char payload[1484];
int payloadLength;
int fdin;
struct sockaddr_in servaddr;

//============================================================================
// FUNCTIONS
uint64_t getUnixTimestamp(void);
uint32_t getRtpTimestamp(float rate);
void help(void);
uint64_t htonll(uint64_t num);
void makePacket(unsigned char *buff);
int sysIsBigEndian(void);
int udpInit(void);
int udpSendPacket(const char * packet);

//--------------------------------------------------
// Closes UDP socket before exiting
void exitProgram() {
#ifdef WIN32
	closesocket(sock);
	WSACleanup();
#else
	close(sock);
#endif
	//printf("Exiting now...\n");
	exit(0);
}

//--------------------------------------------------
// Returns an RTP timestamp based on the current framerate
// Approximates 90,000Hz RTP clock frequency for video
// Normally this value is generated at frame capture time.
// This timestamp is arbitrary.
uint32_t getRtpTimestamp(float rate) {
    static uint32_t rtpTime = 0;
    rtpTime += (uint32_t)(90000/rate);
    return rtpTime;
}

//--------------------------------------------------
// Returns the current Unix timestamp in microseconds
uint64_t getUnixTimestamp(void) {
#ifdef WIN32
	SYSTEMTIME st, epochs;
	FILETIME ft, epochf;
	ULARGE_INTEGER epoch, now;
	
	GetSystemTime(&st);
	epochs.wYear = 1970;
	epochs.wMonth = 1;
	epochs.wDay = 1;
	epochs.wHour = 0;
	epochs.wMinute = 0;
	epochs.wSecond = 0;
	epochs.wMilliseconds = 0;
	
	SystemTimeToFileTime(&st, &ft);
	SystemTimeToFileTime(&epochs, &epochf);
	
	memcpy(&epoch, &epochf, sizeof(epochf));
	memcpy(&now, &ft, sizeof(ft));
	if (now.QuadPart > epoch.QuadPart) {
		return (uint64_t)((now.QuadPart - epoch.QuadPart) / 10);
	}
	else return 0;
#elif defined __gnu_linux__
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ((((uint64_t)ts.tv_sec * 1000000)) + (((uint64_t)ts.tv_nsec / 1000)));
#elif (defined __APPLE__) && (defined __MACH__)
    struct timespec ts;
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    ts.tv_sec = mts.tv_sec;
    ts.tv_nsec = mts.tv_nsec;
    return (uint64_t)((ts.tv_sec * 10^9) + ts.tv_nsec);
#endif
}

//--------------------------------------------------
// Displays help information for the tool
void help(void) {
	printf("Usage: rtpgen ...\n");
	printf("  -a or --address <address>\n\tDestination address in dotted quad notation (e.g. 127.0.0.1)\n\tDefault: 127.0.0.1\n");
	printf("  -p or --port <port>\n\tThe port to send packets to\n\tDefault: 9000\n");
	printf("  -r or --rate <rate>\n\tPackets per second (e.g. rate 30, 30 packets sent per second)\n\tDefault: 1\n");
    printf("  -c or --payload <file>\n\tLoad packet payload from file \n\tDefault:\"TEST PAYLOAD\"\n");
}

//--------------------------------------------------
// Convert ints of type uint64_t to network order, checks if conversion is needed.
uint64_t htonll(uint64_t num) {
	if (sysIsBigEndian()) return num;
	else return (((num & 0xFFULL) << 56) | ((num & 0xFF00000000000000ULL) >> 56) |
                 ((num & 0xFF00ULL) << 40) | ((num & 0x00FF000000000000ULL) >> 40) |
                 ((num & 0xFF0000ULL) << 24) | ((num & 0x0000FF0000000000ULL) >> 24) |
                 ((num & 0xFF000000ULL) << 8) | ((num & 0x000000FF00000000ULL) >> 8));
}


//--------------------------------------------------
// Assemble packet in the given buffer
void makePacket(unsigned char *buff) {
	// Copy all fields into the packet buffer
	memcpy(&buff[0], &networkRtpHeader, 2);
	memcpy(&buff[2], &networkSequenceNumber, 2);
	memcpy(&buff[4], &networkRtpTimestamp, 4);
	memcpy(&buff[8], &ssrc, 4);
	memcpy(&buff[12], &networkMpegVideoHeader, 4);
    //Copy Packet payload
    memcpy(&buff[16], &payload, payloadLength);
	return;
}

//--------------------------------------------------
// Check if the system is big endian or not.
int sysIsBigEndian(void) {
	union {
		uint32_t i;
		char ch[4];
	} tmp = {0x01020304};
	
	return tmp.ch[0] == 1;
}

//--------------------------------------------------
// Initialize UDP socket
int udpInit(void) {
	//printf("udpInit: servPort: %d\n", servPort);
#ifdef WIN32
	int error;
	error = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (error != 0) {
		perror("Unable to initialize WinSock DLL");
		return 1;
	}
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET) {
		//printf("Unable to create socket.");
		perror("Unable to create socket.");
		return -1;
	}
#else
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("Unable to create socket.");
		return -1;
	}
#endif
//	if (setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) != 0) {
//		perror("Unable to set TTL for socket");
//	}
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr(address);
	servaddr.sin_port = htons(servPort);

	//printf("Current servPort: %d, sin_port: %d\n", servPort, servaddr.sin_port);
	return 0;
}

//--------------------------------------------------
// Sends the contents of the given packet
int udpSendPacket(const char * packet) {
	if (sendto(sock, packet, payloadLength+16, 0, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
		//perror("Error sending socket message");
		return -1;
	}
	return 1;
}

//============================================================================
int main(int argc, char *argv[]) {
#ifndef WIN32
	signal(SIGINT, exitProgram);
	signal(SIGTERM, exitProgram);
	signal(SIGHUP, exitProgram);
	signal(SIGKILL, exitProgram);
#endif

	unixTimestamp = getUnixTimestamp();
	networkRtpHeader = htons(RtpHeader);
	networkMpegVideoHeader = htonl(mpegVideoHeader);
    int i;
    
	// Set Default values
	sendRate = 1.0;
	strcpy(address, "127.0.0.1");
	servPort = 9000;
	DEBUG = 0;
	srand(time(0));
	ssrc = rand();
	memcpy(payload, testMessage, strlen(testMessage)+1);
	payloadLength = strlen(testMessage)+1;
	
	printf("\nRTP Stream Generator, Version 0.1\nKevan Ahlquist\nAll Rights Reserved\n\n");
	
	// read user options
	int option_index = 0;
	int optc;
	static struct option long_options[] =
		{
		 {"address", 		required_argument, 0, 'a'},
		 {"port", 	 		required_argument, 0, 'p'},
		 {"rate",  	 		required_argument, 0, 'r'},
		 {"payload",		required_argument, 0, 'c'},
		 {"help", no_argument,       0, 'h'},
		 {"version", no_argument,       0, 'v'},
		 {0, 0, 0, 0}
		};
	while (( optc = getopt_long(argc, argv, "a:p:r:c:hv", long_options, &option_index)) != -1) {
		switch(optc) {
			case 'a':
				strncpy(address, optarg, 16);
				address[15] = '\0'; // Prevent buffer overrun
				printf("Address received: %s\n", address);
				break;
			case 'p':
				servPort = atol(optarg);
				printf("Port received: %d\n", servPort);
				break;
			case 'r':
				sendRate = atof(optarg);
				printf("Rate received: %f\n", sendRate);
				if (sendRate > 10000) {
					printf("Values greater than 10000 packets per second are not supported\n");
					exit(0);
				}
				break;
			case 'c':
				// Open file
				fdin = open(optarg, O_RDONLY);
				if (fdin < 0) {
					perror("Error opening payload file");
					exit(1);
				}
				payloadLength = read(fdin, payload, 1484);
				if (payloadLength < 0) {
					perror("Error reading from payload file");
					exit(1);
				}
				close(fdin);
				break;
			case 'h':
				help();
				exit(0);
				break;
			case 'v':
				exit(0);
				break;
			default:
				printf("Usage: udpout -a <address>:<port> -r <rate> -c <payload>\n");
				printf("For help use option -h or --help\n");
				exit(0);
		}
	}
	
	if (udpInit() == -1) exit(-1);
    
	while (1) {
		networkUnixTimestamp = htonll(getUnixTimestamp());
        networkRtpTimestamp = htonl(getRtpTimestamp(sendRate));
		networkSequenceNumber = htons(++sequenceNumber); // Sequence number is unique for each packet, while timestamp may remain the same
		makePacket(packetBuffer);
		udpSendPacket((const char *)packetBuffer);
		if (DEBUG) {
			printf("Packet contents:\n");
			for (i = 0; i < 80; ++i) {
				printf("%2X ", packetBuffer[i]);
				if ((i == 16) || (i == 26) || (i == 40) || (i == 54) || 
						(i == 60) || (i == 66) || (i == 70) || (i == 73)) {
					printf("\n");
				}
			}
		}
#ifdef WIN32
		Sleep((1 / sendRate) * 1000);
#else
        	long long int sleep;
		sleep = 1000000000 / sendRate;
		to_sleep.tv_sec = (time_t) (sleep / 1000000000);
		to_sleep.tv_nsec = (long) (sleep % 1000000000);
		while ((nanosleep(&to_sleep, &to_sleep) == -1) && (errno == EINTR));
#endif
	}
}
