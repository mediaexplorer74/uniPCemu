/*

Copyright (C) 2019 - 2021 Superfury

This file is part of UniPCemu.

UniPCemu is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

UniPCemu is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with UniPCemu.  If not, see <https://www.gnu.org/licenses/>.
*/

//Only when not using Windows, include types first!
#ifndef _WIN32
#include "headers/types.h" //Basic types first! Also required for system detection!
#endif

//Compile without PCAP support, but with server simulation when NOPCAP and PACKERSERVER_ENABLED is defined(essentially a server without login information and PCap support(thus no packets being sent/received))?
/*
#define NOPCAP
#define PACKETSERVER_ENABLED
*/

#if defined(PACKETSERVER_ENABLED)
#define HAVE_REMOTE

//Missing for various systems?
#if !defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
//On Linux and MinGW!
typedef unsigned char u_char;
typedef unsigned int u_int;
typedef unsigned short u_short;
#endif

//WPCAP is defined by support when using winpcap! Don't define it here anymore!
#ifndef NOPCAP
#ifdef _WIN32
#ifndef WPCAP
//Temporarily define WPCAP!
#define WPCAP
#define WPCAP_WASNTDEFINED
#endif
#ifndef WIN32
//Make sure WIN32 is also defined with _WIN32 for PCAP to successfully be used!
#define WIN32
#endif
#endif
#include <pcap.h>
#ifdef WPCAP_WASNTDEFINED
//Undefine the temporary WPCAP define!
#undef WPCAP
#endif
#ifdef _WIN32
#include <tchar.h>
#endif
#endif
#endif

#include "headers/types.h" //Basic types first! Also required for system detection!

//Remaining headers
#include "headers/hardware/modem.h" //Our basic definitions!

#include "headers/support/zalloc.h" //Allocation support!
#include "headers/hardware/uart.h" //UART support for the COM port!
#include "headers/support/fifobuffer.h" //FIFO buffer support!
#include "headers/support/locks.h" //Locking support!
#include "headers/bios/bios.h" //BIOS support!
#include "headers/support/tcphelper.h" //TCP support!
#include "headers/support/log.h" //Logging support for errors!
#include "headers/support/highrestimer.h" //High resolution timing support for cleaning up DHCP!
#include "headers/emu/threads.h" //Thread support for pcap!
#include "headers/emu/gpu/gpu.h" //Message box support!

#if defined(PACKETSERVER_ENABLED)
#include <stdint.h>
#include <stdlib.h>

//Nice little functionality for dynamic loading of the Windows libpcap dll!

#ifdef _WIN32

// DLL loading
#define pcap_sendpacket(A,B,C)			PacketSendPacket(A,B,C)
#define pcap_close(A)					PacketClose(A)
#define pcap_freealldevs(A)				PacketFreealldevs(A)
#define pcap_open(A,B,C,D,E,F)			PacketOpen(A,B,C,D,E,F)
#define pcap_next_ex(A,B,C)				PacketNextEx(A,B,C)
#define pcap_findalldevs_ex(A,B,C,D)	PacketFindALlDevsEx(A,B,C,D)
#define pcap_geterr(A)	PacketGetError(A)
#define pcap_datalink(A) PacketDataLink(A)

int (*PacketSendPacket)(pcap_t*, const u_char*, int) = 0;
void (*PacketClose)(pcap_t*) = 0;
void (*PacketFreealldevs)(pcap_if_t*) = 0;
pcap_t* (*PacketOpen)(char const*, int, int, int, struct pcap_rmtauth*, char*) = 0;
int (*PacketNextEx)(pcap_t*, struct pcap_pkthdr**, const u_char**) = 0;
int (*PacketFindALlDevsEx)(char*, struct pcap_rmtauth*, pcap_if_t**, char*) = 0;
char* (*PacketGetError)(pcap_t*) = 0;
int	(*PacketDataLink)(pcap_t*) = 0;

char pcap_src_if_string[] = PCAP_SRC_IF_STRING;

byte LoadPcapLibrary() {
	// remember if we've already initialized the library
	static HINSTANCE pcapinst = (HINSTANCE)-1;
	if (pcapinst != (HINSTANCE)-1) {
		return (pcapinst != NULL);
	}

	// init the library
	pcapinst = LoadLibrary("WPCAP.DLL");
	if (pcapinst == NULL) {
		return FALSE;
	}
	FARPROC psp;

#ifdef __MINGW32__
	// C++ defines function and data pointers as separate types to reflect
	// Harvard architecture machines (like the Arduino). As such, casting
	// between them isn't portable and GCC will helpfully warn us about it.
	// We're only running this code on Windows which explicitly allows this
	// behaviour, so silence the warning to avoid confusion.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

	psp = GetProcAddress(pcapinst, "pcap_sendpacket");
	if (!PacketSendPacket) PacketSendPacket =
		(int(__cdecl*)(pcap_t*, const u_char*, int))psp;

	psp = GetProcAddress(pcapinst, "pcap_close");
	if (!PacketClose) PacketClose =
		(void(__cdecl*)(pcap_t*)) psp;

	psp = GetProcAddress(pcapinst, "pcap_freealldevs");
	if (!PacketFreealldevs) PacketFreealldevs =
		(void(__cdecl*)(pcap_if_t*)) psp;

	psp = GetProcAddress(pcapinst, "pcap_open");
	if (!PacketOpen) PacketOpen =
		(pcap_t * (__cdecl*)(char const*, int, int, int, struct pcap_rmtauth*, char*)) psp;

	psp = GetProcAddress(pcapinst, "pcap_next_ex");
	if (!PacketNextEx) PacketNextEx =
		(int(__cdecl*)(pcap_t*, struct pcap_pkthdr**, const u_char**)) psp;

	psp = GetProcAddress(pcapinst, "pcap_findalldevs_ex");
	if (!PacketFindALlDevsEx) PacketFindALlDevsEx =
		(int(__cdecl*)(char*, struct pcap_rmtauth*, pcap_if_t**, char*)) psp;

	psp = GetProcAddress(pcapinst, "pcap_geterr");
	if (!PacketGetError) PacketGetError =
		(char* (__cdecl*)(pcap_t*)) psp;

	psp = GetProcAddress(pcapinst, "pcap_datalink");
	if (!PacketDataLink) PacketDataLink =
		(int (__cdecl*)(pcap_t*)) psp;

#ifdef __MINGW32__
#pragma GCC diagnostic pop
#endif

	if (PacketFindALlDevsEx == 0 || PacketNextEx == 0 || PacketOpen == 0 ||
		PacketFreealldevs == 0 || PacketClose == 0 || PacketSendPacket == 0 ||

		PacketGetError == 0) {
		dolog("ethernetcard","Incorrect or non-functional WinPcap version.");
		pcapinst = NULL;
		return FALSE;
	}

	return TRUE;
}

#endif

//End of the libpcap support for Windows!

#endif

/*

Packet server support!

*/

extern BIOS_Settings_TYPE BIOS_Settings; //Currently used settings!

/* packet.c: functions to interface with libpcap/winpcap for ethernet emulation. */

byte PacketServer_running = 0; //Is the packet server running(disables all emulation but hardware)?
uint8_t maclocal[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; //The MAC address of the modem we're emulating!
uint8_t packetserver_broadcastMAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }; //The MAC address of the modem we're emulating!
byte packetserver_sourceMAC[6]; //Our MAC to send from!
byte packetserver_gatewayMAC[6]; //Gateway MAC to send to!
byte packetserver_defaultstaticIP[4] = { 0,0,0,0 }; //Static IP to use?
uint_32 packetserver_hostIPaddrd = 0; //Host IP to use for subnetting?
byte packetserver_defaultgatewayIP = 0; //Gotten a default gateway IP?
byte packetserver_defaultgatewayIPaddr[4] = { 0,0,0,0 }; //Default gateway IP to use?
uint_32 packetserver_defaultgatewayIPaddrd = 0; //Default gateway IP to use?
byte packetserver_DNS1IP = 0; //Gotten a default gateway IP?
byte packetserver_DNS1IPaddr[4] = { 0,0,0,0 }; //Default gateway IP to use?
byte packetserver_DNS2IP = 0; //Gotten a default gateway IP?
byte packetserver_DNS2IPaddr[4] = { 0,0,0,0 }; //Default gateway IP to use?
byte packetserver_NBNS1IP = 0; //Gotten a default gateway IP?
byte packetserver_NBNS1IPaddr[4] = { 0,0,0,0 }; //Default gateway IP to use?
byte packetserver_NBNS2IP = 0; //Gotten a default gateway IP?
byte packetserver_NBNS2IPaddr[4] = { 0,0,0,0 }; //Default gateway IP to use?
byte packetserver_subnetmaskIP = 0; //Gotten a default gateway IP?
byte packetserver_subnetmaskIPaddr[4] = { 0,0,0,0 }; //Default gateway IP to use?
uint_32 packetserver_subnetmaskIPaddrd = 0; //Default gateway IP to use?
byte packetserver_hostsubnetmaskIP = 0; //Gotten a default gateway IP?
byte packetserver_hostsubnetmaskIPaddr[4] = { 0,0,0,0 }; //Default gateway IP to use?
char packetserver_hostsubnetmaskIPstr[256] = ""; //Static IP, string format
uint_32 packetserver_hostsubnetmaskIPaddrd = 0; //Default gateway IP to use?
byte packetserver_broadcastIP[4] = { 0xFF,0xFF,0xFF,0xFF }; //Broadcast IP to use?
byte packetserver_usedefaultStaticIP = 0; //Use static IP?
char packetserver_defaultstaticIPstr[256] = ""; //Static IP, string format
char packetserver_defaultgatewayIPstr[256] = ""; //Static IP, string format
char packetserver_DNS1IPstr[256] = ""; //Static IP, string format
char packetserver_DNS2IPstr[256] = ""; //Static IP, string format
char packetserver_NBNS1IPstr[256] = ""; //Static IP, string format
char packetserver_NBNS2IPstr[256] = ""; //Static IP, string format
char packetserver_subnetmaskIPstr[256] = ""; //Static IP, string format

typedef struct
{
	byte* buffer;
	uint_32 size;
	uint_32 length;
} MODEM_PACKETBUFFER; //Packet buffer for PAD packets!

//Authentication data and user-specific data!
typedef struct
{
	uint16_t pktlen;
	byte *packet; //Current packet received!
	uint16_t IPpktlen;
	byte *IPpacket; //Current packet received!
	byte *packetserver_transmitbuffer; //When sending a packet, this contains the currently built decoded data, which is already decoded!
	uint_32 packetserver_bytesleft;
	uint_32 packetserver_transmitlength; //How much has been built?
	uint_32 packetserver_transmitsize; //How much has been allocated so far, allocated in whole chunks?
	byte packetserver_transmitstate; //Transmit state for processing escaped values!
	char packetserver_username[256]; //Username(settings must match)
	char packetserver_password[256]; //Password(settings must match)
	char packetserver_protocol[256]; //Protocol(slip). Hangup when sent with username&password not matching setting.
	byte packetserver_staticIP[4]; //Static IP to assign this user!
	char packetserver_staticIPstr[256]; //Static IP, string format
	byte packetserver_useStaticIP; //Use static IP?
	byte packetserver_slipprotocol; //Are we using the slip protocol?
	byte packetserver_slipprotocol_pppoe; //Are we using the PPPOE protocol instead of PPP?
	byte packetserver_stage; //Current login/service/packet(connected and authenticated state).
	word packetserver_stage_byte; //Byte of data within the current stage(else, use string length or connected stage(no position; in SLIP mode). 0xFFFF=Init new stage.
	byte packetserver_stage_byte_overflown; //Overflown?
	char packetserver_stage_str[4096]; //Buffer containing output data for a stage
	byte packetserver_credentials_invalid; //Marked invalid by username/password/service credentials?
	char packetserver_staticIPstr_information[268];
	DOUBLE packetserver_delay; //Delay for the packet server until doing something!
	uint_32 packetserver_packetpos; //Current pos of sending said packet!
	byte lastreceivedCRLFinput; //Last received input for CRLF detection!
	byte packetserver_packetack;
	sword connectionid; //The used connection!
	byte used; //Used client record?
	//Connection for PPP connections!
	MODEM_PACKETBUFFER pppoe_discovery_PADI; //PADI(Sent)!
	MODEM_PACKETBUFFER pppoe_discovery_PADO; //PADO(Received)!
	MODEM_PACKETBUFFER pppoe_discovery_PADR; //PADR(Sent)!
	MODEM_PACKETBUFFER pppoe_discovery_PADS; //PADS(Received)!
	MODEM_PACKETBUFFER pppoe_discovery_PADT; //PADT(Send final)!
	//Disconnect clears all of the above packets(frees them if set) when receiving/sending a PADT packet!
	//DHCP data
	MODEM_PACKETBUFFER DHCP_discoverypacket; //Discovery packet that's sent!
	MODEM_PACKETBUFFER DHCP_offerpacket; //Offer packet that's received!
	MODEM_PACKETBUFFER DHCP_requestpacket; //Request packet that's sent!
	MODEM_PACKETBUFFER DHCP_acknowledgepacket; //Acknowledge packet that's sent!
	MODEM_PACKETBUFFER DHCP_releasepacket; //Release packet that's sent!
	byte ppp_autodetectpos;
	byte ppp_autodetectbuf[7];
	byte ppp_autodetected;
	//PPP data
	byte ppp_sendframing; //Sender frame status. Gets toggled for each PPP flag received! 1=Frame active, 0=Frame inactive
	byte PPP_packetstartsent; //Has a packet start been sent to the client?
	//End of PPP framing information

	byte PPP_packetreadyforsending; //Is the PPP packet ready to be sent to the client? 1 when containing data for the client, 0 otherwise. Ignored for non-PPP clients!
	byte PPP_packetpendingforsending; //Is the PPP packet pending processed for the client? 1 when pending to be processed for the client, 0 otherwise. Ignored for non-PPP clients!
	//Most PPP statuses and numbers are sets of two values: index 0 is the receiver(the client) and used for sending data properly to the client(how to send to the client), index 1 is the sender(the server) and used for receiving data properly from the client(how to receive from the client).
	//PPP CP packet processing
	byte PPP_headercompressed[2]; //Is the header compressed?
	byte PPP_protocolcompressed[2]; //Is the protocol compressed?
	word PPP_MRU[2]; //Pending MRU field for the request!
	MODEM_PACKETBUFFER ppp_response; //The PPP packet that's to be sent to the client!
	MODEM_PACKETBUFFER ppp_nakfields, ppp_nakfields_ipxcp, ppp_nakfields_ipcp, ppp_rejectfields, ppp_rejectfields_ipxcp, ppp_rejectfields_ipcp; //The NAK and Reject packet that's pending to be sent!
	byte ppp_nakfields_identifier, ppp_nakfields_ipxcp_identifier, ppp_nakfields_ipcp_identifier, ppp_rejectfields_identifier, ppp_rejectfields_ipxcp_identifier, ppp_rejectfields_ipcp_identifier; //The NAK and Reject packet identifier to be sent!
	byte ppp_LCPstatus[2]; //Current LCP status. 0=Init, 1=Open.

	//Some extra data for the server-client PPP LCP connection!
	DOUBLE ppp_serverLCPrequesttimer; //Server LCP request timer until a response is gotten!
	byte ppp_serverLCPstatus; //Server LCP status! 0=Not ready yet, 1=First requesting sent
	byte ppp_servercurrentLCPidentifier; //Current Server LCP identifier!
	byte ppp_serverLCPidentifier; //Server LCP identifier!
	byte ppp_serverLCP_haveMRU; //MRU trying?
	word ppp_serverLCP_pendingMRU; //MRU that's pending!
	//authentication protocol unsupported atm.
	//quality protocol unused
	byte ppp_serverLCP_haveMagicNumber; //Magic number trying?
	byte ppp_serverLCP_pendingMagicNumber[4]; //Magic number that's pending!
	byte ppp_serverLCP_haveProtocolFieldCompression; //Protocol Field Compression trying?
	byte ppp_serverLCP_haveAddressAndControlFieldCompression; //Address and Control Field Compression trying?
	byte ppp_serverLCP_haveAsyncControlCharacterMap; //ASync Control Character Map enabled?
	byte ppp_serverLCP_haveAuthenticationProtocol; //Authentication protocol enabled?
	byte ppp_serverLCP_pendingASyncControlCharacterMap[4]; //ASync control character map that's pending!

	//Some extra data for the server-client PPP PAP connection
	DOUBLE ppp_serverPAPrequesttimer; //Server LCP request timer until a response is gotten!
	byte ppp_serverPAPstatus; //Server LCP status! 0=Not ready yet, 1=First requesting sent
	byte ppp_servercurrentPAPidentifier; //Current Server LCP identifier!
	byte ppp_serverPAPidentifier; //Server LCP identifier!

	byte ppp_serverprotocolroulette; //Current protocol trying from the server.
	//Some extra data for the server-client PPP IPXCP connection
	DOUBLE ppp_serverIPXCPrequesttimer; //Server LCP request timer until a response is gotten!
	byte ppp_serverIPXCPstatus; //Server LCP status! 0=Not ready yet, 1=First requesting sent
	byte ppp_servercurrentIPXCPidentifier; //Current Server LCP identifier!
	byte ppp_serverIPXCPidentifier; //Server LCP identifier!

	//And for IPCP connection
	DOUBLE ppp_serverIPCPrequesttimer; //Server LCP request timer until a response is gotten!
	byte ppp_serverIPCPstatus; //Server LCP status! 0=Not ready yet, 1=First requesting sent
	byte ppp_servercurrentIPCPidentifier; //Current Server LCP identifier!
	byte ppp_serverIPCPidentifier; //Server LCP identifier!
	//all server-to-client settings for IPXCP
	byte ppp_serverIPXCP_havenetworknumber;
	byte ppp_serverIPXCP_pendingnetworknumber[4];
	byte ppp_serverIPXCP_havenodenumber;
	byte ppp_serverIPXCP_pendingnodenumber[6];
	byte ppp_serverIPXCP_haveroutingprotocol;
	word ppp_serverIPXCP_pendingroutingprotocol;
	//Normal connection data
	byte ppp_protocolreject_count; //Protocol-Reject counter. From 0 onwards
	byte magic_number[2][4];
	byte have_magic_number[2];
	byte ppp_PAPstatus[2]; //0=Not authenticated, 1=Authenticated.
	byte ppp_IPXCPstatus[2]; //0=Not connected, 1=Connected
	byte ppp_IPCPstatus[2]; //0=Not connected, 1=Connected
	byte ppp_suppressIPXCP; //IPXCP suppression requested by the client?
	byte ppp_suppressIPX; //IPX suppression requested by the client?
	byte ppp_suppressIPCP; //IPXCP suppression requested by the client?
	byte ppp_suppressIP; //IP suppression requested by the client?
	byte ipxcp_networknumber[2][4];
	byte ipxcp_nodenumber[2][6];
	byte ipxcp_networknumberecho[4]; //Echo address during negotiation
	byte ipxcp_nodenumberecho[6]; //Echo address negotiation
	word ipxcp_routingprotocol[2];
	byte ipxcp_negotiationstatus; //Negotiation status for the IPXCP login. 0=Ready for new negotiation. 1=Negotiation request has been sent. 2=Negotation has been given a reply and to NAK, 3=Negotiation has succeeded.
	DOUBLE ipxcp_negotiationstatustimer; //Negotiation status timer for determining response time!
	//IPCP data
	byte ppp_serverIPCP_haveipaddress;
	byte ppp_serverIPCP_pendingipaddress[4];
	byte ipcp_ipaddress[2][4];
	byte ipcp_DNS1ipaddress[2][4];
	byte ipcp_DNS2ipaddress[2][4];
	byte ipcp_NBNS1ipaddress[2][4];
	byte ipcp_NBNS2ipaddress[2][4];
	byte ipcp_subnetmaskipaddress[2][4];
	uint_32 ipcp_subnetmaskipaddressd; //Subnet mask the client chose!
	uint_32 asynccontrolcharactermap[2]; //Async control character map, stored in little endian format!
	void* next, * prev; //Next and previous (un)allocated client!
	word connectionnumber; //The number of this entry in the list!
	
	//ARP request support for sending!
	TicksHolder ARPtimer;
	byte ARPrequeststatus; //0=None, 1=Requesting, 2=Result loaded.
	byte ARPrequestIP[4]; //What was requested!
	byte ARPrequestresult[6]; //What was the result!
	byte roundrobinpackettype; //IP/Other packet type switcher!
} PacketServer_client, *PacketServer_clientp;

PacketServer_client Packetserver_clients[0x100]; //Up to 100 clients!
PacketServer_clientp Packetserver_freeclients; //All free clients
PacketServer_clientp Packetserver_allocatedclients; //All allocated clients
PacketServer_clientp Packetserver_unusableclients; //Unusable clients

//How much to delay before sending a message while authenticating?
#define PACKETSERVER_MESSAGE_DELAY 10000000.0
//How much to delay before DHCP timeout?
#define PACKETSERVER_DHCP_TIMEOUT 5000000000.0
//How much to delay before starting the SLIP service?
#define PACKETSERVER_SLIP_DELAY 300000000.0

//Different stages of the auth process:
//Ready stage 
//QueryUsername: Sending username request
#define PACKETSTAGE_REQUESTUSERNAME 1
//EnterUsername: Entering username
#define PACKETSTAGE_ENTERUSERNAME 2
//QueryPassword: Sending password request
#define PACKETSTAGE_REQUESTPASSWORD 3
//EnterPassword: Entering password
#define PACKETSTAGE_ENTERPASSWORD 4
//QueryProtocol: Sending protocol request
#define PACKETSTAGE_REQUESTPROTOCOL 5
//EnterProtocol: Entering protocol
#define PACKETSTAGE_ENTERPROTOCOL 6
//DHCP: DHCP obtaining or release phase.
#define PACKETSTAGE_DHCP 7
//Information: IP&MAC autoconfig. Terminates connection when earlier stages invalidate.
#define PACKETSTAGE_INFORMATION 8
//Ready: Sending ready and entering SLIP mode when finished.
#define PACKETSTAGE_READY 9
//SLIP: Delaying before starting the SLIP mode!
#define PACKETSTAGE_SLIPDELAY 10
//SLIP: Transferring SLIP data
#define PACKETSTAGE_PACKETS 11
//Initial packet stage without credentials
#define PACKETSTAGE_INIT PACKETSTAGE_REQUESTPROTOCOL
//Initial packet stage with credentials
#define PACKETSTAGE_INIT_PASSWORD PACKETSTAGE_REQUESTUSERNAME
//Packet stage initializing
#define PACKETSTAGE_INITIALIZING 0xFFFF

//SLIP reserved values
//End of frame byte!
#define SLIP_END 0xC0
//Escape byte!
#define SLIP_ESC 0xDB
//END is being send(send after ESC)
#define SLIP_ESC_END 0xDC
//ESC is being send(send after ESC)
#define SLIP_ESC_ESC 0xDD

//PPP reserved values
//End of frame byte
#define PPP_END 0x7E
//Escape
#define PPP_ESC 0x7D
//Escaped value encoding and decoding
#define PPP_ENCODEESC(val) (val^0x20)
#define PPP_DECODEESC(val) (val^0x20)

//What configuration to use when we're receiving data from the client
#define PPP_RECVCONF 0
//What configuration to use when we're sending data to the client
#define PPP_SENDCONF 1

//Easy phase detection
//LCP:
//LCP has reached Open or Authenticated phases
#define LCP_OPEN (connectedclient->ppp_LCPstatus[PPP_RECVCONF] && connectedclient->ppp_LCPstatus[PPP_SENDCONF])
//LCP is authenticating with the client
#define LCP_AUTHENTICATING ((connectedclient->ppp_PAPstatus[PPP_SENDCONF]==0) && LCP_OPEN)
//LCP is in Network phase
#define LCP_NCP ((connectedclient->ppp_PAPstatus[PPP_RECVCONF] && connectedclient->ppp_PAPstatus[PPP_SENDCONF]) && LCP_OPEN)
//Network protocol have reached Open
#define IPXCP_OPEN ((connectedclient->ppp_IPXCPstatus[PPP_RECVCONF] && connectedclient->ppp_IPXCPstatus[PPP_SENDCONF]) && LCP_NCP)
#define IPCP_OPEN ((connectedclient->ppp_IPCPstatus[PPP_RECVCONF] && connectedclient->ppp_IPCPstatus[PPP_SENDCONF]) && LCP_NCP)

//Define below to encode/decode the PPP packets sent/received from the user using the PPP_ESC values
#define PPPOE_ENCODEDECODE 0

#ifdef PACKETSERVER_ENABLED
struct netstruct { //Supported, thus use!
#else
struct {
#endif
	uint16_t pktlen;
	byte *packet; //Current packet received!
} net, IPnet, loopback;

#include "headers/packed.h"
typedef union PACKED
{
	struct
	{
		byte dst[6]; //Destination MAC!
		byte src[6]; //Source MAC!
		word type; //What kind of packet!
	};
	byte data[14]; //The data!
} ETHERNETHEADER;
#include "headers/endpacked.h"

#include "headers/packed.h"
typedef struct PACKED
{
	//Pseudo IP header
	byte srcaddr[4];
	byte dstaddr[4];
	byte mustbezero;
	byte protocol;
	word UDPlength;
} UDPpseudoheader;
#include "headers/endpacked.h"

#include "headers/packed.h"
typedef struct PACKED
{
	word sourceport;
	word destinationport;
	word length;
	word checksum;
} UDPheader;
#include "headers/endpacked.h"


#include "headers/packed.h"
typedef union PACKED
{
	UDPpseudoheader header;
	byte data[12]; //12 bytes of data!
} UDPpseudoheadercontainer;
#include "headers/endpacked.h"

#include "headers/packed.h"
typedef struct PACKED
{
	byte version_IHL; //Low 4 bits=Version, High 4 bits is size in 32-bit dwords.
	byte DSCP_ECN;
	word totallength; //Total length of allocation for the entire packet to be received.
	word identification;
	byte flags7_5_fragmentoffsethigh4_0; //flags 2:0, fragment offset high 7:3(bits 4:0 of the high byte)
	byte fragmentoffset; //Remainder of fragment offset low byte
	byte TTL;
	byte protocol;
	word headerchecksum;
	byte sourceaddr[4];
	byte destaddr[4];
	//Now come the options, which are optional.
} IPv4header;
#include "headers/endpacked.h"

#include "headers/packed.h"
typedef struct PACKED
{
	word htype;
	word ptype;
	byte hlen;
	byte plen;
	word oper;
	byte SHA[6]; //Sender hardware address
	uint_32 SPA; //Sender protocol address
	byte THA[6]; //Target hardware address
	uint_32 TPA; //Target protocol address
} ARPpackettype;
#include "headers/endpacked.h"

#include "headers/packed.h"
typedef struct PACKED
{
	word CheckSum;
	word Length;
	byte TransportControl;
	byte PacketType;
	byte DestinationNetworkNumber[4];
	byte DestinationNodeNumber[6];
	word DestinationSocketNumber;
	byte SourceNetworkNumber[4];
	byte SourceNodeNumber[6];
	word SourceSocketNumber;
} IPXPACKETHEADER;
#include "headers/endpacked.h"

//Normal modem operations!
//Text buffer size for transmitting text to the DTE.
#define MODEM_TEXTBUFFERSIZE 256

//Server polling speed
#define MODEM_SERVERPOLLFREQUENCY 1000
//Data tranfer frequency of transferring data
#define MODEM_DATATRANSFERFREQUENCY 57600
//Data transfer frequency of tranferring data, in the numeric result code of the connection numeric result code! Must match the MODEM_DATATRANSFERFREQUENCY
#define MODEM_DATATRANSFERFREQUENCY_NR 18
//Command completion timeout after receiving a carriage return during a command!
#define MODEM_COMMANDCOMPLETIONTIMEOUT (DOUBLE)((1000000000.0/57600.0)*5760.0)

struct
{
	byte supported; //Are we supported?
	FIFOBUFFER *inputbuffer; //The input buffer!
	FIFOBUFFER *inputdatabuffer[0x100]; //The input buffer, data mode only!
	FIFOBUFFER *outputbuffer[0x100]; //The output buffer!
	FIFOBUFFER* blockoutputbuffer[0x100]; //The block output buffer! For sending whole blocks of data at once! Using this must wait for the buffer to be empty before writing blocks of (encoded) data to it.
	byte datamode; //1=Data mode, 0=Command mode!
	byte connected; //Are we connected?
	word connectionport; //What port to connect to by default?
	byte previousATCommand[256]; //Copy of the command for use with "A/" command!
	byte ATcommand[256]; //AT command in uppercase when started!
	byte ATcommandoriginalcase[256]; //AT command in original unmodified case!
	word ATcommandsize; //The amount of data sent!
	byte escaping; //Are we trying to escape?
	DOUBLE timer; //A timer for detecting timeout!
	DOUBLE ringtimer; //Ringing timer!
	DOUBLE serverpolltimer; //Network connection request timer!
	DOUBLE networkdatatimer; //Network connection request timer!

	DOUBLE serverpolltick; //How long it takes!
	DOUBLE networkpolltick;
	DOUBLE detectiontimer[2]; //For autodetection!
	DOUBLE RTSlineDelay; //Delay line on the CTS!
	DOUBLE effectiveRTSlineDelay; //Effective CTS line delay to use!
	DOUBLE DTRlineDelay; //Delay line on the DSR!
	DOUBLE effectiveDTRlineDelay; //Effective DSR line delay to use!

	byte TxDisMark; //Is TxD currently in mark state?
	byte TxDisBreak; //Is TxD currently in the break state?

	//Various parameters used!
	byte communicationstandard; //What communication standard! B command!
	byte echomode; //Echo everything back to use user? E command!
	byte offhook; //1: Off hook(disconnected), 2=Off hook(connected), otherwise on-hook(disconnected)! H command!
	byte verbosemode; //Verbose mode: 0=Numeric result codes, 1=Text result codes, 2=Quiet mode(no response). Bit 0=V command, Bits 1-2=Q command
	byte speakervolume; //Speaker volume! L command!
	byte speakercontrol; //0=Always off, 1=On until carrier detected, 2=Always on, 3=On only while answering! M command!
	byte callprogressmethod; //Call progress method! X command!
	byte lastnumber[256]; //Last-dialed number!
	byte currentregister; //What register is selected?
	byte registers[256]; //All possible registers!
	byte flowcontrol; //&K command! See below for an explanation!
	/*
	0=Blind dial and no busy detect. CONNECT message when established.
	1=Blind dial and no busy detect. Connection speed in BPS added to CONNECT string.
	2=Dial tone detection, but no busy detection. Connection speed in BPS added to the CONNECT string.
	3=Blind dial, but busy detection. Connection speed in BPS appended to the CONNECT string.
	4=Dial tone detection and busy tone detection. Connection speed in BPS appended to the CONNECT string.
	*/
	byte communicationsmode; //Communications mode, default=5! &Q command!

	//Active status emulated for the modem!
	byte ringing; //Are we ringing?
	byte DTROffResponse; //Default: full reset! &D command!
	byte DSRisConnectionEstablished; //Default: assert high always! &S command!
	byte DCDisCarrier; //&C command!
	byte CTSAlwaysActive; //Default: always active! &R command!

	//Various characters that can be sent, set by the modem's respective registers!
	byte escapecharacter;
	byte carriagereturncharacter;
	byte linefeedcharacter;
	byte backspacecharacter;
	DOUBLE escapecodeguardtime;

	//Allocated UART port
	byte port; //What port are we allocated to?
	
	//Line status for the different modem lines!
	byte canrecvdata; //Can we start receiving data to the UART?
	byte linechanges; //For detecting line changes!
	byte outputline; //Raw line that's output!
	byte outputlinechanges; //For detecting line changes!
	byte effectiveline; //Effective line to actually use!
	byte effectivelinechanges; //For detecting line changes!

	//What is our connection ID, if we're connected?
	sword connectionid; //Normal connection ID for the internal modem!

	//Command completion status!
	byte wascommandcompletionecho; //Was command completion with echo!
	DOUBLE wascommandcompletionechoTimeout; //Timeout for execution anyways!
	byte passthroughlinestatusdirty; //Passthrough mode line status dirty? Bit 0=DTR, bit 1=RTS, bit 2=Break
	byte passthroughescaped; //Was the last byte escaped?
	byte passthroughlines; //The actual lines that were received in passthrough mode!
	byte breakPending; //Is a break pending to be received on the receiver of the connection?
} modem;

byte readIPnumber(char **x, byte *number); //Prototype!

void initPacketServerClients()
{
	word index;
	PacketServer_clientp us;
	//Start with all clients unusable!
	//Allocate the clients in ascending order, which will appear reversed on the list. But the modem initialization will reverse that, fixing it.
	Packetserver_freeclients = NULL; //Nothing!
	Packetserver_allocatedclients = NULL; //Nothing!
	Packetserver_unusableclients = NULL; //Nothing!
	for (index = 0; index<NUMITEMS(Packetserver_clients); ++index) //process all indexes!
	{
		Packetserver_clients[index].used = 0; //We're in the free list!
		us = &Packetserver_clients[index]; //What entry are we?
		us->prev = NULL; //We start out as the head for the added items here, so never anything before us!
		us->next = NULL; //We start out as the head, so next is automatically filled!
		if (likely(Packetserver_unusableclients)) //Head already set?
		{
			Packetserver_unusableclients->prev = us; //We're the previous for the current head!
			us->next = Packetserver_unusableclients; //Our next is the head!
		}
		Packetserver_unusableclients = us; //We're the new head!
	}
}

void packetserver_moveListItem(PacketServer_clientp listitem, PacketServer_clientp* newlist_head, PacketServer_clientp* oldlist_head)
{
	//First, remove us from the old head list!
	if (listitem->prev) //Do we have anything before us?
	{
		((PacketServer_clientp)listitem->prev)->next = listitem->next; //Remove us from the previous item of the list!
	}
	else //We're the head, so remove us from the list!
	{
		*oldlist_head = listitem->next; //Remove us from the head of the list and assign the new head!
	}

	if (listitem->next) //Did we have a next item?
	{
		((PacketServer_clientp)listitem->next)->prev = listitem->prev; //Remove us from the next item of the list!
	}

	listitem->next = NULL; //We don't have a next!
	listitem->prev = NULL; //We don't have a previous!

	/* Now, we're removed from the old list and a newly unmapped item! */

	//Now, insert us into the start of the new list!
	if (*newlist_head) //Anything in the new list already?
	{
		(*newlist_head)->prev = listitem; //We're at the start of the new list, so point the head to us, us to the head and make us the new head!
		listitem->next = *newlist_head; //Our next is the old head!
		*newlist_head = listitem; //We're the new head!
	}
	else //We're the new list?
	{
		*newlist_head = listitem; //We're the new head!
	}
}

uint8_t maclocal_default[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x13, 0x37 }; //The MAC address of the modem we're emulating!

//Supported and enabled the packet setver?
#if defined(PACKETSERVER_ENABLED)
#ifndef _WIN32
#ifndef IS_LINUX
#ifndef NOPCAP
#define PCAP_OPENFLAG_PROMISCUOUS 1
#endif
#endif
#endif

byte pcap_loaded = 0; //Is WinPCap loaded?
byte dummy;
int_64 ethif = 0;
uint8_t pcap_enabled = 0;
byte pcap_receiverstate = 0;
uint8_t dopktrecv = 0;
uint16_t rcvseg, rcvoff, hdrlen, handpkt;

#if defined(PACKETSERVER_ENABLED) && !defined(NOPCAP)
pcap_if_t *alldevs;
pcap_if_t *d;
pcap_t *adhandle;
const u_char *pktdata;
struct pcap_pkthdr *hdr;
uint_32 pcaplength;
int_64 inum;
uint16_t curhandle = 0;
char errbuf[PCAP_ERRBUF_SIZE];
#endif
byte pcap_verbose = 0;

#ifdef WPCAP_WASNTDEFINED
#ifdef IS_WINDOWS
byte LoadNpcapDlls()
{
	_TCHAR npcap_dir[512];
	UINT len;
	len = GetSystemDirectory(npcap_dir, 480);
	if (!len) {
		return FALSE;
	}
	_tcscat_s(npcap_dir, 512, _T("\\Npcap"));
	if (SetDllDirectory(npcap_dir) == 0) {
		return FALSE;
	}
	return TRUE;
}
#endif
#endif

void initPcap() {
	memset(&net,0,sizeof(net)); //Init!
	int i=0;
	char *p;
	byte IPnumbers[4];

#ifdef WPCAP_WASNTDEFINED
#ifdef IS_WINDOWS
	dummy = LoadNpcapDlls(); //Try and load the npcap DLLs if present!
#endif
#endif

#ifdef _WIN32
	pcap_loaded = LoadPcapLibrary(); //Load the PCap library that's to be used!
#else
#if defined(IS_LINUX) && !defined(NOPCAP)
	pcap_loaded = 1; //pcap is always assumed loaded on Linux!
#endif
#endif

	/*

	Custom by superfury

	*/
	memset(&Packetserver_clients, 0, sizeof(Packetserver_clients)); //Initialize the clients!
	PacketServer_running = 0; //We're not using the packet server emulation, enable normal modem(we don't connect to other systems ourselves)!

#if defined(PACKETSERVER_ENABLED) && !defined(NOPCAP)
	if ((BIOS_Settings.ethernetserver_settings.ethernetcard==-1) || (BIOS_Settings.ethernetserver_settings.ethernetcard<-2)) //No ethernet card to emulate?
	{
		return; //Disable ethernet emulation!
	}
	ethif = BIOS_Settings.ethernetserver_settings.ethernetcard; //What ethernet card to use?
#endif

	//Load MAC address!
	int values[6];

#if defined(PACKETSERVER_ENABLED) && !defined(NOPCAP)
	if( 6 == sscanf( BIOS_Settings.ethernetserver_settings.MACaddress, "%02x:%02x:%02x:%02x:%02x:%02x%*c",
		&values[0], &values[1], &values[2],
		&values[3], &values[4], &values[5] ) ) //Found a MAC address to emulate?
	{
		/* convert to uint8_t */
		for( i = 0; i < 6; ++i )
			maclocal[i] = (uint8_t) values[i]; //MAC address parts!
	}
	else
	{
		memcpy(&maclocal,&maclocal_default,sizeof(maclocal)); //Copy the default MAC address to use!
	}
	if (ethif==-2) //Loopback mode?
	{
		memcpy(&packetserver_gatewayMAC,&maclocal,sizeof(maclocal)); //Send to ourselves!
	}
	else if( 6 == sscanf( BIOS_Settings.ethernetserver_settings.gatewayMACaddress, "%02x:%02x:%02x:%02x:%02x:%02x%*c",
		&values[0], &values[1], &values[2],
		&values[3], &values[4], &values[5] ) ) //Found a MAC address to emulate?
	{
		/* convert to uint8_t */
		for( i = 0; i < 6; ++i )
			packetserver_gatewayMAC[i] = (uint8_t) values[i]; //MAC address parts!
	}
	else
	{
		memset(&packetserver_gatewayMAC,0,sizeof(packetserver_gatewayMAC)); //Nothing!
		//We don't have the required addresses! Log and abort!
		dolog("ethernetcard", "Gateway MAC address is required on this platform! Aborting server installation!");
		return; //Disable ethernet emulation!
	}
#endif

	memcpy(&packetserver_sourceMAC,&maclocal,sizeof(packetserver_sourceMAC)); //Load sender MAC to become active!

	memset(&packetserver_defaultstaticIPstr, 0, sizeof(packetserver_defaultstaticIPstr));
	memset(&packetserver_defaultgatewayIPstr, 0, sizeof(packetserver_defaultgatewayIPstr));
	memset(&packetserver_DNS1IPstr, 0, sizeof(packetserver_DNS1IPstr));
	memset(&packetserver_DNS2IPstr, 0, sizeof(packetserver_DNS2IPstr));
	memset(&packetserver_NBNS1IPstr, 0, sizeof(packetserver_NBNS1IPstr));
	memset(&packetserver_NBNS2IPstr, 0, sizeof(packetserver_NBNS2IPstr));
	memset(&packetserver_subnetmaskIPstr, 0, sizeof(packetserver_subnetmaskIPstr));
	memset(&packetserver_hostsubnetmaskIPstr, 0, sizeof(packetserver_hostsubnetmaskIPstr));
	memset(&packetserver_defaultstaticIP, 0, sizeof(packetserver_defaultstaticIP));

	packetserver_usedefaultStaticIP = 0; //Default to unused!

	memset(&packetserver_defaultgatewayIPaddr, 0, sizeof(packetserver_defaultgatewayIPaddr));
	memset(&packetserver_defaultgatewayIPstr, 0, sizeof(packetserver_defaultgatewayIPstr));
	packetserver_defaultgatewayIP = 0; //No gateway IP!
	memset(&packetserver_DNS1IPaddr, 0, sizeof(packetserver_DNS1IPaddr));
	packetserver_DNS1IP = 0; //No gateway IP!
	memset(&packetserver_DNS2IPaddr, 0, sizeof(packetserver_DNS2IPaddr));
	packetserver_DNS2IP = 0; //No gateway IP!
	memset(&packetserver_NBNS1IPaddr, 0, sizeof(packetserver_NBNS1IPaddr));
	packetserver_NBNS1IP = 0; //No gateway IP!
	memset(&packetserver_NBNS2IPaddr, 0, sizeof(packetserver_NBNS2IPaddr));
	packetserver_NBNS2IP = 0; //No gateway IP!
	memset(&packetserver_subnetmaskIPaddr, ~0, sizeof(packetserver_subnetmaskIPaddr));
	packetserver_subnetmaskIPaddrd = ~0; //Unused!
	packetserver_subnetmaskIP = 0; //No gateway IP!
	memset(&packetserver_hostsubnetmaskIPaddr, ~0, sizeof(packetserver_hostsubnetmaskIPaddr));
	packetserver_hostsubnetmaskIPaddrd = ~0; //Unused!
	packetserver_hostsubnetmaskIP = 0; //No gateway IP!


#if defined(PACKETSERVER_ENABLED) && !defined(NOPCAP)
	if (safestrlen(&BIOS_Settings.ethernetserver_settings.hostIPaddress[0], 256) >= 12) //Valid length to convert IP addresses?
	{
		p = &BIOS_Settings.ethernetserver_settings.hostIPaddress[0]; //For scanning the IP!
		if (readIPnumber(&p, &IPnumbers[0]))
		{
			if (readIPnumber(&p, &IPnumbers[1]))
			{
				if (readIPnumber(&p, &IPnumbers[2]))
				{
					if (readIPnumber(&p, &IPnumbers[3]))
					{
						if (*p == '\0') //EOS?
						{
							//Automatic port?
							snprintf(packetserver_defaultstaticIPstr, sizeof(packetserver_defaultstaticIPstr), "%u.%u.%u.%u", IPnumbers[0], IPnumbers[1], IPnumbers[2], IPnumbers[3]); //Formulate the address!
							memcpy(&packetserver_defaultstaticIP, &IPnumbers, 4); //Set read IP!
							memcpy(&packetserver_hostIPaddrd, &IPnumbers, 4); //Set read IP!
							packetserver_usedefaultStaticIP = 1; //Static IP set!
						}
					}
				}
			}
		}
	}
	//Fallback to the first client's address!
	if ((safestrlen(&BIOS_Settings.ethernetserver_settings.users[0].IPaddress[0], 256) >= 12) && (!packetserver_usedefaultStaticIP)) //Valid length to convert IP addresses?
	{
		p = &BIOS_Settings.ethernetserver_settings.users[0].IPaddress[0]; //For scanning the IP!
		if (readIPnumber(&p, &IPnumbers[0]))
		{
			if (readIPnumber(&p, &IPnumbers[1]))
			{
				if (readIPnumber(&p, &IPnumbers[2]))
				{
					if (readIPnumber(&p, &IPnumbers[3]))
					{
						if (*p == '\0') //EOS?
						{
							//Automatic port?
							snprintf(packetserver_defaultstaticIPstr, sizeof(packetserver_defaultstaticIPstr), "%u.%u.%u.%u", IPnumbers[0], IPnumbers[1], IPnumbers[2], IPnumbers[3]); //Formulate the address!
							memcpy(&packetserver_defaultstaticIP, &IPnumbers, 4); //Set read IP!
							memcpy(&packetserver_hostIPaddrd, &IPnumbers, 4); //Set read IP!
							packetserver_usedefaultStaticIP = 1; //Static IP set!
						}
					}
				}
			}
		}
	}
	if (safestrlen(&BIOS_Settings.ethernetserver_settings.gatewayIPaddress[0], 256) >= 12) //Valid length to convert IP addresses?
	{
		p = &BIOS_Settings.ethernetserver_settings.gatewayIPaddress[0]; //For scanning the IP!
		if (readIPnumber(&p, &IPnumbers[0]))
		{
			if (readIPnumber(&p, &IPnumbers[1]))
			{
				if (readIPnumber(&p, &IPnumbers[2]))
				{
					if (readIPnumber(&p, &IPnumbers[3]))
					{
						if (*p == '\0') //EOS?
						{
							//Automatic port?
							snprintf(packetserver_defaultgatewayIPstr, sizeof(packetserver_defaultgatewayIPstr), "%u.%u.%u.%u", IPnumbers[0], IPnumbers[1], IPnumbers[2], IPnumbers[3]); //Formulate the address!
							memcpy(&packetserver_defaultgatewayIPaddr, &IPnumbers, 4); //Set read IP!
							memcpy(&packetserver_defaultgatewayIPaddrd, &IPnumbers, 4); //Set read IP!
							packetserver_defaultgatewayIP = 1; //Static IP set!
						}
					}
				}
			}
		}
	}
	if (safestrlen(&BIOS_Settings.ethernetserver_settings.DNS1IPaddress[0], 256) >= 12) //Valid length to convert IP addresses?
	{
		p = &BIOS_Settings.ethernetserver_settings.DNS1IPaddress[0]; //For scanning the IP!
		if (readIPnumber(&p, &IPnumbers[0]))
		{
			if (readIPnumber(&p, &IPnumbers[1]))
			{
				if (readIPnumber(&p, &IPnumbers[2]))
				{
					if (readIPnumber(&p, &IPnumbers[3]))
					{
						if (*p == '\0') //EOS?
						{
							//Automatic port?
							snprintf(packetserver_DNS1IPstr, sizeof(packetserver_DNS1IPstr), "%u.%u.%u.%u", IPnumbers[0], IPnumbers[1], IPnumbers[2], IPnumbers[3]); //Formulate the address!
							memcpy(&packetserver_DNS1IPaddr, &IPnumbers, 4); //Set read IP!
							packetserver_DNS1IP = 1; //Static IP set!
						}
					}
				}
			}
		}
	}
	if (safestrlen(&BIOS_Settings.ethernetserver_settings.DNS2IPaddress[0], 256) >= 12) //Valid length to convert IP addresses?
	{
		p = &BIOS_Settings.ethernetserver_settings.DNS2IPaddress[0]; //For scanning the IP!
		if (readIPnumber(&p, &IPnumbers[0]))
		{
			if (readIPnumber(&p, &IPnumbers[1]))
			{
				if (readIPnumber(&p, &IPnumbers[2]))
				{
					if (readIPnumber(&p, &IPnumbers[3]))
					{
						if (*p == '\0') //EOS?
						{
							//Automatic port?
							snprintf(packetserver_DNS2IPstr, sizeof(packetserver_DNS2IPstr), "%u.%u.%u.%u", IPnumbers[0], IPnumbers[1], IPnumbers[2], IPnumbers[3]); //Formulate the address!
							memcpy(&packetserver_DNS2IPaddr, &IPnumbers, 4); //Set read IP!
							packetserver_DNS2IP = 1; //Static IP set!
						}
					}
				}
			}
		}
	}
	if (safestrlen(&BIOS_Settings.ethernetserver_settings.NBNS1IPaddress[0], 256) >= 12) //Valid length to convert IP addresses?
	{
		p = &BIOS_Settings.ethernetserver_settings.NBNS1IPaddress[0]; //For scanning the IP!
		if (readIPnumber(&p, &IPnumbers[0]))
		{
			if (readIPnumber(&p, &IPnumbers[1]))
			{
				if (readIPnumber(&p, &IPnumbers[2]))
				{
					if (readIPnumber(&p, &IPnumbers[3]))
					{
						if (*p == '\0') //EOS?
						{
							//Automatic port?
							snprintf(packetserver_NBNS1IPstr, sizeof(packetserver_NBNS1IPstr), "%u.%u.%u.%u", IPnumbers[0], IPnumbers[1], IPnumbers[2], IPnumbers[3]); //Formulate the address!
							memcpy(&packetserver_NBNS1IPaddr, &IPnumbers, 4); //Set read IP!
							packetserver_NBNS1IP = 1; //Static IP set!
						}
					}
				}
			}
		}
	}
	if (safestrlen(&BIOS_Settings.ethernetserver_settings.NBNS2IPaddress[0], 256) >= 12) //Valid length to convert IP addresses?
	{
		p = &BIOS_Settings.ethernetserver_settings.NBNS2IPaddress[0]; //For scanning the IP!
		if (readIPnumber(&p, &IPnumbers[0]))
		{
			if (readIPnumber(&p, &IPnumbers[1]))
			{
				if (readIPnumber(&p, &IPnumbers[2]))
				{
					if (readIPnumber(&p, &IPnumbers[3]))
					{
						if (*p == '\0') //EOS?
						{
							//Automatic port?
							snprintf(packetserver_NBNS2IPstr, sizeof(packetserver_NBNS2IPstr), "%u.%u.%u.%u", IPnumbers[0], IPnumbers[1], IPnumbers[2], IPnumbers[3]); //Formulate the address!
							memcpy(&packetserver_NBNS2IPaddr, &IPnumbers, 4); //Set read IP!
							packetserver_NBNS2IP = 1; //Static IP set!
						}
					}
				}
			}
		}
	}


	if (safestrlen(&BIOS_Settings.ethernetserver_settings.subnetmaskIPaddress[0], 256) >= 12) //Valid length to convert IP addresses?
	{
		p = &BIOS_Settings.ethernetserver_settings.subnetmaskIPaddress[0]; //For scanning the IP!
		if (readIPnumber(&p, &IPnumbers[0]))
		{
			if (readIPnumber(&p, &IPnumbers[1]))
			{
				if (readIPnumber(&p, &IPnumbers[2]))
				{
					if (readIPnumber(&p, &IPnumbers[3]))
					{
						if (*p == '\0') //EOS?
						{
							//Automatic port?
							snprintf(packetserver_subnetmaskIPstr, sizeof(packetserver_subnetmaskIPstr), "%u.%u.%u.%u", IPnumbers[0], IPnumbers[1], IPnumbers[2], IPnumbers[3]); //Formulate the address!
							memcpy(&packetserver_subnetmaskIPaddr, &IPnumbers, 4); //Set read IP!
							memcpy(&packetserver_subnetmaskIPaddrd, &IPnumbers, 4); //Set read IP!
							packetserver_subnetmaskIP = 1; //Static IP set!
						}
					}
				}
			}
		}
	}

	if (safestrlen(&BIOS_Settings.ethernetserver_settings.hostsubnetmaskIPaddress[0], 256) >= 12) //Valid length to convert IP addresses?
	{
		p = &BIOS_Settings.ethernetserver_settings.hostsubnetmaskIPaddress[0]; //For scanning the IP!
		if (readIPnumber(&p, &IPnumbers[0]))
		{
			if (readIPnumber(&p, &IPnumbers[1]))
			{
				if (readIPnumber(&p, &IPnumbers[2]))
				{
					if (readIPnumber(&p, &IPnumbers[3]))
					{
						if (*p == '\0') //EOS?
						{
							//Automatic port?
							snprintf(packetserver_hostsubnetmaskIPstr, sizeof(packetserver_hostsubnetmaskIPstr), "%u.%u.%u.%u", IPnumbers[0], IPnumbers[1], IPnumbers[2], IPnumbers[3]); //Formulate the address!
							memcpy(&packetserver_hostsubnetmaskIPaddr, &IPnumbers, 4); //Set read IP!
							memcpy(&packetserver_hostsubnetmaskIPaddrd, &IPnumbers, 4); //Set read IP!
							packetserver_hostsubnetmaskIP = 1; //Static IP set!
						}
					}
				}
			}
		}
	}
#endif

	dolog("ethernetcard","Receiver MAC address: %02x:%02x:%02x:%02x:%02x:%02x",maclocal[0],maclocal[1],maclocal[2],maclocal[3],maclocal[4],maclocal[5]);
	dolog("ethernetcard","Gateway MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",packetserver_gatewayMAC[0],packetserver_gatewayMAC[1],packetserver_gatewayMAC[2],packetserver_gatewayMAC[3],packetserver_gatewayMAC[4],packetserver_gatewayMAC[5]); //Log loaded address!
	if (packetserver_usedefaultStaticIP) //Static IP configured?
	{
		dolog("ethernetcard","Static IP configured: %s(%02x%02x%02x%02x)",packetserver_defaultstaticIPstr,packetserver_defaultstaticIP[0],packetserver_defaultstaticIP[1],packetserver_defaultstaticIP[2],packetserver_defaultstaticIP[3]); //Log it!
	}
	if (packetserver_defaultgatewayIP) //Static IP configured?
	{
		dolog("ethernetcard", "Default Gateway IP configured: %s(%02x%02x%02x%02x)", packetserver_defaultgatewayIPstr, packetserver_defaultgatewayIPaddr[0], packetserver_defaultgatewayIPaddr[1], packetserver_defaultgatewayIPaddr[2], packetserver_defaultgatewayIPaddr[3]); //Log it!
	}

	for (i = 0; i < NUMITEMS(Packetserver_clients); ++i) //Initialize client data!
	{		
		Packetserver_clients[i].packetserver_transmitlength = 0; //We're at the start of this buffer, nothing is sent yet!
	}

	/*

	End of custom!

	*/

	i = 0; //Init!

	if (!pcap_loaded) //PCap isn't loaded?
	{
		dolog("ethernetcard", "The pcap interface and public packet server is disabled because the required libraries aren't installed!");
		pcap_enabled = 0;
		if (BIOS_Settings.ethernetserver_settings.ethernetcard == -2) //Special loopback mode?
		{
			pcap_enabled = 2; //Special loopback mode instead!
			dolog("ethernetcard", "The packet server is running in loopback mode.");
			PacketServer_running = 1; //We're using the packet server emulation, disable normal modem(we don't connect to other systems ourselves)!
		}
		else
		{
			if (BIOS_Settings.ethernetserver_settings.ethernetcard > 0) //Required to operate according to the settings?
			{
				GPU_messagebox(NULL,MESSAGEBOX_WARNING, "The pcap interface and public packet server is disabled because the required libraries aren't installed!");
			}
			PacketServer_running = 0; //We're using the packet server emulation, disable normal modem(we don't connect to other systems ourselves)!
		}
		pcap_receiverstate = 0; //Packet receiver/filter state: ready to receive a packet!
		return; //Abort!
	}

	if (BIOS_Settings.ethernetserver_settings.ethernetcard != -2) //Not special loopback mode?
	{
	dolog("ethernetcard","Obtaining NIC list via libpcap...");

#if defined(PACKETSERVER_ENABLED) && !defined(NOPCAP)
	/* Retrieve the device list from the local machine */
#if defined(_WIN32)
#ifdef WPCAP
	//Winpcap version!
	if (pcap_findalldevs_ex(PCAP_SRC_IF_STRING, NULL /* auth is not needed */, &alldevs, errbuf) == -1)
#else
	if (pcap_findalldevs (&alldevs, errbuf))
#endif
#else
	if (pcap_findalldevs (&alldevs, errbuf))
#endif
		{
			dolog("ethernetcard","Error in pcap_findalldevs_ex: %s", errbuf);
			GPU_messagebox(NULL, MESSAGEBOX_ERROR, "Error in pcap_findalldevs_ex: %s", errbuf);
			exit (1);
		}

	/* Print the list */
	for (d= alldevs; d != NULL; d= d->next) {
			i++;
			if (ethif==0) {
					dolog("ethernetcard","%d. %s", i, d->name);
					if (d->description) {
							dolog("ethernetcard"," (%s)", d->description);
						}
					else {
							dolog("ethernetcard"," (No description available)");
						}
				}
		}

	if (i == 0) {
			dolog("ethernetcard","No interfaces found! Make sure WinPcap is installed.");
			GPU_messagebox(NULL, MESSAGEBOX_WARNING, "No interfaces found! Make sure WinPcap is installed.");
			return;
		}

	if (ethif == 0)
	{
		GPU_messagebox(NULL, MESSAGEBOX_WARNING, "Interfaces logged. Terminating app.");
		exit(0); //Failed: no ethernet card to use: only performing detection!
	}
	else inum = ethif;
	dolog("ethernetcard","Using network interface %u.", ethif);


	if (inum < 1 || inum > i) {
			dolog("ethernetcard","Interface number out of range.");
			GPU_messagebox(NULL, MESSAGEBOX_WARNING, "Interface number out of range.");
			/* Free the device list */
			pcap_freealldevs (alldevs);
			return;
		}

	/* Jump to the selected adapter */
	for (d=alldevs, i=0; ((i< inum-1) && d) ; d=d->next, i++);

	/* Open the device */
#ifdef _WIN32
#ifdef WPCAP
	//Winpcap version!
	if ((adhandle = pcap_open(d->name, 65536, PCAP_OPENFLAG_PROMISCUOUS, -1, NULL, errbuf)) == NULL)
#else
	if ((adhandle = pcap_open_live(d->name, 65535, 1, -1, NULL)) == NULL)
#endif
#else
	if ( (adhandle= pcap_open_live (d->name, 65535, PCAP_OPENFLAG_PROMISCUOUS, -1, errbuf) ) == NULL)
#endif
		{
			dolog("ethernetcard","Unable to open the adapter. \"%s\" is not supported by WinPcap. Reason: %s", d->name, errbuf);
			GPU_messagebox(NULL, MESSAGEBOX_ERROR, "Unable to open the adapter. \"%s\" is not supported by WinPcap. Reason: %s", d->name, errbuf);
			/* Free the device list */
			pcap_freealldevs (alldevs);
			exit(1);
			return;
		}

	dolog("ethernetcard","Ethernet bridge on %s (%s)...", d->name, d->description?d->description:"No description available");

	if (BIOS_Settings.ethernetserver_settings.ethernetcard != -2) //Not loopback mode?
	{
		if (pcap_datalink(adhandle) != DLT_EN10MB) //Invalid link layer?
		{
			dolog("ethernetcard", "Ethernet card \"%s\" is unsupported!", d->description ? d->description : "No description available");
			GPU_messagebox(NULL, MESSAGEBOX_WARNING, "Ethernet card \"%s\" is unsupported!", d->description ? d->description : "No description available");
			/* Free the device list */
			pcap_freealldevs(alldevs);
			pcap_close(adhandle); //Close the handle!
			return;
		}
	}

	/* At this point, we don't need any more the device list. Free it */
	pcap_freealldevs (alldevs);
	pcap_enabled = 1; //Normal mode!
#endif
	} //pcap enabled?
	else
	{
		dolog("ethernetcard", "The packet server is running in loopback mode.");
		pcap_enabled = 2; //Loopback mode!
	}
	PacketServer_running = 1; //We're using the packet server emulation, disable normal modem(we don't connect to other systems ourselves)!
	pcap_receiverstate = 0; //Packet receiver/filter state: ready to receive a packet!
}

byte pcap_capture = 0; //A flag asking for the pcap to quit!
void fetchpackets_pcap() { //Handle any packets to process!
#if defined(PACKETSERVER_ENABLED) && !defined(NOPCAP)
	//Filter parameters to apply!
	PacketServer_clientp connectedclient;
	ETHERNETHEADER ethernetheader; //The header to inspect!
	word headertype;
	ARPpackettype ARPpacket, ARPresponse; //For analyzing and responding to ARP requests!
	byte skippacket; //Skipping the packet as unusable?
	ETHERNETHEADER ppptransmitheader;
	byte *arppacketc;

	union
	{
		uint_32 addressnetworkorder32;
		byte addressnetworkorderb[4];
	} ARPIP;
	byte ARPether[6]; //Ethernet address!

	if (pcap_enabled) //Enabled?
	{
		lock(LOCK_PCAPFLAG);
		for (; (!shuttingdown()) && (pcap_capture);) //Keep looping until exit is detected!
		{
			unlock(LOCK_PCAPFLAG);
			//Check for new packets arriving and filter them as needed!
			if (pcap_receiverstate == 0) //Ready to receive a new packet?
			{
			invalidpacket_receivefilter:
				if (pcap_enabled != 2) //Not loopback mode?
				{
					if (pcap_next_ex(adhandle, &hdr, &pktdata) <= 0) goto trynexttime; //Nothing valid to process?
					if (hdr->len == 0) goto invalidpacket_receivefilter; //Try again on invalid 
					pcaplength = hdr->len; //The length!
				}
				else //Loopback mode?
				{
					lock(LOCK_PCAP);
					if (!(loopback.packet && loopback.pktlen)) //Not allocated?
					{
						unlock(LOCK_PCAP);
						goto trynexttime; //Wait for a packet to appear on the loopback!
					}
					pktdata = loopback.packet; //For easy handling below!
					pcaplength = loopback.pktlen; //It's length!
					unlock(LOCK_PCAP);
				}
				//Packet received!
				memcpy(&ethernetheader.data, &pktdata[0], sizeof(ethernetheader.data)); //Copy to the client buffer for inspection!
				//Check for the packet type first! Don't receive anything that is our unsupported (the connected client)!
				if (ethernetheader.type != SDL_SwapBE16(0x0800)) //Not IP packet?
				{
					if (ethernetheader.type != SDL_SwapBE16(0x8863)) //Are we not a discovery packet?
					{
						if (ethernetheader.type != SDL_SwapBE16(0x8864)) //Not Receiving uses normal PPP packets to transfer/receive on the receiver line only!
						{
							if (ethernetheader.type != SDL_SwapBE16(0x8864)) //Not Receiving uses normal PPP packets to transfer/receive on the receiver line only!
							{
								if (ethernetheader.type != SDL_SwapBE16(0x8137)) //Not an IPX packet!
								{
									if (ethernetheader.type != SDL_SwapBE16(0x0806)) //Not ARP?
									{
										//This is an unsupported packet type discard it fully and don't look at it anymore!
										//Discard the received packet, so nobody else handles it too!
										goto invalidpacket_receivefilter; //Ignore this packet and check for more!
									}
								}
							}
						}
					}
				}
				waitforclientready: //Wait for all clients to become ready to receive!
				lock(LOCK_PCAP); //Make sure that we don't conflict with the receiver!
				skippacket = 1; //Default: skip the packet!
				headertype = ethernetheader.type; //What packet type is used?
				for (connectedclient = Packetserver_allocatedclients; connectedclient; connectedclient = connectedclient->next) //Parse all possible clients to receive it!
				{
					//Perform the same logic as the main thread, checking if a packet is to be received properly!

					//Check for the client first! Don't receive anything that is our own traffic (the connected client)!
					//Next, check for supported packet types!
					if (connectedclient->packetserver_slipprotocol == 3) //PPP protocol used?
					{
						if (ethernetheader.type == SDL_SwapBE16(0x8863)) //Are we a discovery packet?
						{
							if (connectedclient->packetserver_slipprotocol_pppoe) //Using PPPOE?
							{
								skippacket = 0; //Handle it!
								goto skippacketfinished;
							}
							//Using PPP, ignore the header type and parse this later!
						}
					}
					headertype = ethernetheader.type; //The requested header type!
					//Now, check the normal receive parameters!
					if (connectedclient->packetserver_useStaticIP && (headertype == SDL_SwapBE16(0x0800)) && (((connectedclient->packetserver_slipprotocol == 1)) || ((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe) && (connectedclient->ppp_IPCPstatus[PPP_RECVCONF])))) //IP filter to apply for IPv4 connections and PPPOE connections?
					{
						if ((memcmp(&pktdata[sizeof(ethernetheader.data) + 16], &connectedclient->packetserver_staticIP, 4) != 0) && (memcmp(&pktdata[sizeof(ethernetheader.data) + 16], &packetserver_broadcastIP, 4) != 0)) //Static IP mismatch?
						{
							continue; //Invalid packet!
						}
					}
					if ((memcmp(&ethernetheader.dst, &packetserver_sourceMAC, sizeof(ethernetheader.dst)) != 0) && (memcmp(&ethernetheader.dst, &packetserver_broadcastMAC, sizeof(ethernetheader.dst)) != 0)) //Invalid destination(and not broadcasting)?
					{
						continue; //Invalid packet!
					}
					if (connectedclient->packetserver_slipprotocol == 3) //PPP protocol used?
					{
						if (ethernetheader.type == SDL_SwapBE16(0x8863)) //Are we a discovery packet?
						{
							if (connectedclient->packetserver_slipprotocol_pppoe) //PPPOE?
							{
								skippacket = 0; //Handle it!
								goto skippacketfinished;
							}
							else
							{
								continue; //Invalid for us!
							}
						}
						else if ((headertype == SDL_SwapBE16(0x8864)) && connectedclient->packetserver_slipprotocol_pppoe) //Receiving uses normal PPP packets to transfer/receive on the receiver line only!
						{
							skippacket = 0; //Handle it!
							goto skippacketfinished;
						}
						else if (headertype == SDL_SwapBE16(0x8864)) //Invalid for PPP?
						{
							continue; //Invalid for us!
						}
						if (!connectedclient->packetserver_slipprotocol_pppoe) //PPP requires extra filtering?
						{
							if (headertype == SDL_SwapBE16(0x0800)) //IPv4?
							{
								if (!connectedclient->ppp_IPCPstatus[PPP_RECVCONF]) //IPv4 not used on PPP?
								{
									continue; //Invalid for us!
								}
							}
							else if (headertype == SDL_SwapBE16(0x0806)) //ARP?
							{
								if (!IPCP_OPEN) //IPv4 not used on PPP?
								{
									continue; //Invalid for us!
								}
							}
							else if (headertype == SDL_SwapBE16(0x8137)) //We're an IPX packet?
							{
								if (!IPXCP_OPEN) //IPX not used on PPP?
								{
									continue; //Invalid for us!
								}
							}
							else //Unknown packet type?
							{
								continue; //Invalid for us!
							}
						}
					}
					else if (connectedclient->packetserver_slipprotocol == 2) //IPX protocol used?
					{
						if (headertype != SDL_SwapBE16(0x8137)) //We're an IPX packet!
						{
							continue; //Invalid for us!
						}
					}
					else //IPv4?
					{
						if ((headertype != SDL_SwapBE16(0x0800)) && (headertype!=SDL_SwapBE16(0x0806))) //We're an IP or ARP packet!
						{
							continue; //Invalid for us!
						}
					}

					if (connectedclient->packetserver_stage != PACKETSTAGE_PACKETS) continue; //Don't handle SLIP/PPP/IPX yet!
					if (ethernetheader.type == SDL_SwapBE16(0x0806)) //ARP?
					{
						if ((connectedclient->packetserver_slipprotocol == 1) || //IPv4 used?
							((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe) && IPCP_OPEN) //IPv4 used on PPP?
							) //IPv4 protocol used?
						{
							//Always handle ARP packets, if we're IPv4 type!
							if (pcaplength < (28 + 0xE)) //Unsupported length?
							{
								continue; //Invalid packet!
							}
							//TODO: Check if it's a request for us. If so, reply with our IPv4 address!
							memcpy(&ARPpacket, &pktdata[0xE], 28); //Retrieve the ARP packet, if compatible!
							memcpy(&ARPIP.addressnetworkorderb, &ARPpacket.TPA, 4); //What is requested?
							memcpy(&ARPether, &ARPpacket.THA, 6); //Who is requested for responses?
							//ARPIP.addressnetworkorder32 = SDL_Swap32(ARPIP.addressnetworkorder32); //Make sure it's in our supported format!
							if (connectedclient->ARPrequeststatus==1) //Anything requested?
							{
								if ((SDL_SwapBE16(ARPpacket.htype) == 1) && (ARPpacket.ptype == SDL_SwapBE16(0x0800)) && (ARPpacket.hlen == 6) && (ARPpacket.plen == 4) && (SDL_SwapBE16(ARPpacket.oper) == 2)) //Valid reply received?
								{
									//The packet is reversed from the request packet, but instead of the MAC address that was sent to being 0, it's now filled in the source of the ARP.
									memcpy(&ARPIP.addressnetworkorderb, &ARPpacket.SPA, 4); //What is requested?
									if (
										(memcmp(&ARPIP.addressnetworkorderb, &connectedclient->ARPrequestIP, 4) == 0) && //Matching source IP (the IP that's requested)
										(memcmp(&ARPether, &maclocal, 6)==0) && //Matched destination MAC is for us?
										(memcmp(&ARPpacket.TPA, ((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe) && IPCP_OPEN) ? &connectedclient->ipcp_ipaddress[PPP_RECVCONF][0] : &connectedclient->packetserver_staticIP[0],4)==0) //Target is our client's IP?
										)
										//Match found?
									{
										memcpy(&connectedclient->ARPrequestresult, &ARPpacket.SHA, 6); //Where to send: the ARP MAC address!
										connectedclient->ARPrequeststatus = 2; //Result gotten!
										getnspassed(&connectedclient->ARPtimer); //Reset the timer to now count cached time instead of waiting for response time!
										continue; //Accept the packet. Continue searcihng for other clients though.
									}
									else
									{
										continue;
									}
								}
								else
								{
									continue;
								}
							}
							if ((SDL_SwapBE16(ARPpacket.htype) == 1) && (ARPpacket.ptype == SDL_SwapBE16(0x0800)) && (ARPpacket.hlen == 6) && (ARPpacket.plen == 4) && (SDL_SwapBE16(ARPpacket.oper) == 1))
							{
								//IPv4 ARP request
								//Check it's our IP, send a response if it's us!
								if (connectedclient->packetserver_useStaticIP) //IP filter is used?
								{
									if (memcmp(&ARPIP.addressnetworkorderb, ((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe) && IPCP_OPEN) ? &connectedclient->ipcp_ipaddress[PPP_RECVCONF][0] : &connectedclient->packetserver_staticIP[0], 4) != 0) //Static IP mismatch?
									{
										continue; //Invalid packet!
									}
									arppacketc = zalloc(pcaplength,"MODEM_PACKET",NULL); //Allocate a reply of the very same length!
									if (arppacketc==NULL) continue; //Skip if unable!
									//It's for us, send a response!
									//Construct the ARP reply packet!
									ARPresponse.htype = ARPpacket.htype;
									ARPresponse.ptype = ARPpacket.ptype;
									ARPresponse.hlen = ARPpacket.hlen;
									ARPresponse.plen = ARPpacket.plen;
									ARPresponse.oper = SDL_SwapBE16(2); //Reply!
									memcpy(&ARPresponse.THA, &ARPpacket.SHA, 6); //To the originator!
									memcpy(&ARPresponse.TPA, &ARPpacket.SPA, 4); //Destination IP!
									memcpy(&ARPresponse.SHA, &maclocal, 6); //Our MAC address!
									memcpy(&ARPresponse.SPA, &ARPpacket.TPA, 4); //Our IP!
									//Construct the ethernet header!
									memcpy(&arppacketc[0xE], &ARPresponse, 28); //Paste the response in the packet we're handling (reuse space)!
									//Make sure that the room in between the ARP response and ethernet header stays zeroed.
									//Now, construct the ethernet header!
									memcpy(&ppptransmitheader, &ethernetheader, sizeof(ethernetheader.data)); //Copy the header!
									memcpy(&ppptransmitheader.src, &maclocal, 6); //From us!
									memcpy(&ppptransmitheader.dst, &ARPpacket.SHA, 6); //To the requester!
									memcpy(&arppacketc[0], ppptransmitheader.data, 0xE); //The ethernet header!
									//Now, the packet we've stored has become the packet to send back!
									pcap_sendpacket(adhandle, arppacketc, pcaplength); //Send the ARP response now!
									freez((void **)&arppacketc,pcaplength,"MODEM_PACKET"); //Free it!
									arppacketc = NULL;
									unlock(LOCK_PCAP);
									skippacket = 1; //Skip it!
									goto skippacketfinished; //Stop searching!
								}
								else
								{
									//Unsupported ARP!
									continue; //Invalid for our use, discard it!
								}
							}
							else
							{

								continue; //Invalid for our use, discard it!
							}
						}
						else
						{
							continue; //Invalid for our use, discard it!
						}
					}
					//Valid packet! Receive it!
					if ((ethernetheader.type==SDL_SwapBE16(0x0800))?connectedclient->IPpacket:connectedclient->packet) //Client isn't ready to receive it?
					{
						unlock(LOCK_PCAP);
						goto waitforclientready; //Wait for the client to become ready!
					}
					skippacket = 0; //Receive it!
					//Continue scanning for other clients to become ready to receive!
				}
				skippacketfinished:
				if (skippacket) //To skip receiving it?
				{
					if (pcap_enabled == 2) //Loopback mode?
					{
						freez((void **)&loopback.packet, loopback.pktlen, "LOOPBACK_PACKET"); //Free the loopback packet!
						loopback.packet = NULL;
						loopback.pktlen = 0; //Freed!
					}
					unlock(LOCK_PCAP);
					goto invalidpacket_receivefilter; //Fetch next packet(s)!
				}
				unlock(LOCK_PCAP);
				//Valid packet! Receive it!
				//Packet ready to receive!
				pcap_receiverstate = 1; //Packet is loaded and ready to parse by the receiver algorithm!
			}

			lock(LOCK_PCAP);
			//Try and receive the packet!
			if ((((ethernetheader.type==SDL_SwapBE16(0x0800))?IPnet.packet:net.packet) == NULL) && (pcap_receiverstate == 1)) //Can we receive anything and receiver is loaded?
			{
				//Packet acnowledged for clients to receive!
				if (ethernetheader.type==SDL_SwapBE16(0x0800)) //IPv4?
				{
					IPnet.packet = zalloc(pcaplength, "MODEM_PACKET", NULL);
				}
				else //Other?
				{
					net.packet = zalloc(pcaplength, "MODEM_PACKET", NULL);
				}
				if ((ethernetheader.type==SDL_SwapBE16(0x0800))?IPnet.packet:net.packet) //Allocated?
				{
					if (ethernetheader.type==SDL_SwapBE16(0x0800)) //IPv4?
					{
						memcpy(IPnet.packet, &pktdata[0], pcaplength);
						IPnet.pktlen = pcaplength;
					}
					else //Other?
					{
						memcpy(net.packet, &pktdata[0], pcaplength);
						net.pktlen = pcaplength;
					}
					if (pcap_verbose) {
						dolog("ethernetcard", "Received packet of %u bytes.", net.pktlen);
					}
					if (pcap_enabled == 2) //Loopback mode?
					{
						freez((void **)&loopback.packet, loopback.pktlen, "LOOPBACK_PACKET"); //Free the loopback packet!
						loopback.packet = NULL; //Not allocated anymore!
						loopback.pktlen = 0; //Freed!
					}
					pcaplength = 0;
					//Packet received!
					pcap_receiverstate = 0; //Start scanning for incoming packets again, since the receiver is cleared again!
				}
			}
			unlock(LOCK_PCAP);
			trynexttime: //Try the next time?
			lock(LOCK_PCAPFLAG); //Start to use the flag!
		}
		unlock(LOCK_PCAPFLAG);
	}
#endif
}

byte sendpkt_pcap(PacketServer_clientp connectedclient, uint8_t* src, uint16_t len) {
#if defined(PACKETSERVER_ENABLED) && !defined(NOPCAP)
	ETHERNETHEADER ethernetheader; //The header to inspect!
	ARPpackettype ARPresponse; //For analyzing and responding to ARP requests!
	ETHERNETHEADER ppptransmitheader;
	uint_32 dstip;
	uint_32 srcip;
	byte* packet;
	byte *ourip;
	byte macrequest[6]; //Dummy NULL field to request ARP!
	byte *arppacketc;
	if (pcap_enabled) //Enabled?
	{
		if (pcap_enabled == 2) //Loopback?
		{
			if (len) //Valid length?
			{
				lock(LOCK_PCAP);
				if (loopback.packet && loopback.pktlen) //Something is still pending?
				{
					unlock(LOCK_PCAP);
					return 0; //Failed!
				}
				packet = zalloc(len, "LOOPBACK_PACKET", NULL); //Allocate!
				if (!packet) //Failed?
				{
					unlock(LOCK_PCAP);
					return 0; //Failed!
				}
				memcpy(packet, src, len); //Set the contents of the packet!
				loopback.packet = packet; //The packet to use!
				loopback.pktlen = len; //The length!
				unlock(LOCK_PCAP);
			}
		}
		else //Normal sending?
		{
			memcpy(&ethernetheader, src, 0xE); //Set the header!
			if (ethernetheader.type == SDL_SwapBE16(0x0800)) //IP?
			{
				if (memcmp(&ethernetheader.dst, &packetserver_broadcastMAC, 6) != 0) //Not broadcasting IP?
				{
					//We can assume it's addressed to the default gateway in this case.
					if (len >= (0xE + 16 + 4)) //Long enough to check?
					{
						memcpy(&srcip,((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe) && IPCP_OPEN) ? &connectedclient->ipcp_ipaddress[PPP_RECVCONF][0] : &connectedclient->packetserver_staticIP[0], 4); //The clients own IP address!
						memcpy(&dstip, &src[sizeof(ethernetheader.data) + 16], 4); //The IP address!
						if (((dstip&connectedclient->ipcp_subnetmaskipaddressd)==(srcip&connectedclient->ipcp_subnetmaskipaddressd)) && srcip) //Local network destination?
						{
							memcpy(src, &maclocal, 6); //Send to ourselves for now!
						}
						//Otherwise, it's meant for the default gateway. Then determine if it's for the local host network.
						else if (((dstip&packetserver_hostsubnetmaskIPaddrd)==(packetserver_hostIPaddrd&packetserver_hostsubnetmaskIPaddrd)) && packetserver_hostIPaddrd) //Host network destination?
						{
							handledefaultgateway:
							lock(LOCK_PCAP);
							//TODO: Send ARP to network with client timeout(=failure) when not sending to ourselves.
							if (connectedclient->ARPrequeststatus) //Anything requested?
							{
								if (connectedclient->ARPrequeststatus==2) //Finished?
								{
									if (memcmp(&dstip,&connectedclient->ARPrequestIP,4)==0) //Match found?
									{
										if (getnspassed_k(&connectedclient->ARPtimer)>=30000000000.0f) //Timeout 30 seconds?
										{
											connectedclient->ARPrequeststatus = 0; //Timeout the cache itself!
											goto startnewARPrequest; //Refresh!
										}
										memcpy(src,&connectedclient->ARPrequestresult,6); //Where to send: the ARP MAC address!
										//Don't clear the request status: perform this like a buffering of most recently resulted MAC address!
									}
									else //For other request was last received? Try again until we get a valid result!
									{
										goto startnewARPrequest;
									}
								}
								else //Pending?
								{
									if (getnspassed_k(&connectedclient->ARPtimer)>=500000000.0f) //Timeout 500ms?
									{
										connectedclient->ARPrequeststatus = 0; //Stop waiting for it!
										unlock(LOCK_PCAP);
										memcpy(src,&packetserver_gatewayMAC,6); //ARP failed! Send to the default gateway instead!
									}
								}
							}
							else //New request?
							{
								startnewARPrequest:
								initTicksHolder(&connectedclient->ARPtimer);
								getnspassed(&connectedclient->ARPtimer); //Start timing!
								//Send a request!
								memcpy(&connectedclient->ARPrequestIP,&dstip,4); //What ARP reply to wait for!
								connectedclient->ARPrequeststatus = 1; //Start waiting for it!
								ourip = ((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe) && IPCP_OPEN) ? &connectedclient->ipcp_ipaddress[PPP_RECVCONF][0] : &connectedclient->packetserver_staticIP[0]; //Our IP to use!
								arppacketc = zalloc(42,"MODEM_PACKET",NULL); //Allocate a reply of the very same length!
								if (arppacketc==NULL) //Skip if unable!
								{
									unlock(LOCK_PCAP);
									return 0; //Pending!
								}
								//It's for us, send a response!
								//Construct the ARP reply packet!
								ARPresponse.htype = SDL_SwapBE16(1);
								ARPresponse.ptype = SDL_SwapBE16(0x0800);
								ARPresponse.hlen = 6;
								ARPresponse.plen = 4;
								ARPresponse.oper = SDL_SwapBE16(1); //Request!
								memset(&macrequest,0,sizeof(macrequest)); //Requesting this: zeroed!
								memcpy(&ARPresponse.THA, &macrequest, 6); //To the broadcast MAC!
								memcpy(&ARPresponse.TPA, &dstip, 4); //Destination IP!
								memcpy(&ARPresponse.SHA, &maclocal, 6); //Our MAC address!
								memcpy(&ARPresponse.SPA, ourip, 4); //Our IP!
								//Construct the ethernet header!
								memcpy(&arppacketc[0xE], &ARPresponse, 28); //Paste the response in the packet we're handling (reuse space)!
								//Make sure that the room in between the ARP response and ethernet header stays zeroed.
								//Now, construct the ethernet header!
								memcpy(&ppptransmitheader.src, &maclocal, 6); //From us!
								memcpy(&ppptransmitheader.dst, &packetserver_broadcastMAC, 6); //A broadcast!
								ppptransmitheader.type = SDL_SwapBE16(0x0806); //ARP!
								memcpy(&arppacketc[0], ppptransmitheader.data, 0xE); //The ethernet header!
								//Now, the packet we've stored has become the packet to send back!
								pcap_sendpacket(adhandle, arppacketc, 42); //Send the ARP response now!
								freez((void **)&arppacketc,60,"MODEM_PACKET"); //Free it!
								unlock(LOCK_PCAP);
								return 0; //Pending!
							}
							unlock(LOCK_PCAP);
						}
						else //Use the default gateway on the host network?
						{
							if (packetserver_defaultgatewayIP) //Gotten a default gateway set?
							{
								dstip = packetserver_defaultgatewayIPaddrd;
								goto handledefaultgateway; //Handle the ARP to the default gateway!
							}
						}
					}
				}
			}
			pcap_sendpacket(adhandle, src, len);
		}
	}
#endif
	return 1; //Default: success!
}

void termPcap()
{
	word clientnumber;
	lock(LOCK_PCAP);
	if (net.packet)
	{
		freez((void **)&net.packet,net.pktlen,"MODEM_PACKET"); //Cleanup!
	}
	if (IPnet.packet)
	{
		freez((void **)&IPnet.packet,IPnet.pktlen,"MODEM_PACKET"); //Cleanup!
	}
	unlock(LOCK_PCAP);
	PacketServer_clientp client;
	for (clientnumber=0;clientnumber<NUMITEMS(Packetserver_clients);++clientnumber) //Process all clients!
	{
		client = &Packetserver_clients[clientnumber]; //What to use!
		if (client->packet)
		{
			freez((void **)&client->packet, client->pktlen, "SERVER_PACKET"); //Cleanup!
		}
		if (client->IPpacket)
		{
			freez((void**)&client->IPpacket, client->IPpktlen, "SERVER_PACKET"); //Cleanup!
		}
		if (client->packetserver_transmitbuffer && client->packetserver_transmitsize) //Gotten a send buffer allocated?
		{
			freez((void **)&client->packetserver_transmitbuffer, client->packetserver_transmitsize, "MODEM_SENDPACKET"); //Clear the transmit buffer!
			if (client->packetserver_transmitbuffer == NULL) client->packetserver_transmitsize = 0; //Nothing allocated anymore!
		}
	}
#if defined(PACKETSERVER_ENABLED) && !defined(NOPCAP)
	if (pcap_enabled==1)
	{
		pcap_close(adhandle); //Close the capture/transmit device!
	}
#endif
}
#else
//Not supported?
void initPcap() //Unsupported!
{
	memset(&net,0,sizeof(net)); //Init!
}
byte sendpkt_pcap(PacketServer_clientp connectedclient, uint8_t *src, uint16_t len)
{
	return 1; //Success!
}
void fetchpackets_pcap() //Handle any packets to process!
{
}
void termPcap()
{
}
#endif

PacketServer_clientp allocPacketserver_client()
{
	PacketServer_clientp result;
	if (!Packetserver_freeclients) return NULL; //None available!
	result = Packetserver_freeclients; //What to use!
	lock(LOCK_PCAP); //Start locking to prevent mixing!
	packetserver_moveListItem(result, &Packetserver_allocatedclients, &Packetserver_freeclients); //Allocate it now!
	result->used = 1; //We're used now!
	return result; //Give the client!
}

byte freePacketserver_client(PacketServer_clientp client)
{
	if (client->used) //Used?
	{
		client->used = 0; //Not used anymore!
		packetserver_moveListItem(client, &Packetserver_freeclients, &Packetserver_allocatedclients); //One client became available!
		return 1; //Success!
	}
	return 0; //Failure!
}

void packetServerFreePacketBufferQueue(MODEM_PACKETBUFFER* buffer); //Prototype for freeing of DHCP when not connected!

void normalFreeDHCP(PacketServer_clientp connectedclient)
{
	packetServerFreePacketBufferQueue(&connectedclient->DHCP_discoverypacket); //Free the old one first, if present!
	packetServerFreePacketBufferQueue(&connectedclient->DHCP_offerpacket); //Free the old one first, if present!
	packetServerFreePacketBufferQueue(&connectedclient->DHCP_requestpacket); //Free the old one first, if present!
}

void terminatePacketServer(PacketServer_clientp client) //Cleanup the packet server after being disconnected!
{
	fifobuffer_clear(modem.blockoutputbuffer[client->connectionnumber]); //Clear the receive buffer!
	freez((void **)&client->packetserver_transmitbuffer,client->packetserver_transmitsize,"MODEM_SENDPACKET"); //Clear the transmit buffer!
	if (client->packetserver_transmitbuffer==NULL) client->packetserver_transmitsize = 0; //Clear!
}

void PacketServer_startNextStage(PacketServer_clientp connectedclient, byte stage)
{
	connectedclient->packetserver_stage_byte = PACKETSTAGE_INITIALIZING; //Prepare for next step!
	connectedclient->packetserver_stage = stage; //The specified stage that's starting!
}

void initPacketServer(PacketServer_clientp client) //Initialize the packet server for use when connected to!
{
#if defined(PACKETSERVER_ENABLED) && !defined(NOPCAP)
	word c;
#endif
	terminatePacketServer(client); //First, make sure we're terminated properly!
	client->ppp_autodetected = 0; //PPP isn't detected yet!
	client->ppp_autodetectpos = 0; //PPP isn't detected yet!
	client->packetserver_transmitsize = 1024; //Initialize transmit buffer!
	client->packetserver_transmitbuffer = zalloc(client->packetserver_transmitsize,"MODEM_SENDPACKET",NULL); //Initial transmit buffer!
	client->packetserver_transmitlength = 0; //Nothing buffered yet!
	client->packetserver_transmitstate = 0; //Initialize transmitter state to the default state!
	client->packetserver_stage = PACKETSTAGE_INIT; //Initial state when connected.
#if defined(PACKETSERVER_ENABLED) && !defined(NOPCAP)
	for (c=0;c<NUMITEMS(BIOS_Settings.ethernetserver_settings.users);++c)
	{
		if (BIOS_Settings.ethernetserver_settings.users[c].username[0]&&BIOS_Settings.ethernetserver_settings.users[c].password[0]) //Gotten credentials?
		{
			client->packetserver_stage = PACKETSTAGE_INIT_PASSWORD; //Initial state when connected: ask for credentials too.
			break;
		}
	}
#endif
	client->packetserver_stage_byte = PACKETSTAGE_INITIALIZING; //Reset stage byte: uninitialized!
	if (client->packet)
	{
		freez((void **)&client->packet, client->pktlen, "SERVER_PACKET"); //Release the buffered packet: we're a new client!
		client->packet = NULL; //No packet anymore!
	}
	if (client->IPpacket)
	{
		freez((void**)&client->IPpacket, client->IPpktlen, "SERVER_PACKET"); //Release the buffered packet: we're a new client!
		client->IPpacket = NULL; //No packet anymore!
	}
	client->packetserver_packetpos = 0; //No packet buffered anymore! New connections must read a new packet!
	client->packetserver_packetack = 0; //Not acnowledged yet!
	fifobuffer_clear(modem.inputdatabuffer[client->connectionnumber]); //Nothing is received yet!
	fifobuffer_clear(modem.outputbuffer[client->connectionnumber]); //Nothing is sent yet!
	fifobuffer_clear(modem.blockoutputbuffer[client->connectionnumber]); //Nothing is sent to the client yet!
	client->packetserver_slipprotocol = 0; //Initialize the protocol to the default value, which is unused!
	client->lastreceivedCRLFinput = 0; //Reset last received input to none of CR and LF!
	client->ARPrequeststatus = 0; //No request loaded yet.
	client->roundrobinpackettype = 0; //Initialize the round-robin packet receiver!
}

byte packetserver_authenticate(PacketServer_clientp client)
{
#ifdef PACKETSERVER_ENABLED
#ifndef NOPCAP
	byte IPnumbers[4];
	word c;
	char *p;
#endif
#endif
	if ((strcmp(client->packetserver_protocol, "slip") == 0) || (strcmp(client->packetserver_protocol, "ethernetslip") == 0) || (strcmp(client->packetserver_protocol, "ipxslip") == 0) || (strcmp(client->packetserver_protocol, "ppp") == 0) || (strcmp(client->packetserver_protocol, "pppoe") == 0)) //Valid protocol?
	{
#ifdef PACKETSERVER_ENABLED
#ifndef NOPCAP
		if (!(BIOS_Settings.ethernetserver_settings.users[0].username[0] && BIOS_Settings.ethernetserver_settings.users[0].password[0])) //Gotten no default credentials?
		{
			safestrcpy(client->packetserver_staticIPstr, sizeof(client->packetserver_staticIPstr), packetserver_defaultstaticIPstr); //Default!
			memcpy(&client->packetserver_staticIP, &packetserver_defaultstaticIP, 4); //Set read IP!
			client->packetserver_useStaticIP = packetserver_usedefaultStaticIP; //Static IP set!
			return 1; //Always valid: no credentials required!
		}
		else
		{
			for (c = 0; c < NUMITEMS(BIOS_Settings.ethernetserver_settings.users); ++c) //Check all users!
			{
				if (!(BIOS_Settings.ethernetserver_settings.users[c].username[c] && BIOS_Settings.ethernetserver_settings.users[c].password[c])) //Gotten no credentials?
					continue;
				if (!(strcmp(BIOS_Settings.ethernetserver_settings.users[c].username, client->packetserver_username) || strcmp(BIOS_Settings.ethernetserver_settings.users[c].password, client->packetserver_password))) //Gotten no credentials?
				{
					//Determine the IP address!
					memcpy(&client->packetserver_staticIP, &packetserver_defaultstaticIP, sizeof(client->packetserver_staticIP)); //Use the default IP!
					safestrcpy(client->packetserver_staticIPstr, sizeof(client->packetserver_staticIPstr), packetserver_defaultstaticIPstr); //Formulate the address!
					client->packetserver_useStaticIP = 0; //Default: not detected!
					if (safestrlen(&BIOS_Settings.ethernetserver_settings.users[c].IPaddress[0], 256) >= 12) //Valid length to convert IP addresses?
					{
						p = &BIOS_Settings.ethernetserver_settings.users[c].IPaddress[0]; //For scanning the IP!

						if (readIPnumber(&p, &IPnumbers[0]))
						{
							if (readIPnumber(&p, &IPnumbers[1]))
							{
								if (readIPnumber(&p, &IPnumbers[2]))
								{
									if (readIPnumber(&p, &IPnumbers[3]))
									{
										if (*p == '\0') //EOS?
										{
											//Automatic port?
											snprintf(client->packetserver_staticIPstr, sizeof(client->packetserver_staticIPstr), "%u.%u.%u.%u", IPnumbers[0], IPnumbers[1], IPnumbers[2], IPnumbers[3]); //Formulate the address!
											memcpy(&client->packetserver_staticIP, &IPnumbers, 4); //Set read IP!
											client->packetserver_useStaticIP = 1; //Static IP set!
										}
									}
								}
							}
						}
					}
					else if (safestrlen(&BIOS_Settings.ethernetserver_settings.users[c].IPaddress[0], 256) == 4) //Might be DHCP?
					{
						if ((strcmp(BIOS_Settings.ethernetserver_settings.users[c].IPaddress, "DHCP") == 0) || (strcmp(BIOS_Settings.ethernetserver_settings.users[0].IPaddress, "DHCP") == 0)) //DHCP used for this user or all users?
						{
							//client->packetserver_useStaticIP = 2; //DHCP requested instead of static IP! Not used yet!
						}
					}
					if (!client->packetserver_useStaticIP) //Not specified? Use default!
					{
						safestrcpy(client->packetserver_staticIPstr, sizeof(client->packetserver_staticIPstr), packetserver_defaultstaticIPstr); //Default!
						memcpy(&client->packetserver_staticIP, &packetserver_defaultstaticIP, 4); //Set read IP!
						client->packetserver_useStaticIP = packetserver_usedefaultStaticIP; //Static IP set!
					}
					return 1; //Valid credentials!
				}
			}
		}
#else
		return 1; //Valid credentials!
#endif
#endif
	}
	return 0; //Invalid credentials!
}

byte ATresultsString[6][256] = {"ERROR","OK","CONNECT","RING","NO DIALTONE","NO CARRIER"}; //All possible results!
byte ATresultsCode[6] = {4,0,1,2,6,3}; //Code version!
#define MODEMRESULT_ERROR 0
#define MODEMRESULT_OK 1
#define MODEMRESULT_CONNECT 2
#define MODEMRESULT_RING 3
#define MODEMRESULT_NODIALTONE 4
#define MODEMRESULT_NOCARRIER 5

//usecarriagereturn: bit0=before, bit1=after, bit2=use linefeed
void modem_responseString(byte *s, byte usecarriagereturn)
{
	word i, lengthtosend;
	lengthtosend = (word)safestrlen((char *)s,256); //How long to send!
	if (modem.supported >= 2) return; //No command interface? Then no results!
	if (usecarriagereturn&1)
	{
		writefifobuffer(modem.inputbuffer,modem.carriagereturncharacter); //Termination character!
		if (usecarriagereturn&4) writefifobuffer(modem.inputbuffer,modem.linefeedcharacter); //Termination character!
	}
	for (i=0;i<lengthtosend;) //Process all data to send!
	{
		writefifobuffer(modem.inputbuffer,s[i++]); //Send the character!
	}
	if (usecarriagereturn&2)
	{
		writefifobuffer(modem.inputbuffer,modem.carriagereturncharacter); //Termination character!
		if (usecarriagereturn&4) writefifobuffer(modem.inputbuffer,modem.linefeedcharacter); //Termination character!
	}
}
void modem_nrcpy(char *s, word size, word nr)
{
	memset(s,0,size);
	snprintf(s,size,"%u",nr); //Convert to string!
}
char connectionspeed[256]; //Connection speed!
void modem_responseResult(byte result) //What result to give!
{
	byte s[256];
	if (result>=MIN(NUMITEMS(ATresultsString),NUMITEMS(ATresultsCode))) //Out of range of results to give?
	{
		result = MODEMRESULT_ERROR; //Error!
	}
	if ((modem.verbosemode & 6)==2) //All off?
	{
		return; //Quiet mode? No response messages!
	}
	if ((modem.verbosemode & 6) == 4) //No ring and connect/carrier?
	{
		if ((result == MODEMRESULT_RING) || (result == MODEMRESULT_CONNECT) || (result == MODEMRESULT_NOCARRIER)) //Don't send these when sending results?
		{
			return; //Don't send these results!
		}
	}

	//Now, the results can have different formats:
	/*
	- V0 information text: text<CR><LF>
	- V0 numeric code: code<CR>
	- V1 information text: <CR><LF>text<CR><LF>
	- V1 numeric code: <CR><LF>verbose code<CR><LF>
	*/

	if (modem.verbosemode&1) //Text format result?
	{
		modem_responseString(&ATresultsString[result][0],(((result!=MODEMRESULT_CONNECT) || (modem.callprogressmethod==0))?3:1)|4); //Send the string to the user!
		if ((result == MODEMRESULT_CONNECT) && modem.callprogressmethod) //Add speed as well?
		{
			memset(&connectionspeed,0,sizeof(connectionspeed)); //Init!
			safestrcpy(connectionspeed, sizeof(connectionspeed), " "); //Init!
			safescatnprintf(connectionspeed, sizeof(connectionspeed), "%u", (uint_32)MODEM_DATATRANSFERFREQUENCY); //Add the data transfer frequency!
			modem_responseString((byte *)&connectionspeed[0], (2 | 4)); //End the command properly with a speed indication in bps!
		}
	}
	else //Numeric format result? This is V0 beign active! So just CR after!
	{
		if ((result == MODEMRESULT_CONNECT) && modem.callprogressmethod) //Add speed as well?
		{
			modem_nrcpy((char*)&s[0], sizeof(s), MODEM_DATATRANSFERFREQUENCY_NR); //Report 57600!
		}
		else //Normal result code?
		{
			modem_nrcpy((char*)&s[0], sizeof(s), ATresultsCode[result]);
		}
		modem_responseString(&s[0],((2)));
	}
}

void modem_responseNumber(byte x)
{
	char s[256];
	/*
	- V0 information text: text<CR><LF>
	-> V0 numeric code: code<CR>
	- V1 information text: <CR><LF>text<CR><LF>
	-> V1 numeric code: <CR><LF>verbose code<CR><LF>
	*/
	if (modem.verbosemode&1) //Text format result?
	{
		memset(&s,0,sizeof(s));
		snprintf(s,sizeof(s),"%u",x); //Convert to a string!
		modem_responseString((byte *)&s,(1|2|4)); //Send the string to the user! CRLF before and after!
	}
	else
	{
		modem_nrcpy((char*)&s[0], sizeof(s), x);
		modem_responseString((byte *)&s[0], (2)); //Send the numeric result to the user! CR after!
	}
}

byte modem_sendData(byte value) //Send data to the connected device!
{
	//Handle sent data!
	if (PacketServer_running) return 0; //Not OK to send data this way!
	if (!(fifobuffer_freesize(modem.blockoutputbuffer[0]) == fifobuffer_size(modem.blockoutputbuffer[0])))
	{
		return 0; //Not ready!
	}
	if (modem.supported >= 3) //Might need to be escaped?
	{
		if (modem.passthroughlinestatusdirty & 7) //Still pending to send the last line status?
		{
			return 0; //Don't send any yet! Wait for the transfer to become up-to-date first!
		}
		if (value == 0xFF) //Needs to be escaped?
		{
			writefifobuffer(modem.blockoutputbuffer[0], 0xFF); //Escape the value to write to make it to the other side!
		}
		//Doesn't need to be escaped for any other value!
	}
	return writefifobuffer(modem.blockoutputbuffer[0],value); //Try to write to the output buffer!
}

byte readIPnumber(char **x, byte *number)
{
	byte size=0;
	word result=0;
	for (;(isdigit((int)*(*x)) && (size<3));) //Scan digits!
	{
		result = (result*10)+(*(*x)-'0'); //Convert to a number!
		++(*x); //Next digit!
		++size; //Size has been read!
	}
	if ((size==3) && (result<256)) //Valid IP part?
	{
		*number = (byte)result; //Give the result!
		return 1; //Read!
	}
	return 0; //Not a valid IP section!
}

byte modem_connect(char *phonenumber)
{
	sword connectionid;
	char ipaddress[256];
	byte a,b,c,d;
	char *p; //For normal port resolving!
	unsigned int port;
	if (PacketServer_running) return 0; //Never connect the modem emulation when we're running as a packet server!
	if (modem.ringing && (phonenumber==NULL) && (PacketServer_running==0)) //Are we ringing and accepting it?
	{
		modem.ringing = 0; //Not ringing anymore!
		modem.connected = 1; //We're connected!
		if (modem.supported >= 3) //Requires sending a special packet?
		{
			modem.passthroughlines = 0; //Nothing received yet!
			modem.passthroughlinestatusdirty |= 7; //Request the packet to send!
			modem.breakPending = 0; //Not pending yet!
		}
		return 1; //Accepted!
	}
	else if (phonenumber==NULL) //Not ringing, but accepting?
	{
		return 0; //Not connected!
	}
	if (PacketServer_running) return 0; //Don't accept when the packet server is running instead!
	if (modem.connected == 1) //Connected and dialing out?
	{
		if (TCP_DisconnectClientServer(modem.connectionid)) //Try and disconnect, if possible!
		{
			modem.connectionid = -1; //Not connected anymore if succeeded!
			fifobuffer_clear(modem.inputdatabuffer[0]); //Clear the output buffer for the next client!
			fifobuffer_clear(modem.outputbuffer[0]); //Clear the output buffer for the next client!
			fifobuffer_clear(modem.blockoutputbuffer[0]); //Clear the output buffer for the next client!
			modem.connected = 0; //Not connected anymore!
		}
	}
	memset(&ipaddress,0,sizeof(ipaddress)); //Init IP address to translate!
	if (safestrlen(phonenumber,256)>=12) //Valid length to convert IP addresses?
	{
		p = phonenumber; //For scanning the phonenumber!
		if (readIPnumber(&p,&a))
		{
			if (readIPnumber(&p,&b))
			{
				if (readIPnumber(&p,&c))
				{
					if (readIPnumber(&p,&d))
					{
						if (*p=='\0') //EOS?
						{
							//Automatic port?
							snprintf(ipaddress,sizeof(ipaddress),"%u.%u.%u.%u",a,b,c,d); //Formulate the address!
							port = modem.connectionport; //Use the default port as specified!
						}
						else if (*p==':') //Port might follow?
						{
							++p; //Skip character!
							if (sscanf(p,"%u",&port)==0) //Port incorrectly read?
							{
								return 0; //Fail: invalid port has been specified!
							}
							snprintf(ipaddress,sizeof(ipaddress),"%u.%u.%u.%u",a,b,c,d);
						}
						else //Invalid?
						{
							goto plainaddress; //Plain address inputted?
						}
					}
					else
					{
						goto plainaddress; //Take as plain address!
					}
				}
				else
				{
					goto plainaddress; //Take as plain address!
				}
			}
			else
			{
				goto plainaddress; //Take as plain address!
			}
		}
		else
		{
			goto plainaddress; //Take as plain address!
		}
	}
	else
	{
		plainaddress: //A plain address after all?
		if ((p = strrchr(phonenumber,':'))!=NULL) //Port is specified?
		{
			safestrcpy(ipaddress,sizeof(ipaddress),phonenumber); //Raw IP with port!
			ipaddress[(ptrnum)p-(ptrnum)phonenumber] = '\0'; //Cut off the port part!
			++p; //Take the port itself!
			if (sscanf(p,"%u",&port)==0) //Port incorrectly read?
			{
				return 0; //Fail: invalid port has been specified!
			}
		}
		else //Raw address?
		{
			safestrcpy(ipaddress,sizeof(ipaddress),phonenumber); //Use t
			port = modem.connectionport; //Use the default port as specified!
		}
	}
	if ((connectionid = TCP_ConnectClient(ipaddress,port))>=0) //Connected on the port specified(use the server port by default)?
	{
		modem.connectionid = connectionid; //We're connected to this!
		modem.connected = 1; //We're connected!
		if (modem.supported >= 3) //Requires sending a special packet?
		{
			modem.passthroughlines = 0; //Nothing received yet!
			modem.passthroughlinestatusdirty |= 7; //Request the packet to send!
			modem.breakPending = 0; //Not pending yet!
		}
		return 1; //We're connected!
	}
	return 0; //We've failed to connect!
}

void modem_hangup() //Hang up, if possible!
{
	if (modem.connected == 1) //Connected?
	{
		TCP_DisconnectClientServer(modem.connectionid); //Try and disconnect, if possible!
		modem.connectionid = -1; //Not connected anymore
		fifobuffer_clear(modem.inputdatabuffer[0]); //Clear the output buffer for the next client!
		fifobuffer_clear(modem.outputbuffer[0]); //Clear the output buffer for the next client!
		fifobuffer_clear(modem.blockoutputbuffer[0]); //Clear the output buffer for the next client!
	}
	modem.connected &= ~1; //Not connected anymore!
	modem.ringing = 0; //Not ringing anymore!
	modem.offhook = 0; //We're on-hook!
	fifobuffer_clear(modem.inputdatabuffer[0]); //Clear anything we still received!
	fifobuffer_clear(modem.outputbuffer[0]); //Clear anything we still need to send!
	fifobuffer_clear(modem.blockoutputbuffer[0]); //Clear anything we still need to send!
}

void modem_updateRegister(byte reg)
{
	switch (reg) //What reserved reg to emulate?
	{
		case 2: //Escape character?
			if (modem.escapecharacter!=modem.registers[reg]) //Escape character changed?
			{
				for (;modem.escaping;--modem.escaping) //Process all escaped data!
				{
					modem_sendData(modem.escapecharacter); //Send all escaped data!
				}
			}
			modem.escapecharacter = modem.registers[reg]; //Escape!
			break;
		case 3: //CR character?
			modem.carriagereturncharacter = modem.registers[reg]; //Escape!
			break;
		case 4: //Line feed character?
			modem.linefeedcharacter = modem.registers[reg]; //Escape!
			break;
		case 5: //Backspace character?
			modem.backspacecharacter = modem.registers[reg]; //Escape!
			break;
		case 12: //Escape code guard time?
			#ifdef IS_LONGDOUBLE
			modem.escapecodeguardtime = (modem.registers[reg]*20000000.0L); //Set the escape code guard time, in nanoseconds!
			#else
			modem.escapecodeguardtime = (modem.registers[reg]*20000000.0); //Set the escape code guard time, in nanoseconds!
			#endif
			break;
		case 25: //DTR to DSR Delay Interval(hundredths of a second)
			#ifdef IS_LONGDOUBLE
			modem.effectiveDTRlineDelay = (modem.registers[reg] * 10000000.0L); //Set the RTC to CTS line delay, in nanoseconds!
			#else
			modem.effectiveDTRlineDelay = (modem.registers[reg] * 10000000.0); //Set the RTC to CTS line delay, in nanoseconds!
			#endif
			break;
		case 26: //RTC to CTS Delay Interval(hundredths of a second)
			#ifdef IS_LONGDOUBLE
			modem.effectiveRTSlineDelay = (modem.registers[reg] * 10000000.0L); //Set the RTC to CTS line delay, in nanoseconds!
			#else
			modem.effectiveRTSlineDelay = (modem.registers[reg] * 10000000.0); //Set the RTC to CTS line delay, in nanoseconds!
			#endif
			break;
		default: //Unknown/unsupported?
			break;
	}
}

byte useSERModem() //Serial mouse enabled?
{
	return modem.supported; //Are we supported?
}

byte loadModemProfile(byte state)
{
	if (state==0) //OK?
	{
		return 1; //OK: loaded state!
	}
	return 0; //Default: no states stored yet!
}

byte resetModem(byte state)
{
	word reg;
	memset(&modem.registers,0,sizeof(modem.registers)); //Initialize the registers!
	//Load general default state!
	modem.registers[0] = 0; //Number of rings before auto-answer
	modem.registers[1] = 0; //Ring counter
	modem.registers[2] = 43; //Escape character(+, ASCII)
	modem.registers[3] = 0xD; //Carriage return character(ASCII)
	modem.registers[4] = 0xA; //Line feed character(ASCII)
	modem.registers[5] = 0x8; //Back space character(ASCII)
	modem.registers[6] = 2; //Wait time before blind dialing(seconds).
	modem.registers[7] = 50; //Wait for carrier after dial(seconds(+1))
	modem.registers[8] = 2; //Pause time for ,(dial delay, seconds)
	modem.registers[9] = 6; //Carrier detect response time(tenths of a seconds(+1)) 
	modem.registers[10] = 14; //Delay between Loss of Carrier and Hang-up(tenths of a second)
	modem.registers[11] = 95; //DTMF Tone duration(50-255 milliseconds)
	modem.registers[12] = 50; //Escape code guard time(fiftieths of a second)
	modem.registers[18] = 0; //Test timer(seconds)
	modem.registers[25] = 5; //Delay to DTR(seconds in synchronous mode, hundredths of a second in all other modes)
	modem.registers[26] = 1; //RTC to CTS Delay Interval(hundredths of a second)
	modem.registers[30] = 0; //Inactivity disconnect timer(tens of seconds). 0=Disabled
	modem.registers[37] = 0; //Desired Telco line speed(0-10. 0=Auto, otherwise, speed)
	modem.registers[38] = 20; //Delay before Force Disconnect(seconds)
	for (reg=0;reg<256;++reg)
	{
		modem_updateRegister((byte)reg); //This register has been updated!
	}

	/*

	According to , defaults are:
	B0: communicationstandard=0
	E1: echomode=1
	F0
	L3: speakervolume=3
	M1: speakercontrol=1
	N1
	Q0: verbosemode=(value)<<1|(verbosemode&1)
	T
	V1: verboseemode=(value)|verbosemode
	W1
	X4: callprogressmethod=4
	Y0
	&C1: DCDmodeisCarrier=1
	&D2: DTRoffRresponse=2
	&K3: flowcontrol=3
	&Q5: communicatinsmode=5
	&R1: CTSalwaysActive=1
	&S0: DSRisConnectionEstablished=0
	\A1
	\B3
	\K5
	\N3: 
	%C3
	%E2

	*/
	modem.communicationstandard = 0; //Default communication standard!
	modem.echomode = 1; //Default: echo back!
	//Speaker controls
	modem.speakervolume = 3; //Max level speaker volume!
	modem.speakercontrol = 1; //Enabled speaker!
	//Result defaults
	modem.verbosemode = 1; //Text-mode verbose!
	modem.callprogressmethod = 4;
	//Default handling of the Hardware lines is also loaded:
	modem.DCDisCarrier = 1; //Default: DCD=Set Data Carrier Detect (DCD) signal according to remote modem data carrier signal..
	modem.DTROffResponse = 2; //Default: Hang-up and Goto AT command mode?!
	modem.flowcontrol = 3; //Default: Enable RTS/CTS flow control!
	modem.communicationsmode = 5; //Default: communications mode 5 for V-series system products, &Q0 for Smartmodem products! So use &Q5 here!
	modem.CTSAlwaysActive = 1; //Default: CTS controlled by flow control!
	modem.DSRisConnectionEstablished = 0; //Default: DSR always ON!
	//Finish up the default settings!
	modem.datamode = 0; //In command mode!

	memset(&modem.lastnumber,0,sizeof(modem.lastnumber)); //No last number!
	modem.offhook = 0; //On-hook!
	if (modem.connected&1) //Are we still connected?
	{
		modem.connected &= ~1; //Disconnect!
		modem_responseResult(MODEMRESULT_NOCARRIER); //Report no carrier!
		TCP_DisconnectClientServer(modem.connectionid); //Disconnect the client!
		modem.connectionid = -1; //Not connected anymore!
		fifobuffer_clear(modem.inputdatabuffer[0]); //Clear the output buffer for the next client!
		fifobuffer_clear(modem.outputbuffer[0]); //Clear the output buffer for the next client!
		fifobuffer_clear(modem.blockoutputbuffer[0]); //Clear the output buffer for the next client!
	}


	//Misc data
	memset(&modem.previousATCommand,0,sizeof(modem.previousATCommand)); //No previous command!

	if (loadModemProfile(state)) //Loaded?
	{
		return 1; //OK!
	}
	return 0; //Invalid profile!
}

void MODEM_sendAutodetectionPNPmessage()
{
	if (modem.supported >= 2) return; //Don't handle responses in passthrough mode!
	//return; //We don't know what to send yet, so disable the PNP feature for now!
	//According to https://git.kontron-electronics.de/linux/linux-imx-exceet/blob/115a57c5b31ab560574fe1a09deaba2ae89e77b5/drivers/serial/8250_pnp.c , PNPC10F should be a "Standard Modem".
	//"PNPC10F"=Standard Modem. Order is(in order of escapes: Version(two 5-bits values, divided by 100 is the version number, high 5-bits first, low 5-bits second) ID("PNP"), product ID(the ID), Serial number(00000001), Class name("MODEM", as literally in the Plug and Play Exernal COM Device Specification Version 1.00 February 28, 1995), Device ID("," followed by the ID), User name("Modem", this is what's reported to the user as plain text).
	//The ID used to be "PNPC10F". Use PNPC102 for a safe Standard 28800bps modem.
	char EISA_productID[] = "PNPC107"; //Product ID! Standard modem?
	char DeviceID[] = "\\PNPC107"; //Device ID! Standard modem?
	char PNPHeader[] = "\x28\x01\x24"; //Header until EISA/product ID
	char PNPMid[] = "\\00000001\\MODEM"; //After EISA/product ID until Device ID
	char PNPFooter[] = "\\ModemCC\x29"; //Footer with checksum!
	char message[256]; //Buffer for the message to be modified into!
	memset(&message, 0, sizeof(message)); //Init the message to fill!
	word size;
	byte checksum;
	char *p, *e;
	//Start generating the checksum!
	checksum = 0; //Init checksum!
	//Copy the initial buffer data over(excluding checksum)!
	safestrcat(message,sizeof(message),PNPHeader); //Copy the message over to the actual buffer!
	safestrcat(message,sizeof(message),EISA_productID); //Copy the product ID over!
	safestrcat(message,sizeof(message),PNPMid); //Copy the second part of the message to the actual buffer!
	safestrcat(message,sizeof(message),DeviceID); //Copy the device ID over!
	safestrcat(message,sizeof(message),PNPFooter); //Copy the footer over!
	size = safe_strlen(message,sizeof(message)); //Size to send! Sizeof includes a final NULL byte, which we don't want to include! Taking sizeof's position gives us the byte past the string!
	e = &message[size-1]; //End of the message buffer(when to stop processing the checksum(the end PnP character). This selects from after the start byte until before the end byte, excluding the checksum itself)!
	p = &message[1]; //Init message to calculate the checksum(a ROMmed constant) to the first used byte(the byte after the start of the )!
	message[size - 2] = 0; //Second checksum nibble isn't counted!
	message[size - 3] = 0; //First checksum nibble isn't counted!
	for (;(p!=e);) //Not finished processing the entire checksum?
	{
		checksum += *p++; //Add to the checksum(minus the actual checksum bytes)! Also copy to the active message buffer!
	}
	checksum &= 0xFF; //It's MOD 256 for all but the checksum fields itself to get the actual checksum!
	message[size - 2] = ((checksum & 0xF) > 0xA) ? (((checksum & 0xF) - 0xA) + (byte)'A') : ((checksum & 0xF) + (byte)'0'); //Convert hex digit the low nibble checksum!
	message[size - 3] = (((checksum>>4) & 0xF) > 0xA) ? ((((checksum>>4) & 0xF) - 0xA) + (byte)'A') : (((checksum>>4) & 0xF) + (byte)'0'); //Convert hex digit the high nibble checksum!

	//The PNP message is now ready to be sent to the Data Terminal!

	fifobuffer_clear(modem.inputbuffer); //Clear the input buffer for out message!
	char c;
	p = &message[0]; //Init message!
	e = &message[size]; //End of the message buffer! Don't include the terminating NULL character, so substract one to stop when reaching the NULL byte instead of directly after it!
	for (; (p!=e) && ((fifobuffer_freesize(modem.inputbuffer)>2));) //Process the message, until either finished or not enough size left!
	{
		c = *p++; //Read a character to send!
		writefifobuffer(modem.inputbuffer, c); //Write the character!
	}
	//Finally, the CR/LF combination is sent!
	writefifobuffer(modem.inputbuffer,modem.carriagereturncharacter);
	writefifobuffer(modem.inputbuffer,modem.linefeedcharacter);
}

void modem_updatelines(byte lines); //Prototype for modem_setModemControl!

void modem_setModemControl(byte line) //Set output lines of the Modem!
{
	//Handle modem specifics here!
	//0: Data Terminal Ready(we can are ready to work), 1: Request to Send(UART can receive data), 4=Set during mark state of the TxD line.
	if ((line & 0x10) == 0) //Mark not set?
	{
		//TxD isn't mark, the detection timers are stopped, as TxD is required to be mark when using the detection scheme!
		modem.detectiontimer[0] = (DOUBLE)0; //Stop timing!
		modem.detectiontimer[1] = (DOUBLE)0; //Stop timing!
	}
	modem.canrecvdata = (line&2); //Can we receive data?
	modem.TxDisMark = ((line & 0x10) >> 4); //Is TxD set to mark?
	modem.TxDisBreak = ((line & 0x20) >> 5); //Is TxD set to break?
	line &= 0x2F; //Ignore unused lines!
	modem.outputline = line; //The line that's output!
	if ((modem.linechanges^line)&2) //RTS changed?
	{
		modem.RTSlineDelay = modem.effectiveRTSlineDelay; //Start timing the CTS line delay!
		if (modem.RTSlineDelay) //Gotten a delay?
		{
			modem_updatelines(2 | 4); //Update RTS internally, don't acnowledge RTS to CTS yet!
		}
		else
		{
			modem_updatelines(2); //Update RTS internally, acnowledge RTS to CTS!
		}
	}
	if (((modem.linechanges^line)&1)) //DTR changed?
	{
		modem.DTRlineDelay = modem.effectiveDTRlineDelay; //Start timing the CTS line delay!
		if (modem.DTRlineDelay) //Gotten a delay?
		{
			modem_updatelines(1 | 4); //Update DTR, don't acnowledge yet!
		}
		else
		{
			modem_updatelines(1); //Update DTR, acnowledge!
		}
	}
	if ((modem.linechanges ^ line) & 0x20) //Break changed?
	{
		modem_updatelines(0x20); //Update Break internally, acnowledging it!
	}
	modem.linechanges = line; //Save for reference!
}

void modem_Answered(); //Prototype!

void modem_updatelines(byte lines)
{
	if ((lines & 4) == 0) //Update effective lines?
	{
		modem.effectiveline = ((modem.effectiveline & ~(lines & 3)) | (modem.outputline & (lines & 3))); //Apply the line(s)!
	}
	if ((((modem.effectiveline&1)==0) && ((modem.effectivelinechanges^modem.effectiveline)&1)) && ((lines&4)==0)) //Became not ready?
	{
		modem.detectiontimer[0] = (DOUBLE)0; //Stop timing!
		modem.detectiontimer[1] = (DOUBLE)0; //Stop timing!
		if (modem.supported < 2) //Able to respond normally?
		{
			switch (modem.DTROffResponse) //What reponse?
			{
			case 0: //Ignore the line?
				break;
			case 3: //Reset and Hang-up?
			case 2: //Hang-up and Goto AT command mode?
				if ((modem.connected & 1) || modem.ringing) //Are we connected?
				{
					modem_responseResult(MODEMRESULT_NOCARRIER); //No carrier!
					modem_hangup(); //Hang up!
				}
			case 1: //Goto AT command mode?
				modem.datamode = (byte)(modem.ATcommandsize = 0); //Starting a new command!
				if (modem.DTROffResponse == 3) //Reset as well?
				{
					resetModem(0); //Reset!
				}
				break;
			default:
				break;
			}
		}
		else if (modem.supported >= 3) //Line status is passed as well?
		{
			modem.passthroughlinestatusdirty |= 1; //DTR Line is dirty!
		}
	}
	else if ((((modem.outputline & 1) == 0) && ((modem.outputlinechanges ^ modem.outputline) & 1))) //Became not ready?
	{
		if (modem.supported >= 3) //Line status is passed as well?
		{
			modem.passthroughlinestatusdirty |= 1; //DTR Line is dirty!
		}
	}
	else if ((((modem.outputline & 1) == 1) && ((modem.outputlinechanges ^ modem.outputline) & 1))) //Became ready?
	{
		if (modem.supported >= 3) //Line status is passed as well?
		{
			modem.passthroughlinestatusdirty |= 1; //DTR Line is dirty!
		}
		if (modem.supported >= 4) //Manual dialling out is enabled using phonebook entry #0?
		{
			if (modem.connected != 1) //Not already connected on the modem?
			{
				char* c = &BIOS_Settings.phonebook[0][0]; //Phone book support for entry #0!
				safestrcpy((char*)&modem.lastnumber, sizeof(modem.lastnumber), c); //Set the last number!
				if (modem_connect(c)) //Try to dial said number!
				{
					modem_Answered(); //Answer!
				}
			}
		}
	}

	if (((modem.outputlinechanges ^ modem.outputline) & 0x20)!=0) //Changed break?
	{
		modem.passthroughlinestatusdirty |= 4; //Break Line is dirty!
	}

	if (modem.supported < 2) //Normal behaviour?
	{
		if (((modem.outputline & 1) == 1) && ((modem.outputlinechanges ^ modem.outputline) & 1) && (modem.TxDisMark)) //DTR set while TxD is mark?
		{
			modem.detectiontimer[0] = (DOUBLE)150000000.0; //Timer 150ms!
			modem.detectiontimer[1] = (DOUBLE)250000000.0; //Timer 250ms!
			//Run the RTS checks now!
		}
		if ((modem.outputline & 2) && (modem.detectiontimer[0])) //RTS and T1 not expired?
		{
		modem_startidling:
			modem.detectiontimer[0] = (DOUBLE)0; //Stop timing!
			modem.detectiontimer[1] = (DOUBLE)0; //Stop timing!
			goto finishupmodemlinechanges; //Finish up!
		}
		if ((modem.outputline & 2) && (!modem.detectiontimer[0]) && (modem.detectiontimer[1])) //RTS and T1 expired and T2 not expired?
		{
			//Send serial PNP message!
			MODEM_sendAutodetectionPNPmessage();
			goto modem_startidling; //Start idling again!
		}
		if ((modem.outputline & 2) && (!modem.detectiontimer[1])) //RTS and T2 expired?
		{
			goto modem_startidling; //Start idling again!
		}
	}
	else if ((modem.supported >= 3) && ((modem.outputline ^ modem.outputlinechanges) & 2)) //Line status is passed as well?
	{
		if ((modem.outputline ^ modem.outputlinechanges) & 2) //RTS is passed as well?
		{
			modem.passthroughlinestatusdirty |= 2; //RTS Line is dirty!
		}
		//Check for break as well? Break isn't supported as an output from the UART yet?
	}
finishupmodemlinechanges:
	modem.outputlinechanges = modem.outputline; //Save for reference!
	if ((lines & 4) == 0) //Apply effective line?
	{
		modem.effectivelinechanges = modem.effectiveline; //Save for reference!
	}
}

byte modem_hasData() //Do we have data for input?
{
	byte havedatatoreceive; //Do we have data to receive?
	byte temp;
	byte allowdatatoreceive; //Do we allow data to receive?
	havedatatoreceive = (peekfifobuffer(modem.inputbuffer, &temp) || (peekfifobuffer(modem.inputdatabuffer[0], &temp) && (modem.datamode == 1))); //Do we have data to receive?
	if (modem.supported >= 2) //Passthrough mode?
	{
		allowdatatoreceive = modem.canrecvdata; //Default: allow to receive if not blocked!
		if (modem.supported >= 3) //Lines as well?
		{
			allowdatatoreceive = 1; //Always allow data to receive!
		}
	}
	else if (modem.communicationsmode && (modem.communicationsmode < 4)) //Synchronous mode? CTS is affected!
	{
		allowdatatoreceive = ((modem.canrecvdata && ((modem.flowcontrol == 1) || (modem.flowcontrol == 3))) || ((modem.flowcontrol != 1) && (modem.flowcontrol != 3))); //Default: allow all data to receive!
		switch (modem.CTSAlwaysActive)
		{
		case 0: //Track RTS? V.25bis handshake!
			break;
		case 1: //Depends on the buffers! Only drop when required by flow control!
			break;
		case 2: //Always on?
			break;
		}
	}
	else //Asynchronous mode?
	{
		//Hayes documentation says it doesn't control CTS and RTS functions!
		allowdatatoreceive = 1; //Ignore any RTS input!
	}

	return (havedatatoreceive&&allowdatatoreceive); //Do we have data to receive and flow control allows it?
}

byte modem_getstatus()
{
	byte result = 0;
	result = 0;
	//0: Clear to Send(Can we buffer data to be sent), 1: Data Set Ready(Not hang up, are we ready for use), 2: Ring Indicator, 3: Carrrier detect, 4: Break
	if (modem.supported >= 2) //CTS depends on the outgoing buffer in passthrough mode!
	{
		result |= ((modem.datamode == 1) ? ((modem.connectionid >= 0) ? ((fifobuffer_freesize(modem.blockoutputbuffer[0]) == fifobuffer_size(modem.blockoutputbuffer[0])) ? 1 : 0) : 0) : 0); //Can we send to the modem?
		if (modem.supported >= 3) //Also depend on the received line!
		{
			result = (result & ~1) | ((modem.passthroughlines >> 1) & 1); //Mask CTS with received RTS!
		}
	}
	else if (modem.communicationsmode && (modem.communicationsmode < 4)) //Synchronous mode? CTS is affected!
	{
		switch (modem.CTSAlwaysActive)
		{
		case 0: //Track RTS? V.25bis handshake!
			result |= ((modem.outputline >> 1) & 1); //Track RTS, undelayed!
			break;
		case 1: //Depends on the buffers! Only drop when required by flow control!
			result |= ((modem.datamode == 1) ? ((modem.connectionid >= 0) ? ((fifobuffer_freesize(modem.blockoutputbuffer[0]) == fifobuffer_size(modem.blockoutputbuffer[0])) ? 1 : 0) : 1) : 1); //Can we send to the modem?
			break;
		case 2: //Always on?
			result |= 1; //Always on!
			break;
		}
	}
	else //Asynchronous mode?
	{
		//Hayes documentation says it doesn't control CTS and RTS functions!
		switch (modem.CTSAlwaysActive)
		{
		case 0: //RTS, delayed by S26 register's setting?
			result |= ((modem.effectiveline >> 1) & 1); //Track RTS, delayed!
			break;
		case 1: //Always on? RTS is ignored!
			result |= 1; //Always on! &Rn has no effect according to Hayes docs! But do this anyways!
			break;
		case 2: //Always on?
			result |= 1; //Always on!
			break;
		}
	}
	//DSRisConnectionEstablished: 0:1, 1:DTR
	if (modem.supported >= 2) //DTR depends on the outgoing connection in passthrough mode!
	{
		if ((modem.outputline & 1) == 1) //DTR is set? Then raise DSR when connected using the nullmodem line!
		{
			if ((modem.connected == 1) && (modem.datamode)) //Handshaked or pending handshake?
			{
				result |= 2; //Raise the line!
				if (modem.supported >= 3) //Also depend on the received line?
				{
					result = (result & ~2) | ((result & ((modem.passthroughlines << 1) & 2))); //Replace DSR with received DTR!
				}
			}
		}
	}
	else if ((modem.communicationsmode) && (modem.communicationsmode < 5)) //Special actions taken?
	{
		switch (modem.DSRisConnectionEstablished) //What state?
		{
		default:
		case 0: //S0?
		case 1: //S1?
			//0 at command state and idle, handshake(connected) turns on, lowered when hanged up.
			if ((modem.connected == 1) && (modem.datamode != 2)) //Handshaked?
			{
				result |= 2; //Raise the line!
			}
			//Otherwise, lower the line!
			break;
		case 2: //S2?
			//0 at command state and idle, prior to handshake turns on, lowered when hanged up.
			if ((modem.connected == 1) && (modem.datamode)) //Handshaked or pending handshake?
			{
				result |= 2; //Raise the line!
			}
			//Otherwise, lower the line!
			break;
		}
	}
	else //Q0/5/6?
	{
		switch (modem.DSRisConnectionEstablished) //What state?
		{
		default:
		case 0: //S0?
			result |= 2; //Always raised!
			break;
		case 1: //S1?
			result |= ((modem.outputline & 1) << 1); //Follow handshake!
			break;
		case 2: //S2?
			result |= ((modem.outputline & 1) << 1); //Follow handshake!
			break;
		}
	}
	result |= (((modem.ringing&1)&((modem.ringing)>>1))?4:0)| //Currently Ringing?
			(((modem.connected==1)||((modem.DCDisCarrier==0)&&(modem.supported<2)))?8:0); //Connected or forced on(never forced on for passthrough mode)?

	if (modem.supported >= 3) //Break is implemented?
	{
		result |= (((((modem.passthroughlines & 4) >> 2)&(fifobuffer_freesize(modem.inputdatabuffer[0])))&1)<<4); //Set the break output when needed and not receiving anything anymore on the UART!
		if (likely(modem.breakPending == 0)) //Not break pending or pending anymore (preventing re-triggering without raising it again)?
		{
			result &= ~0x10; //Clear break signalling, as it's not pending yet or anymore!
		}
	}

	return result; //Give the resulting line status!
}

byte modem_readData()
{
	byte result,emptycheck;
	if (modem.breakPending && (fifobuffer_freesize(modem.inputbuffer) == MODEM_TEXTBUFFERSIZE) && (fifobuffer_freesize(modem.inputdatabuffer[0])==fifobuffer_size(modem.inputdatabuffer[0]))) //Break acnowledged by reading the result data?
	{
		modem.breakPending = 0; //Not pending anymore, acnowledged!
		return 0; //A break has this value (00h) read on it's data lines!
	}
	if (modem.datamode!=1) //Not data mode?
	{
		if (readfifobuffer(modem.inputbuffer, &result))
		{
			if ((modem.datamode==2) && (!peekfifobuffer(modem.inputbuffer,&emptycheck))) //Became ready to transfer data?
			{
				modem.datamode = 1; //Become ready to send!
			}
			return result; //Give the data!
		}
	}
	if (modem.datamode==1) //Data mode?
	{
		if (readfifobuffer(modem.inputdatabuffer[0], &result))
		{
			return result; //Give the data!
		}
	}
	return 0; //Nothing to give!
}

byte modemcommand_readNumber(word *pos, int *result)
{
	byte valid = 0;
	*result = 0;
	nextpos:
	switch (modem.ATcommand[*pos]) //What number?
	{
	case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
		*result = (*result*10)+(modem.ATcommand[*pos]-'0'); //Convert to a number!
		++*pos; //Next position to read!
		valid = 1; //We're valid!
		goto nextpos; //Read the next position!
		break;
	default: //Finished?
		break;
	}
	return valid; //Is the result valid?
}

void modem_Answered()
{
	if (modem.supported < 2) //Not passthrough mode?
	{
		modem_responseResult(MODEMRESULT_CONNECT); //Connected!
		modem.datamode = 2; //Enter data mode pending!
	}
	else
	{
		modem.datamode = 1; //Enter data mode!
	}
	modem.offhook = 1; //Off-hook(connect)!
}

void modem_executeCommand() //Execute the currently loaded AT command, if it's valid!
{
	char firmwareversion[] = "UniPCemu emulated modem V1.00\0"; //Firmware version!
	char hardwareinformation[] = "UniPCemu Hayes - compatible modem\0"; //Hardware information!
	char tempcommand[256]; //Stripped command with spaces removed!
	char tempcommand2[256]; //Stripped original case command with spaces removed!
	int n0;
	char number[256];
	byte dialproperties=0;
	memset(&number,0,sizeof(number)); //Init number!
	byte *temp, *temp2;
	byte verbosemodepending; //Pending verbose mode!

	temp = &modem.ATcommand[0]; //Parse the entire string!
	temp2 = &modem.ATcommandoriginalcase[0]; //Original case backup!
	for (; *temp;)
	{
		*temp2 = *temp; //Original case backup!
		*temp = (byte)toupper((int)*temp); //Convert to upper case!
		++temp; //Next character!
		++temp2; //Next character!
	}
	*temp2 = (char)0; //End of string!

	//Read and execute the AT command, if it's valid!
	if (strcmp((char *)&modem.ATcommand[0],"A/")==0) //Repeat last command?
	{
		memcpy(&modem.ATcommand,modem.previousATCommand,sizeof(modem.ATcommand)); //Last command again!
		//Re-case the command!
		temp = &modem.ATcommand[0]; //Parse the entire string!
		temp2 = &modem.ATcommandoriginalcase[0]; //Original case backup!
		for (; *temp;)
		{
			*temp2 = *temp; //Original case backup!
			*temp = (byte)toupper((int)*temp); //Convert to upper case!
			++temp; //Next character!
			++temp2; //Next character!
		}
		*temp2 = 0; //End of string!
		modem.detectiontimer[0] = (DOUBLE)0; //Stop timing!
		modem.detectiontimer[1] = (DOUBLE)0; //Stop timing!
	}

	//Check for a command to send!
	//Parse the AT command!

	if (modem.ATcommand[0]==0) //Empty line? Stop dialing and perform autoanswer!
	{
		modem.detectiontimer[0] = (DOUBLE)0; //Stop timing!
		modem.detectiontimer[1] = (DOUBLE)0; //Stop timing!
		return;
	}

	if (!((modem.ATcommand[0] == 'A') && (modem.ATcommand[1] == 'T'))) //Invalid header to the command?
	{
		modem_responseResult(MODEMRESULT_ERROR); //Error!
		return; //Abort!
	}

	if (modem.ATcommand[2] == 0) //Empty AT command? Just an "AT\r" command!
	{
		//Stop dialing and perform autoanswer!
		modem.registers[0] = 0; //Stop autoanswer!
		//Dialing doesn't need to stop, as it's instantaneous!
	}

	modem.detectiontimer[0] = (DOUBLE)0; //Stop timing!
	modem.detectiontimer[1] = (DOUBLE)0; //Stop timing!
	memcpy(&modem.previousATCommand,&modem.ATcommandoriginalcase,sizeof(modem.ATcommandoriginalcase)); //Save the command for later use!
	verbosemodepending = modem.verbosemode; //Save the old verbose mode, to detect and apply changes after the command is successfully completed!
	word pos=2,posbackup; //Position to read!
	byte SETGET = 0;
	word dialnumbers = 0;
	word temppos;
	char *c = &BIOS_Settings.phonebook[0][0]; //Phone book support

	memcpy(&tempcommand, &modem.ATcommand, MIN(sizeof(modem.ATcommand),sizeof(tempcommand))); //Make a copy of the current AT command for stripping!
	memcpy(&tempcommand2, &modem.ATcommandoriginalcase, MIN(sizeof(modem.ATcommandoriginalcase), sizeof(tempcommand2))); //Make a copy of the current AT command for stripping!
	memset(&modem.ATcommand, 0, sizeof(modem.ATcommand)); //Clear the command for the stripped version!
	memset(&modem.ATcommandoriginalcase, 0, sizeof(modem.ATcommandoriginalcase)); //Clear the command for the stripped version!
	posbackup = safe_strlen(tempcommand, sizeof(tempcommand)); //Store the length for fast comparison!
	for (pos = 0; pos < posbackup; ++pos) //We're stripping spaces!
	{
		if (tempcommand[pos] != ' ') //Not a space(which is ignored)? Linefeed is taken as is and errors out when encountered!
		{
			safescatnprintf((char *)&modem.ATcommand[0], sizeof(modem.ATcommand), "%c", tempcommand[pos]); //Add the valid character to the command!
		}
		if (tempcommand2[pos] != ' ') //Not a space(which is ignored)? Linefeed is taken as is and errors out when encountered!
		{
			safescatnprintf((char*)&modem.ATcommandoriginalcase[0], sizeof(modem.ATcommandoriginalcase), "%c", tempcommand2[pos]); //Add the valid character to the command!
		}
	}
	pos = 2; //Reset the position to the end of the AT identifier for the processing of the command!
	for (;;) //Parse the command!
	{
		switch (modem.ATcommand[pos++]) //What command?
		{
		case 0: //EOS? OK!
			handleModemCommandEOS:
			modem_responseResult(MODEMRESULT_OK); //OK
			modem.verbosemode = verbosemodepending; //New verbose mode, if set!
			return; //Finished processing the command!
		case 'E': //Select local echo?
			switch (modem.ATcommand[pos++]) //What type?
			{
			case '1':
				n0 = 1;
				goto doATE;
			default: //Unknown values are next commands and assume 0!
			case 0:
				--pos; //Next command!
			case '0': //Off?
				n0 = 0;
				doATE:
				if (n0<2) //OK?
				{
					modem.echomode = n0; //Set the communication standard!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //Error!
					return; //Abort!
				}
				break;
			}
			break;
		case 'N': //Automode negotiation?
			switch (modem.ATcommand[pos++]) //What type?
			{
			case '1':
				n0 = 1;
				goto doATN;
			default: //Unknown values are next commands and assume 0!
			case 0:
				--pos; //Next command!
			case '0': //Off?
				n0 = 0;
			doATN:
				if (n0 < 2) //OK?
				{
					//Not handled!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //Error!
					return; //Abort!
				}
				break;
			}
			break;
		case 'D': //Dial?
			do_ATD: //Phonebook ATD!
			switch (modem.ATcommandoriginalcase[pos++]) //What dial command?
			{
			case 'L':
				memcpy(&number,&modem.lastnumber,(safestrlen((char *)&modem.lastnumber[0],sizeof(modem.lastnumber))+1)); //Set the new number to roll!
				goto actondial;
			case 'A': //Reverse to answer mode after dialing?
				goto unsupporteddial; //Unsupported for now!
				dialproperties = 1; //Reverse to answer mode!
				goto actondial;
			case ';': //Remain in command mode after dialing
				dialproperties = 2; //Remain in command mode!
				goto dodial_tone;
			case ',': //Pause for the time specified in register S8(usually 2 seconds)
			case '!': //Flash-Switch hook (Hang up for half a second, as in transferring a call)
				goto unsupporteddial;
			case 0: //EOS?
				--pos; //Next command!
			case 'T': //Tone dial?
			case 'P': //Pulse dial?
			case 'W': //Wait for second dial tone?
			case '@': //Wait for up to	30 seconds for one or more ringbacks
			dodial_tone: //Perform a tone dial!
				//Scan for a remain in command mode modifier!
				for (temppos = pos; temppos < safe_strlen((char *)&modem.ATcommand[0], sizeof(modem.ATcommand)); ++temppos) //Scan the string!
				{
					switch (modem.ATcommand[temppos]) //Check for modifiers in the connection string!
					{
					case ';': //Remain in command mode after dialing
						dialproperties = 2; //Remain in command mode!
						break;
					case ',': //Pause for the time specified in register S8(usually 2 seconds)
					case '!': //Flash-Switch hook (Hang up for half a second, as in transferring a call)
						goto unsupporteddial;
					}
				}
				safestrcpy((char *)&number[0],sizeof(number),(char *)&modem.ATcommandoriginalcase[pos]); //Set the number to dial, in the original case!
				if (safestrlen((char *)&number[0],sizeof(number)) < 2 && number[0]) //Maybe a phone book entry? This is for easy compatiblity for quick dial functionality on unsupported software!
				{
					if (number[0] == ';') //Dialing ';', which means something special?
					{
						goto handleModemCommandEOS; //Special: trigger EOS for OK!
					}
					posbackup = pos; //Save the position!
					if (modemcommand_readNumber(&pos, &n0)) //Read a phonebook entry?
					{
						if (modem.ATcommand[pos] == '\0') //End of string? We're a quick dial!
						{
							if (n0 < 10) //Valid quick dial?
							{
								if (dialnumbers&(1<<n0)) goto badnumber; //Prevent looping!
								goto handleQuickDial; //Handle the quick dial number!
							}
							else //Not a valid quick dial?
							{
								badnumber: //Infinite recursive dictionary detected!
								pos = posbackup; //Return to where we were! It's a normal phonenumber!
							}
						}
						else
						{
							pos = posbackup; //Return to where we were! It's a normal phonenumber!
						}
					}
					else
					{
						pos = posbackup; //Return to where we were! It's a normal phonenumber!
					}
				}
				memset(&modem.lastnumber,0,sizeof(modem.lastnumber)); //Init last number!
				safestrcpy((char *)&modem.lastnumber,sizeof(modem.lastnumber),(char *)&number[0]); //Set the last number!
				actondial: //Start dialing?
				if (modem_connect(number))
				{
					modem_responseResult(MODEMRESULT_CONNECT); //Accept!
					modem.offhook = 2; //On-hook(connect)!
					if (dialproperties!=2) //Not to remain in command mode?
					{
						modem.datamode = 2; //Enter data mode pending!
					}
				}
				else //Dial failed?
				{
					modem_responseResult(MODEMRESULT_NOCARRIER); //No carrier!
				}
				modem.verbosemode = verbosemodepending; //New verbose mode, if set!
				return; //Nothing follows the phone number!
				break;
			case 'S': //Dial phonebook?
				posbackup = pos; //Save for returning later!
				if (modemcommand_readNumber(&pos, &n0)) //Read the number?
				{
					handleQuickDial: //Handle a quick dial!
					pos = posbackup; //Reverse to the dial command!
					--pos; //Return to the dial command!
					if (n0 > NUMITEMS(BIOS_Settings.phonebook)) goto invalidPhonebookNumberDial;
					snprintf((char *)&modem.ATcommand[pos], sizeof(modem.ATcommand) - pos, "%s",(char *)&BIOS_Settings.phonebook[n0]); //Select the phonebook entry based on the number to dial!
					snprintf((char*)&modem.ATcommandoriginalcase[pos], sizeof(modem.ATcommand) - pos, "%s", (char*)&BIOS_Settings.phonebook[n0]); //Select the phonebook entry based on the number to dial!
					if (dialnumbers & (1 << n0)) goto loopingPhonebookNumberDial; //Prevent looping of phonenumbers being quick dialed through the phonebook or through a single-digit phonebook shortcut!
					dialnumbers |= (1 << n0); //Handling noninfinite! Prevent dialing of this entry when quick dialed throuh any method!
					goto do_ATD; //Retry with the new command!
				loopingPhonebookNumberDial: //Loop detected?
					modem_responseResult(MODEMRESULT_NOCARRIER); //No carrier!
					return; //Abort!
				invalidPhonebookNumberDial: //Dialing invalid number?
					modem_responseResult(MODEMRESULT_ERROR);
					return; //Abort!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR);
					return; //Abort!
				}
				break;

			default: //Unsupported?
				--pos; //Retry analyzing!
				goto dodial_tone; //Perform a tone dial on this!
			unsupporteddial: //Unsupported dial function?
				modem_responseResult(MODEMRESULT_ERROR); //Error!
				return; //Abort!
				break;
			}
			break; //Dial?
		case 'A': //Answer?
			switch (modem.ATcommand[pos++]) //What type?
			{
			default: //Unknown values are next commands and assume 0!
			case 0: //EOS?
				--pos; //Next command!
			case '0': //Answer?
				if (modem_connect(NULL)) //Answered?
				{
					modem_Answered(); //Answer!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //Not Connected!
					return; //Abort!
				}
				break;
			}
			break;
		case 'Q': //Quiet mode?
			switch (modem.ATcommand[pos++]) //What type?
			{
			default: //Unknown values are next commands and assume 0!
			case 0: //Assume 0!
				--pos; //Next command!
			case '0': //Answer? All on!
				n0 = 0;
				goto doATQ;
			case '1': //All off!
				n0 = 1;
				goto doATQ;
			case '2': //No ring and no Connect/Carrier in answer mode?
				n0 = 2;
				doATQ:
				if (n0<3)
				{
					verbosemodepending = (n0<<1)|(verbosemodepending&1); //Quiet mode!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //ERROR!
					return; //Abort!
				}
				break;
			}
			break;
		case 'H': //Select communication standard?
			switch (modem.ATcommand[pos++]) //What type?
			{
			case '1':
				n0 = 1;
				goto doATH;
			default: //Unknown values are next commands and assume 0!
			case 0:
				--pos; //Next command!
			case '0': //Off hook?
				n0 = 0;
				doATH:
				if (n0<2) //OK?
				{
					modem.offhook = n0?1:0; //Set the hook status or hang up!
					if ((((modem.connected&1) || modem.ringing)&&(!modem.offhook)) || (modem.offhook && (!((modem.connected&1)||modem.ringing)))) //Disconnected or still ringing/connected?
					{
						if (modem.offhook==0) //On-hook?
						{
							modem_hangup(); //Hang up, if required!
						}
					}
					//Not connected? Simply report OK!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //Error!
					return; //Abort!
				}
				break;
			}
			break;
		case 'B': //Select communication standard?
			switch (modem.ATcommand[pos++]) //What type?
			{
			case '1':
				n0 = 1;
				goto doATB;
			default: //Unknown values are next commands and assume 0!
			case 0:
				--pos; //Next command!
			case '0':
				n0 = 0;
				doATB:
				if (n0<2) //OK?
				{
					modem.communicationstandard = n0; //Set the communication standard!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //Error!
					return; //Abort!
				}
				break;
			}
			break;
		case 'L': //Select speaker volume?
			switch (modem.ATcommand[pos++]) //What type?
			{
			case '1':
				n0 = 1;
				goto doATL;
			case '2':
				n0 = 2;
				goto doATL;
			case '3':
				n0 = 3;
				goto doATL;
			default: //Unknown values are next commands and assume 0!
			case 0:
				--pos; //Next command!
			case '0':
				n0 = 0;
				doATL:
				if (n0<4) //OK?
				{
					modem.speakervolume = n0; //Set the speaker volume!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //Error!
					return; //Abort!
				}
				break;
			}
			break;
		case 'M': //Speaker control?
			switch (modem.ATcommand[pos++]) //What type?
			{
			case '1':
				n0 = 1;
				goto doATM;
			case '2':
				n0 = 2;
				goto doATM;
			case '3':
				n0 = 3;
				goto doATM;
			default: //Unknown values are next commands and assume 0!
			case 0:
				--pos; //Next command!
			case '0':
				n0 = 0;
				doATM:
				if (n0<4) //OK?
				{
					modem.speakercontrol = n0; //Set the speaker control!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //Error!
					return; //Abort!
				}
			}
			break;
		case 'V': //Verbose mode?
			switch (modem.ATcommand[pos++]) //What type?
			{
			case '1':
				n0 = 1;
				goto doATV;
			default: //Unknown values are next commands and assume 0!
			case 0:
				--pos; //Nerxt command!
			case '0':
				n0 = 0;
				doATV:
				if (n0<2) //OK?
				{
					verbosemodepending = ((verbosemodepending&~1)|n0); //Set the verbose mode to numeric(0) or English(1)!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //Error!
					return; //Abort!
				}
				break;
			}
			break;
		case 'X': //Select call progress method?
			switch (modem.ATcommand[pos++]) //What type?
			{
			case '1':
				n0 = 1;
				goto doATX;
			case '2':
				n0 = 2;
				goto doATX;
			case '3':
				n0 = 3;
				goto doATX;
			case '4':
				n0 = 4;
				goto doATX;
			default: //Unknown values are next commands and assume 0!
			case 0:
				--pos; //Next command!
			case '0':
				n0 = 0;
				doATX:
				modem.datamode = 0; //Mode not data!
				if (n0<5) //OK and supported by our emulation?
				{
					modem.callprogressmethod = n0; //Set the speaker control!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //Error!
					return; //Abort!
				}
				break;
			}
			break;
		case 'Z': //Reset modem?
			switch (modem.ATcommand[pos++]) //What type?
			{
			case '1':
				n0 = 1;
				goto doATZ;
			default: //Unknown values are next commands and assume 0!
			case 0: //EOS?
				--pos; //Next command!
			case '0':
				n0 = 0;
				doATZ:
				if (n0<2) //OK and supported by our emulation?
				{
					if (resetModem(n0)) //Reset to the given state!
					{
						//Do nothing when succeeded! Give OK if no other errors occur!
					}
					else
					{
						modem_responseResult(MODEMRESULT_ERROR); //Error!
						return; //Abort!
					}
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //Error!
					return; //Abort!
				}
				break;
			}
			break;
		case 'T': //Tone dial?
		case 'P': //Pulse dial?
			break; //Ignore!
		case 'I': //Inquiry, Information, or Interrogation?
			switch (modem.ATcommand[pos++]) //What type?
			{
			case '1':
				n0 = 1;
				goto doATI;
			case '2':
				n0 = 2;
				goto doATI;
			case '3':
				n0 = 3;
				goto doATI;
			case '4':
				n0 = 4;
				goto doATI;
			case '5':
				n0 = 5;
				goto doATI;
			case '6':
				n0 = 6;
				goto doATI;
			case '7':
				n0 = 7;
				goto doATI;
			case '8':
				n0 = 8;
				goto doATI;
			default: //Unknown values are next commands and assume 0!
			case 0:
				--pos; //Next command!
			case '0':
				n0 = 0;
				doATI:
				if (n0<5) //OK?
				{
					switch (n0) //What request?
					{
					case 3: //Firmware version!
						modem_responseString((byte *)&firmwareversion[0], (1 | 2 | 4)); //Full response!
						break;
					case 4: //Hardware information!
						modem_responseString((byte *)&hardwareinformation[0], (1 | 2 | 4)); //Full response!
						break;
					default: //Unknown!
						//Just respond with a basic OK!
						break;
					}
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR); //Error: line not defined!
					return; //Abort!
				}
				break;
			}
			break;
		case 'O': //Return online?
			switch (modem.ATcommand[pos++]) //What type?
			{
			case '1':
				n0 = 1;
				goto doATO;
			default: //Unknown values are next commands and assume 0!
			case 0:
				--pos; //Next command!
			case '0':
				n0 = 0;
				doATO:
				if (modem.connected & 1) //Connected?
				{
					modem.datamode = 1; //Return to data mode, no result code!
				}
				else
				{
					modem_responseResult(MODEMRESULT_ERROR);
					return; //Abort!
				}
				break;
			}
			break;
		case '?': //Query current register?
			modem_responseNumber(modem.registers[modem.currentregister]); //Give the register value!
			modem.verbosemode = verbosemodepending; //New verbose mode, if set!
			return; //Abort!
			break;
		case '=': //Set current register?
			if (modemcommand_readNumber(&pos,&n0)) //Read the number?
			{
				modem.registers[modem.currentregister] = n0; //Set the register!
				modem_updateRegister(modem.currentregister); //Update the register as needed!
			}
			else
			{
				modem_responseResult(MODEMRESULT_ERROR);
				return; //Abort!
			}
			break;
		case 'S': //Select register n as current register?
			if (modemcommand_readNumber(&pos,&n0)) //Read the number?
			{
				modem.currentregister = n0; //Select the register!
			}
			else
			{
				modem_responseResult(MODEMRESULT_ERROR);
				return; //Abort!
			}
			break;
		case '&': //Extension 1?
			switch (modem.ATcommand[pos++])
			{
			case 0: //EOS?
				modem_responseResult(MODEMRESULT_ERROR); //Error!
				return; //Abort command parsing!
			case 'Q': //Communications mode option?
				switch (modem.ATcommand[pos++]) //What flow control?
				{
				default: //Unknown values are next commands and assume 0!
				case 0:
					--pos; //Next command!
				case '0':
					n0 = 0; //
					goto setAT_EQ;
				case '1':
					n0 = 1; //
					goto setAT_EQ;
				case '2':
					n0 = 2; //
					goto setAT_EQ;
				case '3':
					n0 = 3; //
					goto setAT_EQ;
				case '4':
					n0 = 4; //
					goto setAT_EQ;
				case '5':
					n0 = 5; //
					goto setAT_EQ;
				case '6':
					n0 = 6; //
				setAT_EQ:
					if (n0 < 7) //Valid?
					{
						modem.communicationsmode = n0; //Set communications mode!
					}
					else
					{
						modem_responseResult(MODEMRESULT_ERROR); //Error!
						return; //Abort!
					}
					break;
				}
				break;
			case 'R': //Force CTS high option?
				switch (modem.ATcommand[pos++]) //What flow control?
				{
				default: //Unknown values are next commands and assume 0!
					--pos;
				case '0':
					n0 = 0; //Modem turns on the Clear To Send signal when it detects the Request To Send (RTS) signal from host.
					goto setAT_R;
				case '1':
					n0 = 1; //Modem ignores the Request To Send signal and turns on its Clear To Send signal when ready to receive data.
					goto setAT_R;
				case '2':
					n0 = 2; // *Clear To Send force on.
					setAT_R:
					if (n0<2) //Valid?
					{
						modem.CTSAlwaysActive = n0; //Set flow control!
					}
					else
					{
						modem_responseResult(MODEMRESULT_ERROR); //Error!
						return; //Abort!
					}
					break;
				}
				break;
			case 'C': //Force DCD to be carrier option?
				switch (modem.ATcommand[pos++]) //What flow control?
				{
				default: //Unknown values are next commands and assume 0!
					--pos;
				case '0':
					n0 = 0; // Keep Data Carrier Detect (DCD) signal always ON.
					goto setAT_C;
				case '1':
					n0 = 1; // * Set Data Carrier Detect (DCD) signal according to remote modem data carrier signal.
					setAT_C:
					if (n0<2) //Valid?
					{
						modem.DCDisCarrier = n0; //Set flow control!
					}
					else
					{
						modem_responseResult(MODEMRESULT_ERROR); //Error!
						return; //Abort!
					}
					break;
				}
				break;
			case 'S': //Force DSR high option?
				switch (modem.ATcommand[pos++]) //What flow control?
				{
				default: //Unknown values are next commands and assume 0!
					--pos;
				case '0':
					n0 = 0; // * Data Set Ready is forced on
					goto setAT_S;
				case '1':
					n0 = 1; // Data Set Ready to operate according to RS-232 specification(follow DTR)
					goto setAT_S;
				case '2':
					n0 = 2; //
				setAT_S:
					if (n0<3) //Valid?
					{
						modem.DSRisConnectionEstablished = n0; //Set flow control!
					}
					else
					{
						modem_responseResult(MODEMRESULT_ERROR); //Error!
						return; //Abort!
					}
					break;
				}
				break;
			case 'D': //DTR reponse option?
				switch (modem.ATcommand[pos++]) //What flow control?
				{
				default: //Unknown values are next commands and assume 0!
					--pos;
				case '0':
					n0 = 0; //Ignore DTR line from computer
					goto setAT_D;
				case '1':
					n0 = 1; //Goto AT command state when DTR On->Off
					goto setAT_D;
				case '2':
					n0 = 2; //Hang-up and Command mode when DTR On->Off
					goto setAT_D;
				case '3':
					n0 = 3; //Full reset when DTR On->Off
					setAT_D:
					if (n0<4) //Valid?
					{
						modem.DTROffResponse = n0; //Set DTR off response!
					}
					else
					{
						modem_responseResult(MODEMRESULT_ERROR); //Error!
						return; //Abort!
					}
					break;
				}
				break;
			case 'F': //Load defaults?
				n0 = 0; //Default configuration!
				goto doATZ; //Execute ATZ!
			case 'Z': //Z special?
				n0 = 10; //Default: acnowledge!
				SETGET = 0; //Default: invalid!
				switch (modem.ATcommand[pos++]) //What flow control?
				{
				default:
				case '\0': //EOS?
					goto handlePhoneNumberEntry; //Acnowledge!
					//Ignore!
					break;
				case '0': //Set stored number?
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9': //Might be phonebook?
					n0 = (modem.ATcommand[pos - 1])-(byte)'0'; //Find the number that's to use!
					if (n0 >= NUMITEMS(BIOS_Settings.phonebook))
					{
						n0 = 10; //Invalid entry!
						goto handlePhoneNumberEntry; //Handle it!
					}
					switch (modem.ATcommand[pos++]) //SET/GET detection!
					{
					case '?': //GET?
						SETGET = 1; //GET!
						goto handlePhoneNumberEntry;
						break;
					case '=': //SET?
						SETGET = 2; //SET!
						goto handlePhoneNumberEntry;
						break;
					default: //Invalid command!
						n0 = 10; //Simple acnowledge!
						goto handlePhoneNumberEntry;
						break;
					}
					break;

					handlePhoneNumberEntry: //Handle a phone number dictionary entry!
					if (n0<NUMITEMS(BIOS_Settings.phonebook)) //Valid?
					{
						switch (SETGET) //What kind of set/get?
						{
						case 1: //GET?
							modem_responseString((byte *)&BIOS_Settings.phonebook[n0], 1|2|4); //Give the phonenumber!
							break;
						case 2: //SET?
							memset(&BIOS_Settings.phonebook[n0], 0, sizeof(BIOS_Settings.phonebook[0])); //Init the phonebook entry!
							c = (char *)&modem.ATcommandoriginalcase[pos]; //What phonebook value to set!
							safestrcpy(BIOS_Settings.phonebook[n0], sizeof(BIOS_Settings.phonebook[0]), c); //Set the phonebook entry!
							break;
						default:
							goto ignorePhonebookSETGET;
							break;
						}
					}
					else
					{
						ignorePhonebookSETGET:
						modem_responseResult(MODEMRESULT_ERROR); //Error: invalid phonebook entry or command!
						return; //Abort!
					}
					break;
				}
				break;

			case 'K': //Flow control?
				switch (modem.ATcommand[pos++]) //What flow control?
				{
				default: //Unknown values are next commands and assume 0!
					--pos;
				case '0':
					n0 = 0;
					goto setAT_K;
				case '1':
					goto unsupportedflowcontrol; //Unsupported!
					n0 = 1;
					goto setAT_K;
				case '2':
					goto unsupportedflowcontrol; //Unsupported!
					n0 = 2;
					goto setAT_K;
				case '3':
					n0 = 3;
					goto setAT_K;
				case '4':
					goto unsupportedflowcontrol; //Unsupported!
					n0 = 4;
					setAT_K:
					if (n0<5) //Valid?
					{
						modem.flowcontrol = n0; //Set flow control!
					}
					else
					{
						unsupportedflowcontrol:
						modem_responseResult(MODEMRESULT_ERROR); //Error!
						return; //Abort!
					}
					break;
				}
				break;
			default: //Invalid extension?
				--pos; //Retry analyzing!
				modem_responseResult(MODEMRESULT_ERROR); //Invalid extension!
				return;
				break;
			}
			break;
		case '\\': //Extension 2?
			switch (modem.ATcommand[pos++])
			{
			case 0: //EOS?
				modem_responseResult(MODEMRESULT_ERROR); //Let us handle it!
				return; //Abort processing!
			case 'N': //Flow control?
				switch (modem.ATcommand[pos++]) //What flow control?
				{
				default: //Unknown values are next commands and assume 0!
					--pos; //Next command!
				case '0':
					n0 = 0;
					goto setAT_N;
				case '1':
					n0 = 1;
					goto setAT_N;
				case '2':
					n0 = 2;
					goto setAT_N;
				case '3':
					n0 = 3;
					goto setAT_N;
				case '4':
					n0 = 4;
					goto setAT_N;
				case '5':
					n0 = 5;
					setAT_N:
					if (n0<6) //Valid?
					{
						//Unused!
					}
					else //Error out?
					{
						modem_responseResult(MODEMRESULT_ERROR); //Error!
						return; //Abort!
					}
					break;
				}
				break;
			default: //Invalid extension?
				--pos; //Retry analyzing!
				modem_responseResult(MODEMRESULT_ERROR); //Invalid extension!
				return;
			}
			break;
		default: //Unknown instruction?
			modem_responseResult(MODEMRESULT_ERROR); //Just ERROR unknown commands!
			return; //Abort!
			break;
		} //Switch!
	}
}

void modem_flushCommandCompletion()
{
	//Perform linefeed-related things!
	modem.wascommandcompletionecho = 0; //Disable the linefeed echo!
	modem.wascommandcompletionechoTimeout = (DOUBLE)0; //Stop the timeout!

	//Start execution of the currently buffered command!
	modem.ATcommand[modem.ATcommandsize] = 0; //Terminal character!
	modem.ATcommandsize = 0; //Start the new command!
	modem_executeCommand();
}

byte modem_writeCommandData(byte value)
{
	if (modem.datamode) //Data mode?
	{
		modem.wascommandcompletionecho = 0; //Disable the linefeed echo!
		return modem_sendData(value); //Send the data!
	}
	else //Command mode?
	{
		if (modem.supported >= 2) return 1; //Don't allow sending commands when in passthrough mode!
		modem.timer = 0.0; //Reset the timer when anything is received!
		if (value == '~') //Pause stream for half a second?
		{
			modem.wascommandcompletionecho = 0; //Disable the linefeed echo!
			//Ignore this time?
			if (modem.echomode) //Echo enabled?
			{
				writefifobuffer(modem.inputbuffer, value); //Echo the value back to the terminal!
			}
		}
		else if (value == modem.backspacecharacter) //Backspace?
		{
			modem.wascommandcompletionecho = 0; //Disable the linefeed echo!
			if (modem.ATcommandsize) //Valid to backspace?
			{
				--modem.ATcommandsize; //Remove last entered value!
			}
			if (modem.echomode) //Echo enabled?
			{
				if (fifobuffer_freesize(modem.inputbuffer) >= 3) //Enough to add the proper backspace?
				{
					writefifobuffer(modem.inputbuffer, value); //Previous character movement followed by...
					writefifobuffer(modem.inputbuffer, ' '); //Space to clear the character followed by...
					writefifobuffer(modem.inputbuffer, value); //Another backspace to clear it, if possible!
				}
			}
		}
		else if (value == modem.carriagereturncharacter) //Carriage return? Execute the command!
		{
			if (modem.echomode) //Echo enabled?
			{
				modem.wascommandcompletionecho = 1; //Was command completion with echo!
				writefifobuffer(modem.inputbuffer, value); //Echo the value back to the terminal!
			}
			else
			{
				modem.wascommandcompletionecho = 2; //Was command completion without echo!
			}
			handlemodemCR:
			modem.wascommandcompletionechoTimeout = MODEM_COMMANDCOMPLETIONTIMEOUT; //Start the timeout on command completion!
		}
		else if (value) //Not NULL-terminator? Command byte!
		{
			if (modem.echomode || ((modem.wascommandcompletionecho==1) && (value==modem.linefeedcharacter))) //Echo enabled and command completion with echo?
			{
				if (modem.echomode || ((modem.wascommandcompletionecho == 1) && (value == modem.linefeedcharacter))) //To echo back?
				{
					writefifobuffer(modem.inputbuffer, value); //Echo the value back to the terminal!
				}
				if ((modem.wascommandcompletionecho && (value == modem.linefeedcharacter))) //Finishing echo and start of command execution?
				{
					modem_flushCommandCompletion(); //Start executing the command now!
					return 1; //Don't add to the buffer!
				}
			}
			if (modem.wascommandcompletionecho) //Finishing echo and start of command execution?
			{
				modem_flushCommandCompletion(); //Start executing the command now!
			}
			modem.wascommandcompletionecho = 0; //Disable the linefeed echo from now on!
			if (modem.ATcommandsize < (sizeof(modem.ATcommand) - 1)) //Valid to input(leave 1 byte for the terminal character)?
			{
				modem.ATcommand[modem.ATcommandsize++] = value; //Add data to the string!
				if (modem.ATcommandsize >= 4) //At least AT/at started and another AT/at might be entered after it?
				{
					if ( //Is the command string ended with...
						((modem.ATcommand[modem.ATcommandsize - 1] == 'T') && (modem.ATcommand[modem.ATcommandsize - 2] == 'A')) //Same case AT?
						|| ((modem.ATcommand[modem.ATcommandsize - 1] == 't') && (modem.ATcommand[modem.ATcommandsize - 2] == 'a')) //Same case at?
						)
					{
						fifobuffer_clear(modem.inputbuffer); //Make sure we have enough room for the backspaces to be received!
						for (; modem.ATcommandsize > 2;) //Simulate removing the entire string after AT for any automatic inputs for any parser!
						{
							modem_writeCommandData(modem.backspacecharacter); //Backspace once to remove a character and give a empty backspace character in the removed location!
						}
					}
				}
				if ((modem.ATcommand[0] != 'A') && (modem.ATcommand[0]!='a')) //Not a valid start?
				{
					modem.ATcommand[0] = 0;
					modem.ATcommandsize = 0; //Reset!
				}
				else if ((modem.ATcommandsize == 2) && (modem.ATcommand[1] != '/')) //Invalid repeat or possible attention(AT/at) request!
				{
					if (!( //Not either valid combination of AT or at to get the attention?
						((modem.ATcommand[1] == 'T') && (modem.ATcommand[0] == 'A')) //Same case AT?
						|| ((modem.ATcommand[1] == 't') && (modem.ATcommand[0] == 'a')) //Same case at?
						))
					{
						if ((modem.ATcommand[1] == 'A') || (modem.ATcommand[1] == 'a')) //Another start marker entered?
						{
							modem.ATcommand[0] = modem.ATcommand[1]; //Becomes the new start marker!
							--modem.ATcommandsize; //Just discard to get us back to inputting another one!
						}
						else //Invalid start marker after starting!
						{
							modem.ATcommand[0] = 0;
							modem.ATcommandsize = 0; //Reset!
						}
					}
				}
				else if ((modem.ATcommandsize == 2) && (modem.ATcommand[1] == '/')) //Doesn't need an carriage return?
				{
					if (modem.echomode) //Echo enabled?
					{
						modem.wascommandcompletionecho = 1; //Was command completion with echo!
					}
					else
					{
						modem.wascommandcompletionecho = 0; //Disable the linefeed echo!
					}
					goto handlemodemCR; //Handle the carriage return automatically, because A/ is received!
				}
			}
		}
	}
	return 1; //Received!
}

byte modem_writeData(byte value)
{
	//Handle the data sent to the modem!
	if ((value==modem.escapecharacter) && (modem.supported<2) && (modem.escapecharacter<=0x7F) && ((modem.escaping && (modem.escaping<3)) || ((modem.timer>=modem.escapecodeguardtime) && (modem.escaping==0)))) //Possible escape sequence? Higher values than 127 disables the escape character! Up to 3 escapes after the guard timer is allowed!
	{
		++modem.escaping; //Increase escape info!
	}
	else //Not escaping(anymore)?
	{
		for (;modem.escaping;) //Process escape characters as data!
		{
			if (!modem_writeCommandData(modem.escapecharacter)) //Send it as data/command!
			{
				return 0; //Don't acnowledge the send yet!
			}
			--modem.escaping; //Handle one!
			modem.timer = 0.0; //Reset the timer when anything is received!
		}
		if (!modem_writeCommandData(value)) //Send it as data/command! Not acnowledged?
		{
			return 0; //Don't acnowledge the send yet!
		}
	}
	modem.timer = 0.0; //Reset the timer when anything is received!
	return 1; //Acnowledged and sent!
}

ThreadParams_p pcapthread = NULL; //The pcap thread to use!
void initModem(byte enabled) //Initialise modem!
{
	word numavailableclients;
	word i;
	PacketServer_clientp client;
	memset(&modem, 0, sizeof(modem));
	modem.supported = enabled; //Are we to be emulated?
	if (useSERModem()) //Is this modem enabled?
	{
		modem.port = allocUARTport(); //Try to allocate a port to use!
		if (modem.port==0xFF) //Unable to allocate?
		{
			modem.supported = 0; //Unsupported!
			goto unsupportedUARTModem;
		}
		modem.connectionid = -1; //Default: not connected!
		modem.inputbuffer = allocfifobuffer(MIN(MODEM_TEXTBUFFERSIZE,NUMITEMS(modem.ATcommand)*3),0); //Small input buffer! Make sure it's large enough to contain all command buffer items in backspaces(3 for each character)!
		initPacketServerClients(); //Prepare the clients for use!
		client = Packetserver_unusableclients; //Process all free clients!
		numavailableclients = 0; //How many are allocated!
		for (i = 0; i < MIN(MIN(NUMITEMS(modem.inputdatabuffer),NUMITEMS(modem.outputbuffer)),NUMITEMS(Packetserver_clients)); ++i, client = Packetserver_unusableclients) //Allocate buffers for server and client purposes!
		{
			//A simple 1-byte FIFO for the transmit and receive buffers. This causes the FIFO to become asynchronous like a real modem and not start to buffer entire packets of data that can disrupt the state machines (required for PPP to function).
			modem.inputdatabuffer[i] = allocfifobuffer(1, 0); //Small input buffer!
			modem.outputbuffer[i] = allocfifobuffer(1, 0); //Small input buffer!
			modem.blockoutputbuffer[i] = allocfifobuffer(5, 0); //Buffers for sending block data.
			if (modem.inputdatabuffer[i] && modem.outputbuffer[i] && modem.blockoutputbuffer[i] && Packetserver_unusableclients) //Both allocated?
			{
				++numavailableclients;
				packetserver_moveListItem(client, &Packetserver_freeclients, &Packetserver_unusableclients); //Make the client available for usage!
			}
			else break; //Failed to allocate? Not available client anymore!
		}
		//Reverse the connection list once again to make it proper!
		client = Packetserver_freeclients; //First usable client!
		i = 0; //Init!
		for (;client;client = client->next) //Process all clients again!
		{
			trynextclientalloc:
			if (modem.inputdatabuffer[i] && modem.outputbuffer[i] && modem.blockoutputbuffer[i]) //Both allocated?
			{
				client->connectionnumber = i++; //What connection number to use!
			}
			else
			{
				++i; //Try next!
				goto trynextclientalloc; //Do it!
			}
		}

		if (modem.inputbuffer && modem.inputdatabuffer[0] && modem.outputbuffer[0] && modem.blockoutputbuffer[0]) //Gotten buffers?
		{
			lock(LOCK_PCAPFLAG);
#ifdef PACKETSERVER_ENABLED
			if (pcap_enabled) //Required to actually start the pcap capture?
			{
				pcap_capture = 1; //Make sure that capture is active now!
			}
			else
			{
				pcap_capture = 0; //Make sure that capture is inactive now!
			}
#endif
			unlock(LOCK_PCAPFLAG);
			pcapthread = startThread(&fetchpackets_pcap, "pcapfetch", NULL); //Start the pcap thread for packet capture, if possible!
			if (!pcapthread) //Unavailable?
			{
				goto unsupportedUARTModem;
			}
			modem.connectionport = BIOS_Settings.modemlistenport; //Default port to connect to if unspecified!
			if (modem.connectionport==0) //Invalid?
			{
				modem.connectionport = 23; //Telnet port by default!
			}
			TCP_ConnectServer(modem.connectionport,numavailableclients); //Connect the server on the default port!
			resetModem(0); //Reset the modem to the default state!
			#ifdef IS_LONGDOUBLE
			modem.serverpolltick = (1000000000.0L/(DOUBLE)MODEM_SERVERPOLLFREQUENCY); //Server polling rate of connections!
			modem.networkpolltick = (1000000000.0L/(DOUBLE)MODEM_DATATRANSFERFREQUENCY); //Data transfer polling rate!
			#else
			modem.serverpolltick = (1000000000.0/(DOUBLE)MODEM_SERVERPOLLFREQUENCY); //Server polling rate of connections!
			modem.networkpolltick = (1000000000.0/(DOUBLE)MODEM_DATATRANSFERFREQUENCY); //Data transfer polling rate!
			#endif
			UART_registerdevice(modem.port, &modem_setModemControl, &modem_getstatus, &modem_hasData, &modem_readData, &modem_writeData); //Register our UART device!
		}
		else
		{
			if (modem.inputbuffer) free_fifobuffer(&modem.inputbuffer);
			for (i = 0; i < NUMITEMS(modem.inputdatabuffer); ++i)
			{
				if (modem.outputbuffer[i]) free_fifobuffer(&modem.outputbuffer[i]);
				if (modem.inputdatabuffer[i]) free_fifobuffer(&modem.inputdatabuffer[i]);
				if (modem.blockoutputbuffer[i]) free_fifobuffer(&modem.blockoutputbuffer[i]);
			}
			initPacketServerClients(); //CLear the clients!
		}
	}
	else
	{
		unsupportedUARTModem: //Unsupported!
		modem.inputbuffer = NULL; //No buffer present!
		memset(&modem.inputdatabuffer,0,sizeof(modem.inputdatabuffer)); //No buffer present!
		memset(&modem.outputbuffer, 0, sizeof(modem.outputbuffer)); //No buffer present!
		memset(&modem.blockoutputbuffer, 0, sizeof(modem.blockoutputbuffer)); //No buffer present!
		initPacketServerClients(); //CLear the clients!
	}
}

void PPPOE_finishdiscovery(PacketServer_clientp connectedclient); //Prototype for doneModem!

void doneModem() //Finish modem!
{
	TicksHolder timing;
	word i;
	PacketServer_clientp connectedclient;
	byte DHCPreleaseleasewaiting;
	initTicksHolder(&timing); //Initialize the timing!
	retryReleaseDHCPleasewait:
	DHCPreleaseleasewaiting = 0; //Default: nothing waiting!
	connectedclient = Packetserver_allocatedclients; //Process all allocated clients!
	for (;connectedclient;connectedclient = connectedclient->next) //Process all clients!
	{
		PPPOE_finishdiscovery(connectedclient); //Finish discovery, if needed!
		TCP_DisconnectClientServer(connectedclient->connectionid); //Stop connecting!
		connectedclient->connectionid = -1; //Unused!
		terminatePacketServer(connectedclient); //Stop the packet server, if used!
		if (connectedclient->DHCP_acknowledgepacket.length) //We're still having a lease?
		{
			if (connectedclient->packetserver_useStaticIP < 7) //Not in release phase yet?
			{
				PacketServer_startNextStage(connectedclient, PACKETSTAGE_DHCP);
				connectedclient->packetserver_useStaticIP = 7; //Start the release of the lease!
				connectedclient->used = 2; //Special use case: we're in the DHCP release-only state!
				DHCPreleaseleasewaiting = 1; //Waiting for release!
			}
			else //Still releasing?
			{
				DHCPreleaseleasewaiting = 1; //Waiting for release!
			}
		}
		else //Normal release?
		{
			normalFreeDHCP(connectedclient);
			freePacketserver_client(connectedclient); //Free the client!
		}
	}
	if (DHCPreleaseleasewaiting) //Waiting for release?
	{
		delay(1); //Wait a little bit!
		updateModem(getnspassed(&timing)); //Time the DHCP only!
		goto retryReleaseDHCPleasewait; //Check again!
	}

	if (modem.inputbuffer) //Allocated?
	{
		free_fifobuffer(&modem.inputbuffer); //Free our buffer!
	}
	if (modem.outputbuffer[0] && modem.inputdatabuffer[0]) //Allocated?
	{
		for (i = 0; i < MIN(MIN(NUMITEMS(modem.inputdatabuffer), NUMITEMS(modem.outputbuffer)), NUMITEMS(modem.blockoutputbuffer)); ++i) //Allocate buffers for server and client purposes!
		{
			free_fifobuffer(&modem.outputbuffer[i]); //Free our buffer!
			free_fifobuffer(&modem.blockoutputbuffer[i]); //Free our buffer!
			free_fifobuffer(&modem.inputdatabuffer[i]); //Free our buffer!
		}
	}

	if (TCP_DisconnectClientServer(modem.connectionid)) //Disconnect client, if needed!
	{
		modem.connectionid = -1; //Not connected!
		//The buffers are already released!
	}
	stopTCPServer(); //Stop the TCP server!
	if (pcapthread) //Thread is started?
	{
		lock(LOCK_PCAPFLAG);
		if (strcmp(pcapthread->name, "pcapfetch")) //Pcap thread?
		{
			unlock(LOCK_PCAPFLAG);
			pcapthread = NULL; //Already finished!
			return;
		}
#ifdef PACKETSERVER_ENABLED
		pcap_capture = 0; //Request for the thread to stop!
#endif
		unlock(LOCK_PCAPFLAG);
		delay(1000000); //Wait just a bit for the thread to end!
		waitThreadEnd(pcapthread); //Wait for the capture thread to end!
		pcapthread = NULL; //The pcap thread is stopped!
	}
}

void cleanModem()
{
	//Nothing to do!
}

byte packetServerAddWriteQueue(PacketServer_clientp client, byte data) //Try to add something to the write queue!
{
	byte *newbuffer;
	if (client->packetserver_transmitlength>= client->packetserver_transmitsize) //We need to expand the buffer?
	{
		newbuffer = zalloc(client->packetserver_transmitsize+1024,"MODEM_SENDPACKET",NULL); //Try to allocate a larger buffer!
		if (newbuffer) //Allocated larger buffer?
		{
			memcpy(newbuffer, client->packetserver_transmitbuffer, client->packetserver_transmitsize); //Copy the new data over to the larger buffer!
			freez((void **)&client->packetserver_transmitbuffer, client->packetserver_transmitsize,"MODEM_SENDPACKET"); //Release the old buffer!
			client->packetserver_transmitbuffer = newbuffer; //The new buffer is the enlarged buffer, ready to have been written more data!
			client->packetserver_transmitsize += 1024; //We've been increased to this larger buffer!
			client->packetserver_transmitbuffer[client->packetserver_transmitlength++] = data; //Add the data to the buffer!
			return 1; //Success!
		}
	}
	else //Normal buffer usage?
	{
		client->packetserver_transmitbuffer[client->packetserver_transmitlength++] = data; //Add the data to the buffer!
		return 1; //Success!
	}
	return 0; //Failed!
}

byte packetServerAddPacketBufferQueue(MODEM_PACKETBUFFER *buffer, byte data) //Try to add something to the discovery queue!
{
	byte* newbuffer;
	if (buffer->length >= buffer->size) //We need to expand the buffer?
	{
		newbuffer = zalloc(buffer->size + 1024, "MODEM_SENDPACKET", NULL); //Try to allocate a larger buffer!
		if (newbuffer) //Allocated larger buffer?
		{
			memcpy(newbuffer, buffer->buffer, buffer->size); //Copy the new data over to the larger buffer!
			freez((void **)&buffer->buffer, buffer->size, "MODEM_SENDPACKET"); //Release the old buffer!
			buffer->buffer = newbuffer; //The new buffer is the enlarged buffer, ready to have been written more data!
			buffer->size += 1024; //We've been increased to this larger buffer!
			buffer->buffer[buffer->length++] = data; //Add the data to the buffer!
			return 1; //Success!
		}
	}
	else //Normal buffer usage?
	{
		buffer->buffer[buffer->length++] = data; //Add the data to the buffer!
		return 1; //Success!
	}
	return 0; //Failed!
}

byte packetServerAddPacketBufferQueueBE16(MODEM_PACKETBUFFER* buffer, word data) //Try to add something to the discovery queue!
{
	if (packetServerAddPacketBufferQueue(buffer, ((data>>8) & 0xFF)))
	{
		if (packetServerAddPacketBufferQueue(buffer, (data & 0xFF)))
		{
			return 1; //Success!
		}
	}
	return 0; //Error!
}

byte packetServerAddPacketBufferQueueLE16(MODEM_PACKETBUFFER* buffer, word data) //Try to add something to the discovery queue!
{
	if (packetServerAddPacketBufferQueue(buffer, (data & 0xFF)))
	{
		if (packetServerAddPacketBufferQueue(buffer, ((data >> 8) & 0xFF)))
		{
			return 1; //Success!
		}
	}
	return 0; //Error!
}

void packetServerFreePacketBufferQueue(MODEM_PACKETBUFFER *buffer)
{
	if (buffer->buffer) //Valid buffer to free?
	{
		freez((void**)&buffer->buffer, buffer->size, "MODEM_SENDPACKET"); //Free it!
		buffer->buffer = NULL; //No buffer anymore!
	}
	buffer->size = buffer->length = 0; //No length anymore!
}

char logpacket_outbuffer[0x20001]; //Buffer for storin the data!
char logpacket_filename[256]; //For storing the raw packet that's sent!
void logpacket(byte send, byte *buffer, uint_32 size)
{
	uint_32 i;
	char adding[3];
	memset(&logpacket_filename,0,sizeof(logpacket_filename));
	memset(&logpacket_outbuffer,0,sizeof(logpacket_outbuffer));
	memset(&adding,0,sizeof(adding));
	for (i=0;i<size;++i)
	{
		snprintf(adding,sizeof(adding),"%02X",buffer[i]); //Set and ...
		safestrcat(logpacket_outbuffer,sizeof(logpacket_outbuffer),adding); //... Add!
	}
	if (send)
	{
		dolog("ethernetcard","Sending packet:");
	}
	else
	{
		dolog("ethernetcard","Receiving packet:");
	}
	dolog("ethernetcard","%s",logpacket_outbuffer); //What's received/sent!
}

void authstage_startrequest(DOUBLE timepassed, PacketServer_clientp connectedclient, char *request, byte nextstage)
{
	if (connectedclient->packetserver_stage_byte == PACKETSTAGE_INITIALIZING)
	{
		memset(&connectedclient->packetserver_stage_str, 0, sizeof(connectedclient->packetserver_stage_str));
		safestrcpy(connectedclient->packetserver_stage_str, sizeof(connectedclient->packetserver_stage_str), request);
		connectedclient->packetserver_stage_byte = 0; //Init to start of string!
		connectedclient->packetserver_credentials_invalid = 0; //No invalid field detected yet!
		connectedclient->packetserver_delay = PACKETSERVER_MESSAGE_DELAY; //Delay this until we start transmitting!
	}
	connectedclient->packetserver_delay -= timepassed; //Delaying!
	if ((connectedclient->packetserver_delay <= 0.0) || (!connectedclient->packetserver_delay)) //Finished?
	{
		connectedclient->packetserver_delay = (DOUBLE)0; //Finish the delay!
		if (writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], connectedclient->packetserver_stage_str[connectedclient->packetserver_stage_byte])) //Transmitted?
		{
			if (++connectedclient->packetserver_stage_byte == safestrlen(connectedclient->packetserver_stage_str, sizeof(connectedclient->packetserver_stage_str))) //Finished?
			{
				PacketServer_startNextStage(connectedclient,nextstage); //Prepare for next step!
			}
		}
	}
}

byte ppp_autodetect[3][8] =	{
	{5,0x7E,0xFF,0x03,0xC0,0x21,0,0}, //type 1
	{6,0x7E,0xFF,0x7D,0x23,0xC0,0x21}, //type 2
	{7,0x7E,0x7D,0xDF,0x7D,0x23,0xC0,0x21} //type 3
	};

void packetserver_initStartPPP(PacketServer_clientp connectedclient, byte autodetected); //Prototype

//result: 0: busy, 1: Finished, 2: goto sendoutputbuffer
byte authstage_checkppp(PacketServer_clientp connectedclient, byte datasent)
{
	byte x;
	if (connectedclient->ppp_autodetected) return 1; //Already autodetected!
	if (connectedclient->ppp_autodetectpos<NUMITEMS(connectedclient->ppp_autodetectbuf)) //Can fill?
	{
		connectedclient->ppp_autodetectbuf[connectedclient->ppp_autodetectpos++] = datasent; //Check what's sent!
	}
	for (x=0;x<NUMITEMS(ppp_autodetect);)
	{
		if (connectedclient->ppp_autodetectpos>=ppp_autodetect[x][0]) //Enough buffered to check?
		{
			if (!memcmp(&ppp_autodetect[x][1],&connectedclient->ppp_autodetectbuf,ppp_autodetect[x][0])) //Autodetected?
			{
				connectedclient->ppp_autodetected = 1; //Autodetected!
				packetserver_initStartPPP(connectedclient,1); //Start PPP!
				connectedclient->ppp_sendframing = 3; //The frame was already started, so continue reading it until it ends! Discard it then!
				return 1; //Autodetected!
			}
		}
		++x; //Try next!
	}
	return 0; //Not matched!
}
byte authstage_enterfield(DOUBLE timepassed, PacketServer_clientp connectedclient, char* field, uint_32 size, byte specialinit, char charmask)
{
	byte textinputfield = 0;
	byte isbackspace = 0;
	if (connectedclient->packetserver_stage_byte == PACKETSTAGE_INITIALIZING)
	{
		memset(field, 0, size);
		connectedclient->packetserver_stage_byte = 0; //Init to start filling!
		connectedclient->packetserver_stage_byte_overflown = 0; //Not yet overflown!
		if (specialinit==1) //Special init for protocol?
		{
			#if defined(PACKETSERVER_ENABLED) && !defined(NOPCAP)
			if (!(BIOS_Settings.ethernetserver_settings.users[0].username[0] && BIOS_Settings.ethernetserver_settings.users[0].password[0])) //Gotten no credentials?
			{
				connectedclient->packetserver_credentials_invalid = 0; //Init!
			}
			#endif
		}
	}
	if (peekfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &textinputfield)) //Transmitted?
	{
		isbackspace = (textinputfield == 8) ? 1 : 0; //Are we backspace?
		if (isbackspace) //Backspace?
		{
			if (connectedclient->packetserver_stage_byte == 0) goto ignorebackspaceoutputfield; //To ignore?
			//We're a valid backspace!
			if ((fifobuffer_freesize(modem.blockoutputbuffer[connectedclient->connectionnumber]) < 3) || (!(fifobuffer_freesize(modem.blockoutputbuffer[connectedclient->connectionnumber]) == fifobuffer_size(modem.blockoutputbuffer[connectedclient->connectionnumber])))) //Not enough to contain backspace result?
			{
				return 2; //Not ready to process the writes!
			}
		}
		if (writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], (isbackspace || (textinputfield == '\r') || (textinputfield == '\n') || (!charmask)) ? textinputfield : charmask)) //Echo back to user, encrypted if needed!
		{
			if (isbackspace) //Backspace requires extra data?
			{
				if (!writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], ' ')) return 2; //Clear previous input!
				if (!writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], textinputfield)) return 2; //Another backspace to end up where we need to be!
			}
		ignorebackspaceoutputfield: //Ignore the output part! Don't send back to the user!
			readfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &textinputfield); //Discard the input!
			if (authstage_checkppp(connectedclient,textinputfield)) //PPP autodetected?
			{
				return 0; //Don't do anything! Leave it to the checkppp function!
			}
			if ((textinputfield == '\r') || (textinputfield == '\n')) //Finished?
			{
				if ((connectedclient->lastreceivedCRLFinput == 0) || (textinputfield == connectedclient->lastreceivedCRLFinput)) //Not received LF of CRLF or CR of LFCR?
				{
					field[connectedclient->packetserver_stage_byte] = '\0'; //Finish the string!
					connectedclient->packetserver_credentials_invalid |= connectedclient->packetserver_stage_byte_overflown; //Overflow has occurred?
					connectedclient->lastreceivedCRLFinput = textinputfield; //This was what was last received as the CRLF input!
					return 1; //Finished!
				}
			}
			else
			{
				connectedclient->lastreceivedCRLFinput = 0; //Clear the CRLF received flag: the last was neither!
				if (isbackspace) //Backspace?
				{
					field[connectedclient->packetserver_stage_byte] = '\0'; //Ending!
					if (connectedclient->packetserver_stage_byte) //Non-empty?
					{
						--connectedclient->packetserver_stage_byte; //Previous character!
						field[connectedclient->packetserver_stage_byte] = '\0'; //Erase last character!
					}
				}
				else if ((textinputfield == '\0') || ((connectedclient->packetserver_stage_byte + 1U) >= size) || connectedclient->packetserver_stage_byte_overflown) //Future overflow, overflow already occurring or invalid input to add?
				{
					connectedclient->packetserver_stage_byte_overflown = 1; //Overflow detected!
				}
				else //Valid content to add?
				{
					field[connectedclient->packetserver_stage_byte++] = textinputfield; //Add input!
				}
			}
		}
	}
	return 0; //Still busy!
}

union
{
	word wval;
	byte bval[2]; //Byte of the word values!
} NETWORKVALSPLITTER;

void PPPOE_finishdiscovery(PacketServer_clientp connectedclient)
{
	ETHERNETHEADER ethernetheader, packetheader;
	uint_32 pos; //Our packet buffer location!
	if (!(connectedclient->pppoe_discovery_PADS.buffer && connectedclient->pppoe_discovery_PADS.length)) //Already disconnected?
	{
		return; //No discovery to disconnect!
	}
	memcpy(&ethernetheader.data, &connectedclient->pppoe_discovery_PADS.buffer, sizeof(ethernetheader.data)); //Make a copy of the PADS ethernet header!

	//Send the PADT packet now!
	memcpy(&packetheader.dst, &ethernetheader.src, sizeof(packetheader.dst)); //Make a copy of the ethernet destination to use!
	memcpy(&packetheader.src, &ethernetheader.dst, sizeof(packetheader.src)); //Make a copy of the ethernet source to use!
	memcpy(&packetheader.type, &ethernetheader.type, sizeof(packetheader.type)); //Make a copy of the ethernet type to use!

	packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADT); //Clear the packet!

	//First, the ethernet header!
	for (pos = 0; pos < sizeof(packetheader.data); ++pos)
	{
		packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADT, packetheader.data[pos]); //Send the header!
	}

	//Now, the PADT packet!
	packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADT); //Clear the packet!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADT, 0x11); //V/T!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADT, 0xA7); //PADT!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, connectedclient->pppoe_discovery_PADS.buffer[sizeof(ethernetheader.data)+2]); //Session_ID first byte!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, connectedclient->pppoe_discovery_PADS.buffer[sizeof(ethernetheader.data)+3]); //Session_ID second byte!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, 0x00); //Length first byte!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, 0x00); //Length second byte!
	//Now, the packet is fully ready!
	if (connectedclient->pppoe_discovery_PADR.length != 0x14) //Packet length mismatch?
	{
		packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADT); //PADR not ready to be sent yet!
	}
	else //Send the PADR packet!
	{
		//Send the PADR packet that's buffered!
		if (!sendpkt_pcap(connectedclient,connectedclient->pppoe_discovery_PADT.buffer, connectedclient->pppoe_discovery_PADT.length)) //Send the packet to the network!
		{
			return; //Failed to send!
		}
	}

	//Since we can't be using the buffers after this anyways, free them all!
	packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADI); //No PADI anymore!
	packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADO); //No PADO anymore!
	packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADR); //No PADR anymore!
	packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADS); //No PADS anymore!
	packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADT); //No PADT anymore!
}

byte PPPOE_requestdiscovery(PacketServer_clientp connectedclient)
{
	byte broadcastmac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}; //Broadcast address!
	uint_32 pos; //Our packet buffer location!
	ETHERNETHEADER packetheader;
	//Now, the PADI packet!
	memcpy(&packetheader.dst, broadcastmac, sizeof(packetheader.dst)); //Broadcast it!
	memcpy(&packetheader.src, maclocal, sizeof(packetheader.src)); //Our own MAC address as the source!
	packetheader.type = SDL_SwapBE16(0x8863); //Type!
	packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADI); //Clear the packet!
	for (pos = 0; pos < sizeof(packetheader.data); ++pos)
	{
		packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADI, packetheader.data[pos]); //Send the header!
	}
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADI, 0x11); //V/T!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADI, 0x09); //PADT!
	//Now, the contents of th packet!
	NETWORKVALSPLITTER.wval = SDL_SwapBE16(0); //Session ID!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADI, NETWORKVALSPLITTER.bval[0]); //First byte!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADI, NETWORKVALSPLITTER.bval[1]); //Second byte!
	NETWORKVALSPLITTER.wval = SDL_SwapBE16(0x4); //Length!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADI, NETWORKVALSPLITTER.bval[0]); //First byte!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADI, NETWORKVALSPLITTER.bval[1]); //Second byte!
	NETWORKVALSPLITTER.wval = SDL_SwapBE16(0x0101); //Tag type: Service-Name!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADI, NETWORKVALSPLITTER.bval[0]); //First byte!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADI, NETWORKVALSPLITTER.bval[1]); //Second byte!
	NETWORKVALSPLITTER.wval = SDL_SwapBE16(0); //Tag length!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADI, NETWORKVALSPLITTER.bval[0]); //First byte!
	packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADI, NETWORKVALSPLITTER.bval[1]); //Second byte!

	//Now, the packet is fully ready!
	if (connectedclient->pppoe_discovery_PADI.length != 0x18) //Packet length mismatch?
	{
		packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADI); //PADR not ready to be sent yet!
		return 0; //Failure!
	}
	else //Send the PADR packet!
	{
		//Send the PADR packet that's buffered!
		if (!sendpkt_pcap(connectedclient,connectedclient->pppoe_discovery_PADI.buffer, connectedclient->pppoe_discovery_PADI.length)) //Send the packet to the network!
		{
			return 0; //Failure!
		}
	}
	return 1; //Success!
}

byte PPPOE_handlePADreceived(PacketServer_clientp connectedclient)
{
	uint_32 pos; //Our packet buffer location!
	word length,sessionid,requiredsessionid;
	byte code;
	//Handle a packet that's currently received!
	ETHERNETHEADER ethernetheader, packetheader;
	if (connectedclient->packetserver_slipprotocol_pppoe == 0) return 0; //Invalid: not using PPPOE!
	memcpy(&ethernetheader.data, &connectedclient->packet[0], sizeof(ethernetheader.data)); //Make a copy of the ethernet header to use!
	//Handle the CheckSum after the payload here?
	code = connectedclient->packet[sizeof(ethernetheader.data) + 1]; //The code field!
	if (connectedclient->packet[sizeof(ethernetheader.data)] != 0x11) return 0; //Invalid V/T fields!
	memcpy(&length, &connectedclient->packet[sizeof(ethernetheader.data) + 4],sizeof(length)); //Length field!
	memcpy(&sessionid, &connectedclient->packet[sizeof(ethernetheader.data) + 2], sizeof(sessionid)); //Session_ID field!
	if (connectedclient->pppoe_discovery_PADI.buffer) //PADI sent?
	{
		if(connectedclient->pppoe_discovery_PADO.buffer) //PADO received?
		{
			if (connectedclient->pppoe_discovery_PADR.buffer) //PADR sent?
			{
				if (connectedclient->pppoe_discovery_PADS.buffer==NULL) //Waiting for PADS to arrive?
				{
					if (sessionid) return 0; //No session ID yet!
					if (code != 0x65) return 0; //No PADS yet!
					//We've received our PADO!
					//Ignore it's contents for now(unused) and accept always!
					for (pos = 0; pos < connectedclient->pktlen; ++pos) //Add!
					{
						packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADO, connectedclient->packet[pos]); //Add to the buffer!
					}
					return 1; //Handled!
				}
				else //When PADS is received, we're ready for action for normal communication! Handle PADT packets!
				{
					memcpy(&requiredsessionid, &connectedclient->pppoe_discovery_PADS.buffer[sizeof(ethernetheader.data) + 2], sizeof(sessionid)); //Session_ID field!
					if (code != 0xA7) return 0; //Not a PADT packet?
					if (sessionid != requiredsessionid) return 0; //Not our session ID?
					//Our session has been terminated. Clear all buffers!
					packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADI); //No PADI anymore!
					packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADO); //No PADO anymore!
					packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADR); //No PADR anymore!
					packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADS); //No PADS anymore!
					packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADT); //No PADT anymore!
					return 1; //Handled!
				}
			}
			else //Need PADR to be sent?
			{
				//Send PADR packet now?
				//Ignore the received packet, we can't handle any!
				//Now, the PADR packet again!
				packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADR); //Clear the packet!
				//First, the Ethernet header!
				memcpy(&ethernetheader, &connectedclient->pppoe_discovery_PADO.buffer,sizeof(ethernetheader.data)); //The ethernet header that was used to send the PADO packet!
				memcpy(&packetheader.dst, &ethernetheader.src, sizeof(packetheader.dst)); //Make a copy of the ethernet destination to use!
				memcpy(&packetheader.src, &ethernetheader.dst, sizeof(packetheader.src)); //Make a copy of the ethernet source to use!
				memcpy(&packetheader.type, &ethernetheader.type, sizeof(packetheader.type)); //Make a copy of the ethernet type to use!
				for (pos = 0; pos < sizeof(packetheader.data); ++pos)
				{
					packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, packetheader.data[pos]); //Send the header!
				}
				packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, 0x11); //V/T!
				packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, 0x19); //PADR!
				for (pos = sizeof(ethernetheader.data) + 2; pos < connectedclient->pppoe_discovery_PADO.length; ++pos) //Remainder of the PADO packet copied!
				{
					packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, connectedclient->pppoe_discovery_PADO.buffer[pos]); //Send the remainder of the PADO packet!
				}
				//Now, the packet is fully ready!
				if (connectedclient->pppoe_discovery_PADR.length != connectedclient->pppoe_discovery_PADO.length) //Packet length mismatch?
				{
					packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADR); //PADR not ready to be sent yet!
				}
				else //Send the PADR packet!
				{
					//Send the PADR packet that's buffered!
					if (!sendpkt_pcap(connectedclient,connectedclient->pppoe_discovery_PADR.buffer, connectedclient->pppoe_discovery_PADR.length)) //Send the packet to the network!
					{
						return 0; //Failure!
					}
				}
				return 0; //Not handled!
			}
		}
		else //Waiting for PADO packet response? Parse any PADO responses!
		{
			if (sessionid) return 0; //No session ID yet!
			if (code != 7) return 0; //No PADO yet!
			//We've received our PADO!
			//Ignore it's contents for now(unused) and accept always!
			for (pos = 0; pos < connectedclient->pktlen; ++pos) //Add!
			{
				packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADO, connectedclient->packet[pos]); //Add to the buffer!
			}
			//Send the PADR packet now!
			memcpy(&packetheader.dst, &ethernetheader.src, sizeof(packetheader.dst)); //Make a copy of the ethernet destination to use!
			memcpy(&packetheader.src, &ethernetheader.dst, sizeof(packetheader.src)); //Make a copy of the ethernet source to use!
			memcpy(&packetheader.type, &ethernetheader.type, sizeof(packetheader.type)); //Make a copy of the ethernet type to use!

			//First, the ethernet header!
			for (pos = 0; pos < sizeof(packetheader.data); ++pos)
			{
				packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, packetheader.data[pos]); //Send the header!
			}

			//Now, the PADR packet!
			packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADR); //Clear the packet!
			packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, 0x11); //V/T!
			packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, 0x19); //PADR!
			for (pos = sizeof(ethernetheader.data)+2; pos < connectedclient->pktlen; ++pos) //Remainder of the PADO packet copied!
			{
				packetServerAddPacketBufferQueue(&connectedclient->pppoe_discovery_PADR, connectedclient->packet[pos]); //Send the remainder of the PADO packet!
			}
			//Now, the packet is fully ready!
			if (connectedclient->pppoe_discovery_PADR.length != connectedclient->pktlen) //Packet length mismatch?
			{
				packetServerFreePacketBufferQueue(&connectedclient->pppoe_discovery_PADR); //PADR not ready to be sent yet!
				return 0; //Not handled!
			}
			else //Send the PADR packet!
			{
				//Send the PADR packet that's buffered!
				if (!sendpkt_pcap(connectedclient,connectedclient->pppoe_discovery_PADR.buffer, connectedclient->pppoe_discovery_PADR.length)) //Send the packet to the network!
				{
					return 0; //Failure!
				}
			}
			return 1; //Handled!
		}
	}
	//No PADI sent? Can't handle anything!
	return 0; //Not handled!
}

/*

PPP packet (flag is before and after each packet, which is ignored for packets present(used for framing only). It's location is before and after the packet data, which is unescaped in the buffer):
* start of packet *
address (byte): always 0xFF
control (byte): always 0x03
protocol (word): the protocol that's sent/received.
info: the payload (variable length)
checksum (word or doubleword): HDLC CRC
* end of packet *
*/

//PPP_calcFCS: calculates the FCS of a PPP frame (minus PPP 0x7F bytes). This is transferred in little endian byte order.
//The value of a FCS check including FCS should be 0x0F47 when including the FCS calculated from the sender. When calculating the FCS for sending, the FCS field isn't included in the calculation. The FCS is always stored in little-endian format.

/*
LCP header:
Code (byte)
Length (word): Length including this header.
data (variable): Options as described below for the Option header.
*/

/*
Option header:
Type (byte)
Length (byte): Length including this header
data (variable, depending on the Type as well). Invalid or unrecognised length should result in a Configure-Nak.
*/

typedef struct
{
	byte* data; //Data pointer!
	uint_32 pos; //Reading position within the data!
	uint_32 size; //Size of the data!
} PPP_Stream;

void createPPPstream(PPP_Stream* stream, byte *data, uint_32 size)
{
	stream->data = data;
	stream->pos = 0; //Start of stream!
	stream->size = size; //The size of the stream!
}

byte createPPPsubstream(PPP_Stream* stream, PPP_Stream * substream, uint_32 size)
{
	if ((stream->size==0) && (size)) return 0; //Fail when no size to allocate from or to!
	if (size) //Anything to use?
	{
		if ((stream->size - stream->pos) < size) return 0; //Fail when no room left for the sub-stream!
	}
	substream->data = &stream->data[stream->pos]; //Where to start the sub-stream!
	substream->pos = 0; //Start of stream!
	substream->size = size; //The size of the substream!
	return 1; //The Substream is valid!
}

byte PPP_consumeStream(PPP_Stream* stream, byte* result)
{
	if (stream->pos >= stream->size) return 0; //End of stream reached!
	*result = stream->data[stream->pos]; //Read the data!
	++stream->pos; //Increase pointer in the stream!
	return 1; //Consumed!
}

//result: -1: only managed to read 1 byte(result contains first byte), 0: failed completely, 1: Result read from stream!
sbyte PPP_consumeStreamBE16(PPP_Stream* stream, word* result)
{
	byte temp, temp2;
	if (PPP_consumeStream(stream, &temp)) //First byte!
	{
		if (PPP_consumeStream(stream, &temp2)) //Second byte!
		{
			*result = temp2 | (temp << 8); //Little endian word order!
			return 1; //Success!
		}
		*result = (temp<<8); //What we've read successfully!
		return -1; //Failed at 1 byte!
	}
	return 0; //Failed at 0 bytes!
}


sbyte PPP_consumeStreamLE16(PPP_Stream* stream, word* result)
{
	byte temp, temp2;
	if (PPP_consumeStream(stream, &temp)) //First byte!
	{
		if (PPP_consumeStream(stream, &temp2)) //Second byte!
		{
			*result = temp | (temp2 << 8); //Little endian word order!
			return 1; //Success!
		}
		*result = temp; //What we've read successfully!
		return -1; //Failed at 1 byte!
	}
	return 0; //Failed at 0 bytes!
}

byte PPP_peekStream(PPP_Stream* stream, byte* result)
{
	if (stream->pos >= stream->size) return 0; //End of stream reached!
	*result = stream->data[stream->pos]; //Read the data!
	return 1; //Consumed!
}

uint_32 PPP_streamdataleft(PPP_Stream* stream)
{
	return stream->size - stream->pos; //How much data is left to give!
}

static const word fcslookup[256] =
{
   0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
   0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
   0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E,
   0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
   0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD,
   0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
   0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C,
   0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
   0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB,
   0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
   0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A,
   0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
   0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9,
   0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
   0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
   0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
   0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7,
   0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
   0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036,
   0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
   0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5,
   0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
   0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134,
   0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
   0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3,
   0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
   0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232,
   0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
   0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1,
   0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
   0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330,
   0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78
};

#define PPP_GOODFCS 0xf0b8

word PPP_calcFCS(byte* buffer, uint_32 length)
{
	uint_32 pos;
	word fcs;
	fcs = 0xFFFF; //Starting value!
	for (pos = 0; pos < length; ++pos)
	{
		fcs = (fcs >> 8) ^ fcslookup[(fcs & 0xFF) ^ buffer[pos]]; //Calcalate FCS!
	}
	return fcs; //Don't swap, as this is done by the write only(to provide a little-endian value in the stream)! The result for a checksum is just in our native ordering to check against the good FCS value!
}

byte ip_serveripaddress[4] = { 0xFF,0xFF,0xFF,0xFF }; //Server IP address!

//Addresses are big-endian!
byte ipx_currentnetworknumber[4] = { 0x00,0x00,0x00,0x00 }; //Current network number!
byte ipx_broadcastnetworknumber[4] = { 0xFF,0xFF,0xFF,0xFF }; //Broadcast network number! 
byte ipx_servernetworknumber[4] = { 0x00,0x00,0x00,0x01 }; //Server network number!
byte ipxbroadcastaddr[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}; //IPX Broadcast address
byte ipxnulladdr[6] = {0x00,0x00,0x00,0x00,0x00,0x00 }; //IPX Forbidden NULL address
byte ipx_servernodeaddr[6] = { 0x00,0x00,0x00,0x00,0x00,0x01 }; //IPX server node address!
byte ipnulladdr[4] = { 0x00,0x00,0x00,0x00 }; //IP requesting address when we're to NAK it with the specified address.

byte dummyaddress;
//result: 1 for OK address. 0 for overflow! NULL and Broadcast and special addresses are skipped automatically. addrsizeleft should be 6 (the size of an IPX address)
byte incIPXaddr2(byte* ipxaddr, byte addrsizeleft) //addrsizeleft=6 for the address specified
{
	byte originaladdrsize;
	originaladdrsize = addrsizeleft; //How much is left?
	++*ipxaddr; //Increase the address!
	if (*ipxaddr == 0) //Overflow?
	{
		if (--addrsizeleft) //Something left?
		{
			return incIPXaddr2(--ipxaddr, --addrsizeleft); //Try the next upper byte!
		}
		else //Nothing left to increase?
		{
			return 0; //Error out!
		}
	}
	addrsizeleft = originaladdrsize; //What were we processing?
	if (addrsizeleft == 6) //No overflow for full address?
	{
		if (memcmp(ipxaddr - 5, &ipxbroadcastaddr, sizeof(ipxbroadcastaddr)) == 0) //Broadcast address? all ones.
		{
			dummyaddress = incIPXaddr2(ipxaddr, 6); //Increase to NULL address (forbidden), which we'll skip!
			dummyaddress = incIPXaddr2(ipxaddr, 6); //Increase to server address (forbidden), which we'll skip!
			return incIPXaddr2(ipxaddr, 6); //Increase to the first address, which we'll use!
		}
		if (memcmp(ipxaddr - 5, &ipxnulladdr, sizeof(ipxnulladdr)) == 0) //Null address? all zeroes.
		{
			dummyaddress = incIPXaddr2(ipxaddr, 6); //Increase to server address (forbidden), which we'll skip!
			return incIPXaddr2(ipxaddr, addrsizeleft); //Increase to the first address, which we'll use!
		}
		if (memcmp(ipxaddr - 5, &ipx_servernodeaddr, sizeof(ipx_servernodeaddr)) == 0) //Server address? ~01
		{
			return incIPXaddr2(ipxaddr, 6); //Increase to the next possible address, which we'll use!
		}
	}
	return 1; //Address is OK!
}

//ipxaddr must point to the first byte of the address (it's in big endian format)
byte incIPXaddr(byte* ipxaddr)
{
	return incIPXaddr2(&ipxaddr[5], 6); //Increment the IPX address to a valid address from the LSB!
}

//ppp_responseforuser: a packet for an client has been placed for the client to receive.
void ppp_responseforuser(PacketServer_clientp connectedclient)
{
	//A packet has arrived for an user. Prepare the user data for receiving the packet properly.
	connectedclient->packetserver_packetpos = 0; //Reset packet position!
	connectedclient->packetserver_bytesleft = connectedclient->ppp_response.length; //How much to send!
	connectedclient->PPP_packetreadyforsending = 1; //Ready, not pending anymore!
	connectedclient->PPP_packetstartsent = 0; //Packet hasn't been started yet!
}

//srcaddr should be 12 bytes in length.
byte sendIPXechoreply(PacketServer_clientp connectedclient, PPP_Stream *echodata, PPP_Stream *srcaddr)
{
	byte datab;
	byte result;
	MODEM_PACKETBUFFER response;
	ETHERNETHEADER ppptransmitheader;
	uint_32 skipdatacounter;
	//Now, construct the ethernet header!
	memcpy(&ppptransmitheader.src, &maclocal, 6); //From us!
	ppptransmitheader.dst[0] = 0xFF;
	ppptransmitheader.dst[1] = 0xFF;
	ppptransmitheader.dst[2] = 0xFF;
	ppptransmitheader.dst[3] = 0xFF;
	ppptransmitheader.dst[4] = 0xFF;
	ppptransmitheader.dst[5] = 0xFF; //To a broadcast!
	ppptransmitheader.type = SDL_SwapBE16(0x8137); //We're an IPX packet!

	memset(&response,0,sizeof(response)); //Clear the response to start filling it!

	result = 0; //Default: not success yet!

	for (skipdatacounter = 0; skipdatacounter < 14; ++skipdatacounter)
	{
		if (!packetServerAddPacketBufferQueue(&response, 0)) //Start making room for the header!
		{
			goto ppp_finishpacketbufferqueue_echo; //Keep pending!
		}
	}

	memcpy(&response.buffer[0], ppptransmitheader.data, sizeof(ppptransmitheader.data)); //The ethernet header!
	//Now, create the entire packet as the content for the IPX packet!
	//Header fields
	if (!packetServerAddPacketBufferQueueBE16(&response, 0xFFFF)) //Checksum!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}
	if (!packetServerAddPacketBufferQueueBE16(&response, PPP_streamdataleft(echodata)+30)) //Length!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}
	if (!packetServerAddPacketBufferQueue(&response, 0)) //Control!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}
	if (!packetServerAddPacketBufferQueue(&response, 0x2)) //Echo!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}

	//Now, the destination address, which is the sender of the original request packet!
	for (skipdatacounter = 0; skipdatacounter < 4; ++skipdatacounter)
	{
		if (PPP_consumeStream(srcaddr, &datab)) //The information field itself follows!
		{
			if (!packetServerAddPacketBufferQueue(&response, datab))
			{
				goto ppp_finishpacketbufferqueue_echo;
			}
		}
		else
		{
			goto ppp_finishpacketbufferqueue_echo;
		}
	}
	for (skipdatacounter = 0; skipdatacounter < 6; ++skipdatacounter)
	{
		if (PPP_consumeStream(srcaddr, &datab)) //The information field itself follows!
		{
			if (!packetServerAddPacketBufferQueue(&response, datab))
			{
				goto ppp_finishpacketbufferqueue_echo;
			}
		}
		else
		{
			goto ppp_finishpacketbufferqueue_echo;
		}
	}
	if (!packetServerAddPacketBufferQueueBE16(&response, 0x2)) //Socket!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}
	//Now, the source address, which is our client address for the connected client!
	for (skipdatacounter = 0; skipdatacounter < 4; ++skipdatacounter)
	{
		if (!packetServerAddPacketBufferQueue(&response, connectedclient->ipxcp_networknumber[PPP_RECVCONF][skipdatacounter])) //Our network number!
		{
			goto ppp_finishpacketbufferqueue_echo; //Keep pending!
		}
	}
	for (skipdatacounter = 0; skipdatacounter < 6; ++skipdatacounter)
	{
		if (!packetServerAddPacketBufferQueue(&response, connectedclient->ipxcp_nodenumber[PPP_RECVCONF][skipdatacounter])) //Our network number!
		{
			goto ppp_finishpacketbufferqueue_echo; //Keep pending!
		}
	}
	if (!packetServerAddPacketBufferQueueBE16(&response, 0x2)) //Socket!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}
	//This is followed by the data for from the echo packet!
	for (; PPP_consumeStream(echodata, &datab);) //The information field itself follows!
	{
		if (!packetServerAddPacketBufferQueue(&response, datab))
		{
			goto ppp_finishpacketbufferqueue_echo;
		}
	}
	//End of IPX packet creation.

	//Now, the packet we've stored has become the packet to send!
	if (!sendpkt_pcap(connectedclient,response.buffer, response.length)) //Send the response on the network!
	{
		goto ppp_finishpacketbufferqueue2_echo; //Failed to send!
	}
	result = 1; //Successfully sent!
	goto ppp_finishpacketbufferqueue2_echo;
	ppp_finishpacketbufferqueue_echo: //An error occurred during the response?
	result = 0; //Keep pending until we can properly handle it!
	ppp_finishpacketbufferqueue2_echo:
	packetServerFreePacketBufferQueue(&response); //Free the queued response!
	return result; //Give the result!
}

//Send an IPX echo request to the network for all other existing clients to apply.
byte sendIPXechorequest(PacketServer_clientp connectedclient)
{
	byte result;
	MODEM_PACKETBUFFER response;
	ETHERNETHEADER ppptransmitheader;
	uint_32 skipdatacounter;
	//Now, construct the ethernet header!
	memcpy(&ppptransmitheader.src, &maclocal, 6); //From us!
	ppptransmitheader.dst[0] = 0xFF;
	ppptransmitheader.dst[1] = 0xFF;
	ppptransmitheader.dst[2] = 0xFF;
	ppptransmitheader.dst[3] = 0xFF;
	ppptransmitheader.dst[4] = 0xFF;
	ppptransmitheader.dst[5] = 0xFF; //To a broadcast!
	ppptransmitheader.type = SDL_SwapBE16(0x8137); //We're an IPX packet!

	memset(&response,0,sizeof(response)); //Clear the response to start filling it!

	for (skipdatacounter = 0; skipdatacounter < 14; ++skipdatacounter)
	{
		if (!packetServerAddPacketBufferQueue(&response, 0)) //Start making room for the header!
		{
			goto ppp_finishpacketbufferqueue_echo; //Keep pending!
		}
	}

	memcpy(&response.buffer[0], ppptransmitheader.data, sizeof(ppptransmitheader.data)); //The ethernet header!
	//Now, create the entire packet as the content for the IPX packet!
	//Header fields
	if (!packetServerAddPacketBufferQueueBE16(&response, 0xFFFF)) //Checksum!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}
	if (!packetServerAddPacketBufferQueueBE16(&response, 30)) //Length!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}
	if (!packetServerAddPacketBufferQueue(&response, 0)) //Control!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}
	if (!packetServerAddPacketBufferQueue(&response, 0x2)) //Echo!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}

	//Now, the destination address, which is the sender of the original request packet!
	for (skipdatacounter = 0; skipdatacounter < 4; ++skipdatacounter)
	{
		if (!packetServerAddPacketBufferQueue(&response, connectedclient->ipxcp_networknumberecho[skipdatacounter]))
		{
			goto ppp_finishpacketbufferqueue_echo;
		}
	}
	for (skipdatacounter = 0; skipdatacounter < 6; ++skipdatacounter)
	{
		if (!packetServerAddPacketBufferQueue(&response, 0xFF)) //Specified address FFFFFFFF port FFFF!
		{
			goto ppp_finishpacketbufferqueue_echo;
		}
	}
	if (!packetServerAddPacketBufferQueueBE16(&response, 0x2)) //Socket!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}
	//Now, the source address, which is our client address for the connected client!
	for (skipdatacounter = 0; skipdatacounter < 4; ++skipdatacounter)
	{
		if (!packetServerAddPacketBufferQueue(&response, ipx_servernetworknumber[skipdatacounter])) //Our network number!
		{
			goto ppp_finishpacketbufferqueue_echo; //Keep pending!
		}
	}
	for (skipdatacounter = 0; skipdatacounter < 6; ++skipdatacounter)
	{
		if (!packetServerAddPacketBufferQueue(&response, ipx_servernodeaddr[skipdatacounter])) //Our node number to send back to!
		{
			goto ppp_finishpacketbufferqueue_echo; //Keep pending!
		}
	}
	if (!packetServerAddPacketBufferQueueBE16(&response, 0x2)) //Socket!
	{
		goto ppp_finishpacketbufferqueue_echo; //Keep pending!
	}
	//This is followed by the data for from the echo packet!
	//Don't need to send any data to echo back, as this isn't used for this case.
	//End of IPX packet creation.

	//Now, the packet we've stored has become the packet to send!
	if (!sendpkt_pcap(connectedclient,response.buffer, response.length)) //Send the response on the network!
	{
		goto ppp_finishpacketbufferqueue_echo; //Failed to send!
	}
	result = 1; //Successfully sent!
	goto ppp_finishpacketbufferqueue2_echo;
ppp_finishpacketbufferqueue_echo: //An error occurred during the response?
	result = 0; //Keep pending until we can properly handle it!
ppp_finishpacketbufferqueue2_echo:
	packetServerFreePacketBufferQueue(&response); //Free the queued response!
	return result; //Give the result!
}

//result: 0: success, 1: error
byte PPP_addPPPheader(PacketServer_clientp connectedclient, MODEM_PACKETBUFFER* response, byte allowheadercompression, word protocol)
{
	word c;
	//Don't compress the header yet, since it's still negotiating!
	if (!(
		(connectedclient->PPP_headercompressed[PPP_SENDCONF] && allowheadercompression) || //Not with header compression!
		((protocol==0x2B) && (connectedclient->ppp_IPXCPstatus[PPP_SENDCONF]==2)) || //Not with SNAP mode!
		((protocol == 0x2B) && (connectedclient->ppp_IPXCPstatus[PPP_SENDCONF] == 3)) || //Not with Ethernet II mode!
		((protocol == 0x2B) && (connectedclient->ppp_IPXCPstatus[PPP_SENDCONF] == 4)) //Not with RAW IPX mode!
		|| (protocol==0xC021))) //Header isn't compressed? LCP is never compressed!
	{
		if (!packetServerAddPacketBufferQueue(response, 0xFF)) //Start of PPP header!
		{
			return 1; //Finish up!
		}
		if (!packetServerAddPacketBufferQueue(response, 0x03)) //Start of PPP header!
		{
			return 1; //Finish up!
		}
	}
	if ((connectedclient->ppp_IPXCPstatus[PPP_SENDCONF]==2) && (protocol==0x2B)) //Special headers for SNAP IPX?
	{
		if (!packetServerAddPacketBufferQueue(response, 0x00)) //Pad!
		{
			return 1; //Finish up!
		}
		if (!packetServerAddPacketBufferQueue(response, 0x00)) //OUI 0!
		{
			return 1; //Finish up!
		}
		if (!packetServerAddPacketBufferQueue(response, 0x00)) //OUI 1!
		{
			return 1; //Finish up!
		}
		if (!packetServerAddPacketBufferQueue(response, 0x00)) //OUI 2!
		{
			return 1; //Finish up!
		}
		if (!packetServerAddPacketBufferQueue(response, 0x81)) //EtherType: IPX!
		{
			return 1; //Finish up!
		}
		if (!packetServerAddPacketBufferQueue(response, 0x37)) //EtherType: IPX!
		{
			return 1; //Finish up!
		}
	}
	else if ((connectedclient->ppp_IPXCPstatus[PPP_SENDCONF] == 3) && (protocol == 0x2B)) //Special headers for Ethernet II IPX?
	{
		for (c = 0; c < 12;++c) //Ethnernet dest/src address!
		{
			if (!packetServerAddPacketBufferQueue(response, 0xFF)) //Only broadcast MAC!
			{
				return 1; //Finish up!
			}
		}
		if (!packetServerAddPacketBufferQueue(response, 0x81)) //EtherType: IPX!
		{
			return 1; //Finish up!
		}

		if (!packetServerAddPacketBufferQueue(response, 0x37)) //EtherType: IPX!
		{
			return 1; //Finish up!
		}
	}
	else if (!((connectedclient->ppp_IPXCPstatus[PPP_SENDCONF] == 4) && (protocol == 0x2B)))  //Normal PPP header! Not with IPX raw mode!
	{
		if ((protocol != 0xC021) && (connectedclient->PPP_protocolcompressed[PPP_SENDCONF]) && ((protocol & 0xFF) == protocol) && (protocol&1)) //Protocol can be compressed?
		{
			if (!packetServerAddPacketBufferQueue(response, (protocol & 0xFF))) //The protocol, compressed!
			{
				return 1; //Finish up!
			}
		}
		else //Uncompressed protocol?
		{
			if (!packetServerAddPacketBufferQueueBE16(response, protocol)) //The protocol!
			{
				return 1; //Finish up!
			}
		}
	}
	return 0; //Success!
}

//result: 0: success, 1: error
byte PPP_addLCPNCPResponseHeader(PacketServer_clientp connectedclient, MODEM_PACKETBUFFER* response, byte allowheadercompression, word protocol, byte responsetype, byte common_IdentifierField, word contentlength)
{
	if (PPP_addPPPheader(connectedclient, response, allowheadercompression, protocol))
	{
		return 1; //Finish up!
	}
	//Request-Ack header!
	if (!packetServerAddPacketBufferQueue(response, responsetype)) //Response type!
	{
		return 1; //Finish up!
	}
	if (!packetServerAddPacketBufferQueue(response, common_IdentifierField)) //Identifier!
	{
		return 1; //Finish up!
	}
	if (!packetServerAddPacketBufferQueueBE16(response, contentlength + 4)) //How much data follows!
	{
		return 1; //Finish up!
	}
	return 0; //Success!
}

//result: 0: success, 1: error
byte PPP_addFCS(MODEM_PACKETBUFFER* response, PacketServer_client *connectedclient, word protocol)
{
	word checksumfield;
	if (!(
		((protocol == 0x2B) && (connectedclient->ppp_IPXCPstatus[PPP_SENDCONF] == 2)) || //Not with SNAP mode!
		((protocol == 0x2B) && (connectedclient->ppp_IPXCPstatus[PPP_SENDCONF] == 3)) || //Not with Ethernet II mode!
		((protocol == 0x2B) && (connectedclient->ppp_IPXCPstatus[PPP_SENDCONF] == 4)) //Now with RAW IPX mode!
		)
		) //Using FCS for this packet type?
	{
		//Calculate and add the checksum field!
		checksumfield = PPP_calcFCS(response->buffer, response->length); //The checksum field!
			if (!packetServerAddPacketBufferQueueLE16(response, (checksumfield ^ 0xFFFF))) //Checksum failure? For some reason this is in little-endian format and complemented.
			{
				return 1;
			}
	}
	return 0;
}

byte no_magic_number[4] = { 0,0,0,0 }; //No magic number used!
byte no_network_number[4] = { 0,0,0,0 }; //No network number used!
byte no_node_number[6] = { 0,0,0,0,0,0 }; //No node number used!

//result: 1 on success, 0 on pending. When handleTransmit==1, false blocks the transmitter from handling new packets.
byte PPP_parseSentPacketFromClient(PacketServer_clientp connectedclient, byte handleTransmit)
{
	MODEM_PACKETBUFFER pppNakRejectFields;
	byte result; //The result for this function!
	MODEM_PACKETBUFFER response, pppNakFields, pppRejectFields; //The normal response and Nak fields/Reject fields that are queued!
	MODEM_PACKETBUFFER LCP_requestFields; //Request fields!
	word checksum;
	PPP_Stream pppstream, pppstreambackup, pppstream_protocolstreambackup, pppstream_protocolstreambackup2, pppstream_informationfield, pppstream_requestfield /*, pppstream_optionfield*/;
	byte datab; //byte data from the stream!
	word dataw; //word data from the stream!
	byte data4[4] = {0,0,0,0}; //4-byte data!
	byte data6[6] = {0,0,0,0,0,0}; //6-byte data!
	word protocol; //The used protocol!
	//Header at the start of the info field!
	byte common_CodeField; //Code field!
	byte common_IdentifierField; //Identifier field!
	word common_LengthField; //Length field!
	byte common_TypeField; //Type field
	byte common_OptionLengthField; //Option Length field!
	byte request_NakRejectpendingMRU; //Pending MTU field for the request!
	word request_pendingMRU; //Pending MTU field for the request!
	byte request_pendingProtocolFieldCompression; //Default: no protocol field compression!
	byte request_pendingAddressAndControlFieldCompression; //Default: no address-and-control-field compression!
	byte request_magic_number_used; //Default: none
	byte request_magic_number[4]; //Set magic number
	byte request_asynccontrolcharactermap[4]; //ASync-Control-Character-Map MSB to LSB (Big Endian)!
	byte request_asynccontrolcharactermapspecified; //Default: none
	word request_authenticationprotocol; //Authentication protocol requested!
	byte request_authenticationspecified; //Authentication protocol used!
	uint_32 skipdatacounter;
	byte pap_fieldcounter; //Length of a field until 0 for PAP comparison!
	byte username_length; //Username length for PAP!
	byte password_length; //Password length for PAP!
	byte pap_authenticated; //Is the user authenticated properly?
	byte nakreject_ipxcp;
	byte nakreject_ipcp;
	byte request_NakRejectnetworknumber;
	byte ipxcp_pendingnetworknumber[4];
	byte request_NakRejectnodenumber;
	byte ipxcp_pendingnodenumber[6];
	byte request_NakRejectroutingprotocol;
	word ipxcp_pendingroutingprotocol;
	byte request_NakRejectipaddress;
	byte ipcp_pendingipaddress[4];
	byte ipcp_pendingDNS1ipaddress[4];
	byte ipcp_pendingDNS2ipaddress[4];
	byte ipcp_pendingNBNS1ipaddress[4];
	byte ipcp_pendingNBNS2ipaddress[4];
	byte ipcp_pendingsubnetmaskipaddress[4];
	byte IPnumbers[4];
	char* p; //Pointer for IP address
	ETHERNETHEADER ppptransmitheader;
	IPXPACKETHEADER ipxtransmitheader;
	word c; //For user login.
	byte performskipdataNak;
	performskipdataNak = 0; //Default: not skipping anything!
	if (handleTransmit)
	{
		if (connectedclient->packetserver_transmitlength < (3 + ((!connectedclient->PPP_protocolcompressed[PPP_RECVCONF]) ? 1U : 0U) + ((!connectedclient->PPP_headercompressed[PPP_RECVCONF]) ? 2U : 0U))) //Not enough for a full minimal PPP packet (with 1 byte of payload)?
		{
			return 1; //Incorrect packet: discard it!
		}
	}
	memset(&response, 0, sizeof(response)); //Make sure it's ready for usage!
	memset(&pppNakFields, 0, sizeof(pppNakFields)); //Make sure it's ready for usage!
	memset(&pppRejectFields, 0, sizeof(pppRejectFields)); //Make sure it's ready for usage!
	//TODO: ipxcp nakfields/rejectfields.
	if (
		(connectedclient->ppp_nakfields.buffer || connectedclient->ppp_rejectfields.buffer) || //LCP NAK or Reject packet pending?
		(connectedclient->ppp_nakfields_ipxcp.buffer || connectedclient->ppp_rejectfields_ipxcp.buffer) || //IPXCP NAK or Reject packet pending?
		(connectedclient->ppp_nakfields_ipcp.buffer || connectedclient->ppp_rejectfields_ipcp.buffer) //IPCP NAK or Reject packet pending?
		)
	{
		//Try to send the NAK fields or Reject fields to the client!
		if (!handleTransmit) //Not transmitting?
		{
			result = 0; //Default: not handled!
		}
		else
		{
			result = 1; //Default: handled!
		}
		nakreject_ipxcp = 0; //Not IPXCP by default!
		nakreject_ipcp = 0; //Not IPCP by default!
		if (connectedclient->ppp_nakfields.buffer) //Gotten NAK fields to send?
		{
			memcpy(&pppNakRejectFields, &connectedclient->ppp_nakfields, sizeof(pppNakRejectFields)); //Which one to use!
			common_CodeField = 3; //NAK!
			common_IdentifierField = connectedclient->ppp_nakfields_identifier; //The identifier!
		}
		else if (connectedclient->ppp_nakfields_ipxcp.buffer) //Gotten NAK fields to send?
		{
			memcpy(&pppNakRejectFields, &connectedclient->ppp_nakfields_ipxcp, sizeof(pppNakRejectFields)); //Which one to use!
			common_CodeField = 3; //NAK!
			common_IdentifierField = connectedclient->ppp_nakfields_ipxcp_identifier; //The identifier!
			nakreject_ipxcp = 1; //IPXCP!
		}
		else if (connectedclient->ppp_nakfields_ipcp.buffer) //Gotten NAK fields to send?
		{
			memcpy(&pppNakRejectFields, &connectedclient->ppp_nakfields_ipcp, sizeof(pppNakRejectFields)); //Which one to use!
			common_CodeField = 3; //NAK!
			common_IdentifierField = connectedclient->ppp_nakfields_ipcp_identifier; //The identifier!
			nakreject_ipcp = 1; //IPCP!
		}
		else if (connectedclient->ppp_rejectfields.buffer) //Gotten Reject fields to send?
		{
			memcpy(&pppNakRejectFields, &connectedclient->ppp_rejectfields, sizeof(pppNakRejectFields)); //Which one to use!
			common_CodeField = 4; //Reject!
			common_IdentifierField = connectedclient->ppp_rejectfields_identifier; //The identifier!
		}
		else if (connectedclient->ppp_rejectfields_ipxcp.buffer) //Gotten NAK fields to send?
		{
			memcpy(&pppNakRejectFields, &connectedclient->ppp_rejectfields_ipxcp, sizeof(pppNakRejectFields)); //Which one to use!
			common_CodeField = 4; //Reject!
			common_IdentifierField = connectedclient->ppp_rejectfields_ipxcp_identifier; //The identifier!
			nakreject_ipxcp = 1; //IPXCP!
		}
		else //Gotten NAK fields to send?
		{
			memcpy(&pppNakRejectFields, &connectedclient->ppp_rejectfields_ipcp, sizeof(pppNakRejectFields)); //Which one to use!
			common_CodeField = 4; //Reject!
			common_IdentifierField = connectedclient->ppp_rejectfields_ipcp_identifier; //The identifier!
			nakreject_ipcp = 1; //IPXCP!
		}

		createPPPstream(&pppstream, &pppNakRejectFields.buffer[0], pppNakRejectFields.length); //Create a stream object for us to use, which goes until the end of the payload!

		//Send a Reject/NAK packet to the client!
		memset(&response, 0, sizeof(response)); //Init the response!
		if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, (nakreject_ipcp?0x8021:(nakreject_ipxcp?0x802B:0xC021)), common_CodeField, common_IdentifierField, PPP_streamdataleft(&pppstream)))
		{
			goto ppp_finishpacketbufferqueueNAKReject;
		}
		//Now, the rejected packet itself!
		for (; PPP_consumeStream(&pppstream, &datab);) //The data field itself follows!
		{
			if (!packetServerAddPacketBufferQueue(&response, datab))
			{
				memset(&pppNakRejectFields, 0, sizeof(pppNakRejectFields)); //Abort!
				goto ppp_finishpacketbufferqueueNAKReject;
			}
		}
		//Calculate and add the checksum field!
		if (PPP_addFCS(&response,connectedclient, (nakreject_ipcp ? 0x8021 : (nakreject_ipxcp ? 0x802B : 0xC021))))
		{
			goto ppp_finishpacketbufferqueueNAKReject;
		}

		//Packet is fully built. Now send it!
		if (connectedclient->ppp_response.size) //Previous Response still valid?
		{
			goto ppp_finishpacketbufferqueueNAKReject; //Keep pending!
		}
		if (response.buffer) //Any response to give?
		{
			memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
			ppp_responseforuser(connectedclient); //A response is ready!
			memset(&response, 0, sizeof(response)); //Parsed!
			if (common_CodeField == 3) //NAK?
			{
				if (nakreject_ipcp) //IPCP?
				{
					packetServerFreePacketBufferQueue(&connectedclient->ppp_nakfields_ipcp); //Free the queued response!
				}
				else if (nakreject_ipxcp) //IPXCP?
				{
					packetServerFreePacketBufferQueue(&connectedclient->ppp_nakfields_ipxcp); //Free the queued response!
				}
				else //LCP?
				{
					packetServerFreePacketBufferQueue(&connectedclient->ppp_nakfields); //Free the queued response!
				}
			}
			else //Reject?
			{
				if (nakreject_ipcp) //IPCP?
				{
					packetServerFreePacketBufferQueue(&connectedclient->ppp_rejectfields_ipcp); //Free the queued response!
				}
				else if (nakreject_ipxcp) //IPXCP?
				{
					packetServerFreePacketBufferQueue(&connectedclient->ppp_rejectfields_ipxcp); //Free the queued response!
				}
				else //LCP?
				{
					packetServerFreePacketBufferQueue(&connectedclient->ppp_rejectfields); //Free the queued response!
				}
			}
			if (!handleTransmit) //Not performing an transmit?
			{
				result = 1; //OK, handled!
			}
			else
			{
				result = 0; //Keep pending!
			}
		}
		goto ppp_finishcorrectpacketbufferqueueNAKReject; //Success!
	ppp_finishpacketbufferqueueNAKReject: //An error occurred during the response?
		packetServerFreePacketBufferQueue(&response); //Free the queued response!
		//Don't touch the NakReject field, as this is still pending!
	ppp_finishcorrectpacketbufferqueueNAKReject: //Correctly finished!
		return result; //Keep pending, is selected!
	}
	else if ((!handleTransmit) && (!connectedclient->ppp_LCPstatus[1])) //Not handling a transmitting of anything atm and LCP for the server-client is down?
	{
		//Use a simple nanosecond timer to determine if we're to send a 
		connectedclient->ppp_serverLCPrequesttimer += modem.networkpolltick; //Time!
		if (connectedclient->ppp_serverLCPrequesttimer < ((!connectedclient->ppp_serverLCPstatus) ? 3000000000.0f : 500000000.0f)) //Starting it's timing every interval (first 3 seconds, then half a second)!
		{
			goto donthandleServerPPPLCPyet; //Don't handle the sending of a request from the server yet, because we're still timing!
		}
		if (!connectedclient->ppp_serverLCPstatus) //Initializing?
		{
			connectedclient->ppp_serverLCPidentifier = 0; //Init!
			retryServerLCPnegotiation:
			connectedclient->ppp_serverLCPstatus = 1; //Have initialized!
			connectedclient->ppp_serverLCP_haveAddressAndControlFieldCompression = connectedclient->ppp_serverLCP_haveMRU = connectedclient->ppp_serverLCP_haveMagicNumber = connectedclient->ppp_serverLCP_haveProtocolFieldCompression = connectedclient->ppp_serverLCP_haveAsyncControlCharacterMap = connectedclient->ppp_serverLCP_haveAuthenticationProtocol = 1; //Default by trying all!
			connectedclient->ppp_serverLCP_haveAuthenticationProtocol = 0; //No authentication protocol from the server!
			connectedclient->ppp_serverLCP_pendingMRU = 1500; //Default!
			connectedclient->ppp_serverLCP_pendingMagicNumber[0] = 0xFF; //Default!
			connectedclient->ppp_serverLCP_pendingMagicNumber[1] = 0xFF; //Default!
			connectedclient->ppp_serverLCP_pendingMagicNumber[2] = 0xFF; //Default!
			connectedclient->ppp_serverLCP_pendingMagicNumber[3] = 0xFF; //Default!
			connectedclient->ppp_serverLCP_haveAsyncControlCharacterMap = 1;
			connectedclient->ppp_serverLCP_pendingASyncControlCharacterMap[0] = connectedclient->ppp_serverLCP_pendingASyncControlCharacterMap[2] = connectedclient->ppp_serverLCP_pendingASyncControlCharacterMap[3] = 0; //Default!
			connectedclient->ppp_serverLCP_pendingASyncControlCharacterMap[1] = 0x0A; //Microsoft-defined: A0000 in the Control-Character map: characters 11h and 13h.
		}
		else if (connectedclient->ppp_serverLCPstatus>1) //Resetting?
		{
			++connectedclient->ppp_serverLCPidentifier; //New identifier to start using!
			//Otherwise, it's a retry!
			if (connectedclient->ppp_serverLCPstatus == 2) //Resetting?
			{
				goto retryServerLCPnegotiation;
			}
			else
			{
				connectedclient->ppp_serverLCPstatus = 1; //Ready for use for now!
			}
		}
		result = 1; //Default: handled!
		//Now, formulate a request!
		connectedclient->ppp_servercurrentLCPidentifier = connectedclient->ppp_serverLCPidentifier; //Load the identifier to try!
		memset(&LCP_requestFields, 0, sizeof(LCP_requestFields)); //Make sure it's ready for usage!
		//case 1: //Maximum Receive Unit
			if (connectedclient->ppp_serverLCP_haveMRU) //Required?
			{
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 0x01)) //Request it!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 4)) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueueBE16(&LCP_requestFields, connectedclient->ppp_serverLCP_pendingMRU)) //Requested data!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
			}
			//Field is OK!
		//case 7: //Protocol Field Compression
			if (connectedclient->ppp_serverLCP_haveProtocolFieldCompression) //To request?
			{
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 0x07)) //NAK it!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 2)) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
			}
		//case 8: //Address-And-Control-Field-Compression
			if (connectedclient->ppp_serverLCP_haveAddressAndControlFieldCompression) //To request?
			{
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 0x08)) //NAK it!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 2)) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
			}
			request_pendingAddressAndControlFieldCompression = 1; //Set the request!
		//case 5: //Magic Number
			if (connectedclient->ppp_serverLCP_haveMagicNumber) //To request?
			{
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 0x05)) //NAK it!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 6)) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverLCP_pendingMagicNumber[0])) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverLCP_pendingMagicNumber[1])) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverLCP_pendingMagicNumber[2])) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverLCP_pendingMagicNumber[3])) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
			}
		//case 3: //Authentication Protocol
			if (connectedclient->ppp_serverLCP_haveAuthenticationProtocol || connectedclient->ppp_autodetected) //Autodetect requires PAP?
			{
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 0x03)) //NAK it!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 4)) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueueBE16(&LCP_requestFields, 0xC023)) //PAP is the only one that's currently supported!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
			}
		//case 2: //ASync-Control-Character-Map
			if (connectedclient->ppp_serverLCP_haveAsyncControlCharacterMap) //To request?
			{
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 0x02)) //NAK it!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 6)) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverLCP_pendingASyncControlCharacterMap[0])) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverLCP_pendingASyncControlCharacterMap[1])) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverLCP_pendingASyncControlCharacterMap[2])) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverLCP_pendingASyncControlCharacterMap[3])) //Correct length!
				{
					goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
				}
			}

		createPPPstream(&pppstream, LCP_requestFields.buffer, LCP_requestFields.length); //Create a stream object for us to use, which goes until the end of the payload!
		if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, 0xC021, 0x01, connectedclient->ppp_servercurrentLCPidentifier, PPP_streamdataleft(&pppstream))) //Configure-Request
		{
			goto ppp_finishpacketbufferqueue_lcp; //Finish up!
		}

		for (; PPP_streamdataleft(&pppstream);) //Data left?
		{
			if (!PPP_consumeStream(&pppstream, &datab))
			{
				goto ppp_finishpacketbufferqueue_lcp; //Incorrect packet: discard it!
			}
			if (!packetServerAddPacketBufferQueue(&response, datab)) //Add it!
			{
				goto ppp_finishpacketbufferqueue_lcp; //Finish up!
			}
		}

		//Calculate and add the checksum field!
		if (PPP_addFCS(&response,connectedclient,0xC021))
		{
			goto ppp_finishpacketbufferqueue_lcp;
		}

		//Packet is fully built. Now send it!
		if (connectedclient->ppp_response.size) //Previous Response still valid?
		{
			goto ppp_finishpacketbufferqueue_lcp; //Keep pending!
		}
		if (response.buffer) //Any response to give?
		{
			memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
			ppp_responseforuser(connectedclient); //A response is ready!
			memset(&response, 0, sizeof(response)); //Parsed!
			connectedclient->ppp_serverLCPrequesttimer = (DOUBLE)0.0f; //Restart timing!
		}
		goto ppp_finishpacketbufferqueue2_lcp; //Success!
	ppp_finishpacketbufferqueue_lcp: //An error occurred during the response?
		result = 0; //Keep pending until we can properly handle it!
	ppp_finishpacketbufferqueue2_lcp:
		packetServerFreePacketBufferQueue(&LCP_requestFields); //Free the queued response!
		packetServerFreePacketBufferQueue(&response); //Free the queued response!
		packetServerFreePacketBufferQueue(&pppNakFields); //Free the queued response!
		packetServerFreePacketBufferQueue(&pppRejectFields); //Free the queued response!
		return 1; //Give the correct result! Never block the transmitter inputs when this is the case: this is seperated from the normal transmitter handling!
	}
	else
	{
		donthandleServerPPPLCPyet: //Don't handle PPP LCP from server yet?
		if ((!handleTransmit) &&
			//Also, don't handle PAP yet when both sides didn't do LCP open yet.
			LCP_AUTHENTICATING) //Not handling a transmitting of anything atm and LCP for the server-client is down?
		{
			//Use a simple nanosecond timer to determine if we're to send a 
			connectedclient->ppp_serverPAPrequesttimer += modem.networkpolltick; //Time!
			if (connectedclient->ppp_serverPAPrequesttimer < 500000000.0f) //Starting it's timing every interval (first 3 seconds, then half a second)!
			{
				goto donthandleServerPPPPAPyet; //Don't handle the sending of a request from the server yet, because we're still timing!
			}
			if (!connectedclient->ppp_serverPAPstatus) //Initializing?
			{
				connectedclient->ppp_serverPAPidentifier = 0; //Init!
			retryServerPAPnegotiation:
				connectedclient->ppp_serverPAPstatus = 1; //Have initialized!
			}
			else if (connectedclient->ppp_serverPAPstatus > 1) //Resetting?
			{
				++connectedclient->ppp_serverPAPidentifier; //New identifier to start using!
				//Otherwise, it's a retry!
				goto retryServerPAPnegotiation;
			}
			result = 1; //Default: handled!
			//Now, formulate a request!
			connectedclient->ppp_servercurrentPAPidentifier = connectedclient->ppp_serverPAPidentifier; //Load the identifier to try!
			memset(&LCP_requestFields, 0, sizeof(LCP_requestFields)); //Make sure it's ready for usage!

			dataw = safe_strlen(&connectedclient->packetserver_username[0], sizeof(connectedclient->packetserver_username)); //How long is it?
			dataw = MIN(dataw, 0xFF); //Truncate it to become a valid range!
			datab = (byte)dataw; //Set it to use!

			if (!packetServerAddPacketBufferQueue(&LCP_requestFields, (byte)dataw)) //Correct length!
			{
				goto ppp_finishpacketbufferqueue_papserver; //Incorrect packet: discard it!
			}

			for (dataw = 0; dataw < datab;)
			{
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->packetserver_username[dataw++])) //Username!
				{
					goto ppp_finishpacketbufferqueue_papserver; //Incorrect packet: discard it!
				}
			}

			dataw = safe_strlen(&connectedclient->packetserver_password[0], sizeof(connectedclient->packetserver_password)); //How long is it?
			dataw = MIN(dataw, 0xFF); //Truncate it to become a valid range!
			datab = (byte)dataw; //Set it to use!

			if (!packetServerAddPacketBufferQueue(&LCP_requestFields, (byte)dataw)) //Correct length!
			{
				goto ppp_finishpacketbufferqueue_papserver; //Incorrect packet: discard it!
			}

			for (dataw = 0; dataw < datab;)
			{
				if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->packetserver_password[dataw++])) //Username!
				{
					goto ppp_finishpacketbufferqueue_papserver; //Incorrect packet: discard it!
				}
			}

			createPPPstream(&pppstream, LCP_requestFields.buffer, LCP_requestFields.length); //Create a stream object for us to use, which goes until the end of the payload!
			if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, 0xC023, 0x01, connectedclient->ppp_servercurrentPAPidentifier, PPP_streamdataleft(&pppstream))) //Authentication-Request
			{
				goto ppp_finishpacketbufferqueue_papserver; //Finish up!
			}

			for (; PPP_streamdataleft(&pppstream);) //Data left?
			{
				if (!PPP_consumeStream(&pppstream, &datab))
				{
					goto ppp_finishpacketbufferqueue_papserver; //Incorrect packet: discard it!
				}
				if (!packetServerAddPacketBufferQueue(&response, datab)) //Add it!
				{
					goto ppp_finishpacketbufferqueue_papserver; //Finish up!
				}
			}

			//Calculate and add the checksum field!
			if (PPP_addFCS(&response,connectedclient,0xC023))
			{
				goto ppp_finishpacketbufferqueue_papserver;
			}

			//Packet is fully built. Now send it!
			if (connectedclient->ppp_response.size) //Previous Response still valid?
			{
				goto ppp_finishpacketbufferqueue_papserver; //Keep pending!
			}
			if (response.buffer) //Any response to give?
			{
				memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
				ppp_responseforuser(connectedclient); //A response is ready!
				memset(&response, 0, sizeof(response)); //Parsed!
				connectedclient->ppp_serverPAPrequesttimer = (DOUBLE)0.0f; //Restart timing!
			}
			goto ppp_finishpacketbufferqueue2_papserver; //Success!
		ppp_finishpacketbufferqueue_papserver: //An error occurred during the response?
			result = 0; //Keep pending until we can properly handle it!
		ppp_finishpacketbufferqueue2_papserver:
			packetServerFreePacketBufferQueue(&LCP_requestFields); //Free the queued response!
			packetServerFreePacketBufferQueue(&response); //Free the queued response!
			packetServerFreePacketBufferQueue(&pppNakFields); //Free the queued response!
			packetServerFreePacketBufferQueue(&pppRejectFields); //Free the queued response!
			return 1; //Give the correct result! Never block the transmitter inputs when this is the case: this is seperated from the normal transmitter handling!
		}
		else
		{
			donthandleServerPPPPAPyet: //Don't handle PAP from the server yet?
			result = 1; //Didn't handled a protocol.
			if ((!handleTransmit) &&
				LCP_NCP //Don't handle until the upper layers are open!
				&& (!connectedclient->ppp_IPXCPstatus[1])) //Not handling a transmitting of anything atm and LCP for the server-client is down?
			{	
				if (connectedclient->ppp_suppressIPXCP) //Suppressing IPXCP packets requested from the client?
				{
					goto donthandleServerPPPIPXCPyet; //Don't handle the sending of a request from the server yet, because we're suppressed!
				}
				//Use a simple nanosecond timer to determine if we're to send a 
				connectedclient->ppp_serverIPXCPrequesttimer += modem.networkpolltick; //Time!
				if (connectedclient->ppp_serverIPXCPrequesttimer < 500000000.0f) //Starting it's timing every interval (first 3 seconds, then half a second)!
				{
					//Keep the roulette going!
					goto donthandleServerPPPIPXCPyet; //Don't handle the sending of a request from the server yet, because we're still timing!
				}
				if (connectedclient->ppp_serverprotocolroulette!=0) //Roulette mismatch?
				{
					goto donthandleServerPPPIPXCPyet; //Don't handle the sending of a request from the server yet, because we're still timing!
				}
				if (!connectedclient->ppp_serverIPXCPstatus) //Initializing?
				{
					connectedclient->ppp_serverIPXCPidentifier = 0; //Init!
				retryServerIPXCPnegotiation:
					connectedclient->ppp_serverIPXCPstatus = 1; //Have initialized!
					connectedclient->ppp_serverIPXCP_havenetworknumber = connectedclient->ppp_serverIPXCP_havenodenumber = connectedclient->ppp_serverIPXCP_haveroutingprotocol = 1; //Default by trying none!
					memcpy(&connectedclient->ppp_serverIPXCP_pendingnetworknumber, &ipx_servernetworknumber, sizeof(connectedclient->ppp_serverIPXCP_pendingnetworknumber)); //Initialize the network number
					memcpy(&connectedclient->ppp_serverIPXCP_pendingnodenumber, &ipx_servernodeaddr, sizeof(connectedclient->ppp_serverIPXCP_pendingnodenumber)); //Initialize the node number for the server!
					connectedclient->ppp_serverIPXCP_pendingroutingprotocol = 0; //No routing protocol by default!
				}
				else if (connectedclient->ppp_serverIPXCPstatus > 1) //Resetting?
				{
					++connectedclient->ppp_serverIPXCPidentifier; //New identifier to start using!
					//Otherwise, it's a retry!
					if (connectedclient->ppp_serverLCPstatus == 2) //Resetting?
					{
						goto retryServerIPXCPnegotiation;
					}
					else
					{
						connectedclient->ppp_serverIPXCPstatus = 1; //Ready for use for now!
					}
				}
				result = 1; //Default: handled!
				//Now, formulate a request!
				connectedclient->ppp_servercurrentIPXCPidentifier = connectedclient->ppp_serverIPXCPidentifier; //Load the identifier to try!
				memset(&LCP_requestFields, 0, sizeof(LCP_requestFields)); //Make sure it's ready for usage!

				//case 1: //IPX-Network-Number
				if (connectedclient->ppp_serverIPXCP_havenetworknumber) //To request?
				{
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 1)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 6)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverIPXCP_pendingnetworknumber[0])) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverIPXCP_pendingnetworknumber[1])) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverIPXCP_pendingnetworknumber[2])) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverIPXCP_pendingnetworknumber[3])) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
				}
				//case 2: //IPX-Node-Number
				if (connectedclient->ppp_serverIPXCP_havenodenumber) //To request?
				{
					if (!packetServerAddPacketBufferQueue(&pppNakFields, 2)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppNakFields, 8)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->ppp_serverIPXCP_pendingnodenumber[0])) //None!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->ppp_serverIPXCP_pendingnodenumber[1])) //None!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->ppp_serverIPXCP_pendingnodenumber[2])) //None!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->ppp_serverIPXCP_pendingnodenumber[3])) //None!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->ppp_serverIPXCP_pendingnodenumber[4])) //None!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->ppp_serverIPXCP_pendingnodenumber[5])) //None!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
				}
			//case 4: //IPX-Routing-Protocol
				if (connectedclient->ppp_serverIPXCP_haveroutingprotocol) //To request?
				{
					if (!packetServerAddPacketBufferQueue(&pppNakFields, 4)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppNakFields, 4)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueueBE16(&pppNakFields, connectedclient->ppp_serverIPXCP_pendingroutingprotocol))
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
				}

				createPPPstream(&pppstream, LCP_requestFields.buffer, LCP_requestFields.length); //Create a stream object for us to use, which goes until the end of the payload!
				if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, 0x802B, 0x01, connectedclient->ppp_servercurrentIPXCPidentifier, PPP_streamdataleft(&pppstream))) //Configure-Request
				{
					goto ppp_finishpacketbufferqueue_ipxcpserver; //Finish up!
				}

				for (; PPP_streamdataleft(&pppstream);) //Data left?
				{
					if (!PPP_consumeStream(&pppstream, &datab))
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&response, datab)) //Add it!
					{
						goto ppp_finishpacketbufferqueue_ipxcpserver; //Finish up!
					}
				}

				//Calculate and add the checksum field!
				if (PPP_addFCS(&response,connectedclient,0x802B))
				{
					goto ppp_finishpacketbufferqueue_ipxcpserver;
				}

				//Packet is fully built. Now send it!
				if (connectedclient->ppp_response.size) //Previous Response still valid?
				{
					goto ppp_finishpacketbufferqueue_ipxcpserver; //Keep pending!
				}
				if (response.buffer) //Any response to give?
				{
					memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
					ppp_responseforuser(connectedclient); //A response is ready!
					memset(&response, 0, sizeof(response)); //Parsed!
					connectedclient->ppp_serverIPXCPrequesttimer = (DOUBLE)0.0f; //Restart timing!
					++connectedclient->ppp_serverprotocolroulette; //Roulette next?
				}
				goto ppp_finishpacketbufferqueue2_ipxcpserver; //Success!
			ppp_finishpacketbufferqueue_ipxcpserver: //An error occurred during the response?
				result = 0; //Keep pending until we can properly handle it!
			ppp_finishpacketbufferqueue2_ipxcpserver:
				packetServerFreePacketBufferQueue(&LCP_requestFields); //Free the queued response!
				packetServerFreePacketBufferQueue(&response); //Free the queued response!
				packetServerFreePacketBufferQueue(&pppNakFields); //Free the queued response!
				packetServerFreePacketBufferQueue(&pppRejectFields); //Free the queued response!
				//Make sure that the timer at least updates correctly on the other protocols!
				//Below is a copy of the IPCP version!
				if ((!handleTransmit) &&
					(connectedclient->ppp_LCPstatus[1] && (connectedclient->ppp_LCPstatus[0])) && (connectedclient->ppp_PAPstatus[1] && connectedclient->ppp_PAPstatus[0]) //Don't handle until the upper layers are open!
					&& (!connectedclient->ppp_IPCPstatus[1])) //Not handling a transmitting of anything atm and LCP for the server-client is down?
				{
					if (!connectedclient->ppp_suppressIPCP) //Suppressing IPCP packets requested from the client?
					{
						connectedclient->ppp_serverIPCPrequesttimer += modem.networkpolltick; //Time!
					}
				}
				return 1; //Give the correct result! Never block the transmitter inputs when this is the case: this is seperated from the normal transmitter handling!
			}
			donthandleServerPPPIPXCPyet: //Don't handle PPP IPXCP from server yet?
			if ((!handleTransmit) &&
				LCP_NCP //Don't handle until the upper layers are open!
				&& (!connectedclient->ppp_IPCPstatus[1])) //Not handling a transmitting of anything atm and LCP for the server-client is down?
			{
				if (connectedclient->ppp_suppressIPCP) //Suppressing IPCP packets requested from the client?
				{
					goto donthandleServerPPPprotocolyet; //Don't handle the sending of a request from the server yet, because we're suppressed!
				}
				//Use a simple nanosecond timer to determine if we're to send a 
				connectedclient->ppp_serverIPCPrequesttimer += modem.networkpolltick; //Time!
				if (connectedclient->ppp_serverIPCPrequesttimer < 500000000.0f) //Starting it's timing every interval (first 3 seconds, then half a second)!
				{
					//Keep the roulette going!
					goto donthandleServerPPPprotocolyet; //Don't handle the sending of a request from the server yet, because we're still timing!
				}
				if (connectedclient->ppp_serverprotocolroulette!=0) //Roulette mismatch?
				{
					goto donthandleServerPPPprotocolyet; //Don't handle the sending of a request from the server yet, because we're still timing!
				}
				result = 1; //Stop the roulette from going!
				if (!connectedclient->ppp_serverIPCPstatus) //Initializing?
				{
					connectedclient->ppp_serverIPCPidentifier = 0; //Init!
				retryServerIPCPnegotiation:
					connectedclient->ppp_serverIPCPstatus = 1; //Have initialized!
					connectedclient->ppp_serverIPCP_haveipaddress = packetserver_defaultgatewayIP; //Gotten a default gateway IP to send to the client? If so, try to let the client know!
					memcpy(&connectedclient->ppp_serverIPCP_pendingipaddress, &packetserver_defaultgatewayIPaddr, sizeof(connectedclient->ppp_serverIPCP_pendingipaddress)); //Initialize the network number
				}
				else if (connectedclient->ppp_serverIPCPstatus > 1) //Resetting?
				{
					++connectedclient->ppp_serverIPCPidentifier; //New identifier to start using!
					//Otherwise, it's a retry!
					if (connectedclient->ppp_serverLCPstatus == 2) //Resetting?
					{
						goto retryServerIPCPnegotiation;
					}
					else
					{
						connectedclient->ppp_serverIPCPstatus = 1; //Ready for use for now!
					}
				}
				result = 1; //Default: handled!
				//Now, formulate a request!
				connectedclient->ppp_servercurrentIPCPidentifier = connectedclient->ppp_serverIPCPidentifier; //Load the identifier to try!
				memset(&LCP_requestFields, 0, sizeof(LCP_requestFields)); //Make sure it's ready for usage!

				//case 1: //IP address
				if (connectedclient->ppp_serverIPCP_haveipaddress) //To request?
				{
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 3)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue_ipcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, 6)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverIPCP_pendingipaddress[0])) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverIPCP_pendingipaddress[1])) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverIPCP_pendingipaddress[2])) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&LCP_requestFields, connectedclient->ppp_serverIPCP_pendingipaddress[3])) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipcpserver; //Incorrect packet: discard it!
					}
				}

				createPPPstream(&pppstream, LCP_requestFields.buffer, LCP_requestFields.length); //Create a stream object for us to use, which goes until the end of the payload!
				if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, 0x8021, 0x01, connectedclient->ppp_servercurrentIPCPidentifier, PPP_streamdataleft(&pppstream))) //Configure-Request
				{
					goto ppp_finishpacketbufferqueue_ipcpserver; //Finish up!
				}

				for (; PPP_streamdataleft(&pppstream);) //Data left?
				{
					if (!PPP_consumeStream(&pppstream, &datab))
					{
						goto ppp_finishpacketbufferqueue_ipcpserver; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&response, datab)) //Add it!
					{
						goto ppp_finishpacketbufferqueue_ipcpserver; //Finish up!
					}
				}

				//Calculate and add the checksum field!
				if (PPP_addFCS(&response,connectedclient,0x8021))
				{
					goto ppp_finishpacketbufferqueue_ipcpserver;
				}

				//Packet is fully built. Now send it!
				if (connectedclient->ppp_response.size) //Previous Response still valid?
				{
					goto ppp_finishpacketbufferqueue_ipcpserver; //Keep pending!
				}
				if (response.buffer) //Any response to give?
				{
					memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
					ppp_responseforuser(connectedclient); //A response is ready!
					memset(&response, 0, sizeof(response)); //Parsed!
					connectedclient->ppp_serverIPCPrequesttimer = (DOUBLE)0.0f; //Restart timing!
					++connectedclient->ppp_serverprotocolroulette; //Roulette next?
				}
				goto ppp_finishpacketbufferqueue2_ipcpserver; //Success!
			ppp_finishpacketbufferqueue_ipcpserver: //An error occurred during the response?
				result = 0; //Keep pending until we can properly handle it!
			ppp_finishpacketbufferqueue2_ipcpserver:
				packetServerFreePacketBufferQueue(&LCP_requestFields); //Free the queued response!
				packetServerFreePacketBufferQueue(&response); //Free the queued response!
				packetServerFreePacketBufferQueue(&pppNakFields); //Free the queued response!
				packetServerFreePacketBufferQueue(&pppRejectFields); //Free the queued response!
				if ((!handleTransmit) &&
					(connectedclient->ppp_LCPstatus[1] && (connectedclient->ppp_LCPstatus[0])) && (connectedclient->ppp_PAPstatus[1] && connectedclient->ppp_PAPstatus[0]) //Don't handle until the upper layers are open!
					&& (!connectedclient->ppp_IPXCPstatus[1])) //Not handling a transmitting of anything atm and LCP for the server-client is down?
				{
					if (!connectedclient->ppp_suppressIPXCP) //Suppressing IPXCP packets requested from the client?
					{
						//Use a simple nanosecond timer to determine if we're to send a 
						connectedclient->ppp_serverIPXCPrequesttimer += modem.networkpolltick; //Time!
					}
				}
				return 1; //Give the correct result! Never block the transmitter inputs when this is the case: this is seperated from the normal transmitter handling!
			}
			donthandleServerPPPprotocolyet: //Don't handle PPP protocol from server yet?
			if ((!handleTransmit) &&
				(connectedclient->ppp_LCPstatus[1] && (connectedclient->ppp_LCPstatus[0])) && (connectedclient->ppp_PAPstatus[1] && connectedclient->ppp_PAPstatus[0]) //Don't handle until the upper layers are open!
				) //Not handling a transmitting of anything atm and LCP for the server-client is down?
			{
				if (result) //Keeping the roulette spinning?
				{
					++connectedclient->ppp_serverprotocolroulette; //Roulette spin?
					connectedclient->ppp_serverprotocolroulette = connectedclient->ppp_serverprotocolroulette%2; //Roulette spin?
				}
			}
		}
	}
	if (!handleTransmit) return 1; //Don't do anything more when not handling a transmit!
	//Check the checksum first before doing anything with the data!
	checksum = PPP_calcFCS(&connectedclient->packetserver_transmitbuffer[0], connectedclient->packetserver_transmitlength); //Calculate the checksum!
	createPPPstream(&pppstream, &connectedclient->packetserver_transmitbuffer[0], connectedclient->packetserver_transmitlength - 2); //Create a stream object for us to use, which goes until the end of the payload!
	if (checksum != PPP_GOODFCS) //Checksum error?
	{
		createPPPstream(&pppstream, &connectedclient->packetserver_transmitbuffer[0], connectedclient->packetserver_transmitlength); //Create a stream object for us to use, which goes until the end of the entire packet!
		memcpy(&pppstream_protocolstreambackup, &pppstream,sizeof(pppstream_protocolstreambackup)); //Create a stream object for us to use, which goes until the end of the payload!
		goto checkotherprotocols; //Incorrect packet: check for other protocols!
	}
	memcpy(&pppstreambackup, &pppstream, sizeof(pppstream)); //Backup for checking again!
	if (!connectedclient->PPP_headercompressed[PPP_RECVCONF]) //Header present?
	{
		if (!PPP_consumeStream(&pppstream, &datab))
		{
			return 1; //incorrect packet: discard it!
		}
		if (datab != 0xFF) //Invalid address?
		{
			return 1; //incorret packet: discard it!
		}
		if (!PPP_consumeStream(&pppstream, &datab))
		{
			return 1; //incorrect packet: discard it!
		}
		if (datab != 0x03) //Invalid control?
		{
			return 1; //incorret packet: discard it!
		}
	}
	else //Header MIGHT be compressed?
	{
		if (!PPP_consumeStreamBE16(&pppstream, &dataw))
		{
			return 1; //Incorrect packet: discard it!
		}
		if (dataw != 0xFF03) //The first two bytes are not 0xFF and 0x03? It's an compressed header instead!
		{
			memcpy(&pppstream, &pppstreambackup, sizeof(pppstream)); //Return the stream to it's proper start, being compressed away!
		}
	}
	memcpy(&pppstream_protocolstreambackup,&pppstream,sizeof(pppstream)); //For SNAP detection
	//Now, the packet is at the protocol byte/word, so parse it!
	if (!PPP_consumeStream(&pppstream, &datab))
	{
		return 1; //incorrect packet: discard it!
	}
	dataw = (word)datab; //Store First byte, in little-endian!
	if (((datab & 1)==0) || (!connectedclient->PPP_protocolcompressed[PPP_RECVCONF])) //2-byte protocol?
	{
		if (!PPP_consumeStream(&pppstream, &datab)) //Second byte!
		{
			return 1; //Incorrect packet: discard it!
		}
		dataw = (datab | (dataw<<8)); //Second byte of the protocol!
	}
	protocol = dataw; //The used protocol in the header, if it's valid!
	if (!PPP_peekStream(&pppstream, &datab)) //Reached end of stream (no payload)?
	{
		return 1; //Incorrect packet: discard it!
	}
	//Otherwise, it's a 1-byte protocol!
	//It might be a valid packet if we got here! Perform the checksum first to check? But the checksum is already done way above here.
	memcpy(&pppstream_informationfield, &pppstream, sizeof(pppstream)); //The information field that's used, backed up!
	//Now, the PPPstream contains the packet information field, which is the payload. The data has been checked out and is now ready for processing, according to the protocol!
	result = 1; //Default result: finished up!
	memset(&pppNakFields, 0, sizeof(pppNakFields)); //Init to not used!
	memset(&pppRejectFields, 0, sizeof(pppRejectFields)); //Init to not used!
	switch (protocol) //What protocol is used?
	{
	case 0: //Perhaps a SNAP packet?
	checkotherprotocols:
		if (checksum == PPP_GOODFCS)
		{
			goto ppp_invalidprotocol; //Handle as invalid PPP protocol always!
		}
		else //Disable this handling right now!
		{
			result = 1; //Discard this packet!
			goto ppp_finishpacketbufferqueue2; //Simply abort!
		}
		memcpy(&pppstream_protocolstreambackup2, &pppstream_protocolstreambackup, sizeof(pppstream)); //For different protocols detection
		if (!PPP_consumeStream(&pppstream_protocolstreambackup, &datab)) //Reached end of stream (no payload)?
		{
			goto ppp_invalidprotocol; //Invalid protocol!
		}
		if (datab == 0) //Pad byte found?
		{
			if (!PPP_consumeStream(&pppstream_protocolstreambackup, &datab)) //Reached end of stream (no payload)?
			{
				goto trynextprotocol; //Invalid protocol!
			}
			if (datab != 0) //Not OUI byte 0?
			{
				goto trynextprotocol; //Invalid protocol!
			}
			if (!PPP_consumeStream(&pppstream_protocolstreambackup, &datab)) //Reached end of stream (no payload)?
			{
				goto trynextprotocol; //Invalid protocol!
			}
			if (datab != 0) //Not OUI byte 1?
			{
				goto trynextprotocol; //Invalid protocol!
			}
			if (!PPP_consumeStream(&pppstream_protocolstreambackup, &datab)) //Reached end of stream (no payload)?
			{
				goto trynextprotocol; //Invalid protocol!
			}
			if (datab != 0) //Not OUI byte 2?
			{
				goto trynextprotocol; //Invalid protocol!
			}
			if (!PPP_consumeStream(&pppstream_protocolstreambackup, &datab)) //Reached end of stream (no payload)?
			{
				goto trynextprotocol; //Invalid protocol!
			}
			if (datab != 0x81) //Not IPX (protocol upper byte)?
			{
				goto trynextprotocol; //Invalid protocol!
			}
			if (!PPP_consumeStream(&pppstream_protocolstreambackup, &datab)) //Reached end of stream (no payload)?
			{
				goto trynextprotocol; //Invalid protocol!
			}
			if (datab != 0x37) //Not IPX (protocol lower byte)?
			{
				goto trynextprotocol; //Invalid protocol!
			}
			if ((IPXCP_OPEN) && ((connectedclient->ppp_IPXCPstatus[PPP_RECVCONF]==1)||(connectedclient->ppp_IPXCPstatus[PPP_RECVCONF]==2))) //Undetermined or for us?
			{
				memcpy(&pppstream, &pppstream_protocolstreambackup, sizeof(pppstream)); //The IPX packet to send!
				connectedclient->ppp_IPXCPstatus[PPP_RECVCONF] = connectedclient->ppp_IPXCPstatus[PPP_SENDCONF] = 2; //Special IPX SNAP mode to receive now!
				goto SNAP_sendIPXpacket; //Send the framed IPX packet!
			}
			//Try next protocol!
		}
		trynextprotocol: //Try the next usable protocol!
		memcpy(&pppstream_protocolstreambackup, &pppstream_protocolstreambackup2, sizeof(pppstream)); //For different protocols detection
		for (dataw = 0; dataw < 12; ++dataw)
		{
			if (!PPP_consumeStream(&pppstream_protocolstreambackup, &datab)) //Reached end of stream (no payload)?
			{
				goto trynextprotocol2; //Invalid protocol!
			}
		}
		if (!PPP_consumeStream(&pppstream_protocolstreambackup, &datab)) //Reached end of stream (no payload)?
		{
			goto trynextprotocol2; //Invalid protocol!
		}
		dataw = datab; //Load!
		if (!PPP_consumeStream(&pppstream_protocolstreambackup, &datab)) //Reached end of stream (no payload)?
		{
			goto trynextprotocol2; //Invalid protocol!
		}
		dataw = (dataw << 8) | datab; //Load!
		if ((IPXCP_OPEN) && ((connectedclient->ppp_IPXCPstatus[PPP_RECVCONF] == 1) || (connectedclient->ppp_IPXCPstatus[PPP_RECVCONF] == 3))) //Undetermined or for us?
		{
			if (dataw == 0x8137) //Not IPX (protocol lower byte)?
			{
				memcpy(&pppstream, &pppstream_protocolstreambackup, sizeof(pppstream)); //The IPX packet to send!
				connectedclient->ppp_IPXCPstatus[PPP_RECVCONF] = connectedclient->ppp_IPXCPstatus[PPP_SENDCONF] = 3; //Special IPX Ethernet II mode to receive now!
				goto SNAP_sendIPXpacket; //Send the framed IPX packet!
			}
		}
		trynextprotocol2:
		if (checksum==PPP_GOODFCS) //PPP packet?
		{
			goto ppp_invalidprotocol; //Handle as invalid PPP protocol!
		}
		else //Discard this packet type!
		{
			//Since this is an unknown protocol, assume raw IPX packets, if enabled!
			if ((IPXCP_OPEN) && ((connectedclient->ppp_IPXCPstatus[PPP_RECVCONF] == 1) || (connectedclient->ppp_IPXCPstatus[PPP_RECVCONF] == 4))) //Undetermined or for us?
			{
				memcpy(&pppstream, &pppstream_protocolstreambackup2, sizeof(pppstream)); //The IPX packet to send!
				connectedclient->ppp_IPXCPstatus[PPP_RECVCONF] = connectedclient->ppp_IPXCPstatus[PPP_SENDCONF] = 4; //Special IPX raw mode to receive now!
				goto SNAP_sendIPXpacket; //Send the framed IPX packet!
			}
			result = 1; //Discard this packet!
			goto ppp_finishpacketbufferqueue2; //Simply abort!
		}
		break;
	case 0x0001: //Padding protocol?
		//NOP!
		break;
	case 0xC021: //LCP?
		if (!PPP_consumeStream(&pppstream, &common_CodeField)) //Code couldn't be read?
		{
			return 1; //Incorrect packet: discard it!
		}
		if (!PPP_consumeStream(&pppstream, &common_IdentifierField)) //Identifier couldn't be read?
		{
			return 1; //Incorrect packet: discard it!
		}
		if (!PPP_consumeStreamBE16(&pppstream, &common_LengthField)) //Length couldn't be read?
		{
			return 1; //Incorrect packet: discard it!
		}
		if (common_LengthField < 4) //Not enough data?
		{
			return 1; //Incorrect packet: discard it!
		}
		switch (common_CodeField) //What operation code?
		{
		case 1: //Configure-Request
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField,4)-4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			request_pendingMRU = 1500; //Default MTU value to use!
			request_pendingProtocolFieldCompression = 0; //Default: no protocol field compression!
			request_pendingAddressAndControlFieldCompression = 0; //Default: no address-and-control-field compression!
			memset(&request_magic_number, 0, sizeof(request_magic_number)); //Default: none
			request_magic_number_used = 0; //Default: not used!
			request_authenticationspecified = 0; //Default: not used!
			request_asynccontrolcharactermap[0] = request_asynccontrolcharactermap[1] = request_asynccontrolcharactermap[2] = request_asynccontrolcharactermap[3] = 0xFF; //All ones by default!

			//Now, start parsing the options for the connection!
			for (; PPP_peekStream(&pppstream_requestfield, &common_TypeField);) //Gotten a new option to parse?
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &common_TypeField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (!PPP_consumeStream(&pppstream_requestfield, &common_OptionLengthField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (PPP_streamdataleft(&pppstream_requestfield) < (MAX(common_OptionLengthField,2U)-2U)) //Not enough room left for the option data?
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				performskipdataNak = 0; //Default: not skipped already!
				switch (common_TypeField) //What type is specified for the option?
				{
				case 1: //Maximum Receive Unit
					if (common_OptionLengthField != 4) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 4)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueueBE16(&pppNakFields, 1500)) //Correct data!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_lcp; //Skip the data please!
					}
					if (!PPP_consumeStreamBE16(&pppstream_requestfield, &request_pendingMRU)) //Pending MRU field!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
					//Field is OK!
					break;
				case 7: //Protocol Field Compression
					if (common_OptionLengthField != 2) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 2)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_lcp; //Skip the data please!
					}
					request_pendingProtocolFieldCompression = 1; //Set the request!
					break;
				case 8: //Address-And-Control-Field-Compression
					if (common_OptionLengthField != 2) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 2)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_lcp; //Skip the data please!
					}
					request_pendingAddressAndControlFieldCompression = 1; //Set the request!
					break;
				case 5: //Magic Number
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_lcp; //Skip the data please!
					}
					request_magic_number_used = 1; //Set the request!
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[0])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[1])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[2])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[3])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					break;
				case 3: //Authentication Protocol
					//This is a special case: unlike the other parameters which determines how the client want to receive something, this is the client asking us to authenticate using a specified protocol. Allow only valid protocols and NAK it with a supported one if it doesn't match.
					if (common_OptionLengthField != 4) //Unsupported length?
					{
						invalidauthenticationprotocol:
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 4)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueueBE16(&pppNakFields, 0xC023)) //PAP!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_lcp; //Skip the data please!
					}
					if (!PPP_consumeStreamBE16(&pppstream_requestfield, &request_authenticationprotocol)) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (request_authenticationprotocol != 0xC023) //Not a supported protocol?
					{
						goto invalidauthenticationprotocol; //Count as invalid!
					}
					request_authenticationspecified = 1; //Request that authentication be used!
					break;
				case 2: //ASync-Control-Character-Map
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_lcp; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[0])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[1])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[2])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[3])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					break;
				case 4: //Quality protocol
				default: //Unknown option?
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_TypeField)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_OptionLengthField)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
					performskipdata_lcp:
					if (common_OptionLengthField >= 2) //Enough length to skip?
					{
						skipdatacounter = common_OptionLengthField - 2; //How much to skip!
						for (; skipdatacounter;) //Skip it!
						{
							if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Failed to consume properly?
							{
								goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
							}
							if (!performskipdataNak) //Not skipping data altogether?
							{
								if (!packetServerAddPacketBufferQueue(&pppRejectFields, datab)) //Correct data!
								{
									goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
								}
							}
							--skipdatacounter;
						}
					}
					else //Malformed parameter!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
					break;
				}
			}
			//TODO: Finish parsing properly
			if (pppNakFields.buffer || pppRejectFields.buffer) //NAK or Rejected any fields? Then don't process to the connected phase!
			{
				memcpy(&connectedclient->ppp_nakfields, &pppNakFields, sizeof(pppNakFields)); //Give the response to the client!
				connectedclient->ppp_nakfields_identifier = common_IdentifierField; //Identifier!
				memcpy(&connectedclient->ppp_rejectfields, &pppRejectFields, sizeof(pppRejectFields)); //Give the response to the client!
				connectedclient->ppp_rejectfields_identifier = common_IdentifierField; //Identifier!
				memset(&pppNakFields, 0, sizeof(pppNakFields)); //Queued!
				memset(&pppRejectFields, 0, sizeof(pppRejectFields)); //Queued!
				result = 1; //Success!
			}
			else //OK! All parameters are fine!
			{
				//Apply the parameters to the session and send back an request-ACK!
				memset(&response, 0, sizeof(response)); //Init the response!
				//Build the PPP header first!
				//Don't compress the header yet, since it's still negotiating!
				if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
				if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, protocol, 0x02, common_IdentifierField, PPP_streamdataleft(&pppstream_requestfield)))
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
				for (; PPP_streamdataleft(&pppstream_requestfield);) //Data left?
				{
					if (!PPP_consumeStream(&pppstream_requestfield, &datab))
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&response, datab)) //Add it!
					{
						goto ppp_finishpacketbufferqueue; //Finish up!
					}
				}
				//Calculate and add the checksum field!
				if (PPP_addFCS(&response,connectedclient,protocol))
				{
					goto ppp_finishpacketbufferqueue;
				}
				//Packet is fully built. Now send it!
				if (connectedclient->ppp_response.size) //Previous Response still valid?
				{
					goto ppp_finishpacketbufferqueue; //Keep pending!
				}
				if (response.buffer) //Any response to give?
				{
					memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
					ppp_responseforuser(connectedclient); //A response is ready!
					memset(&response, 0, sizeof(response)); //Parsed!
					//Now, apply the request properly!
					connectedclient->ppp_LCPstatus[0] = 1; //Open!
					connectedclient->PPP_MRU[0] = request_pendingMRU; //MRU!
					connectedclient->PPP_headercompressed[0] = request_pendingAddressAndControlFieldCompression; //Header compression!
					connectedclient->PPP_protocolcompressed[0] = request_pendingProtocolFieldCompression; //Protocol compressed!
					connectedclient->asynccontrolcharactermap[0] = SDL_SwapBE32((request_asynccontrolcharactermap[0]|(request_asynccontrolcharactermap[1]<<8)|(request_asynccontrolcharactermap[2]<<16)|(request_asynccontrolcharactermap[3]<<24)));
					memcpy(&connectedclient->magic_number[0], &request_magic_number, sizeof(request_magic_number)); //Magic number
					connectedclient->have_magic_number[0] = request_magic_number_used; //Use magic number?
					if (request_authenticationspecified) //Authentication specified?
					{
						if (!connectedclient->ppp_PAPstatus[0]) //Wasn't already authenticated?
						{
							connectedclient->ppp_IPXCPstatus[0] = 0; //Closed!
							connectedclient->ppp_IPCPstatus[0] = 0; //Closed!
						}
					}
					else
					{
						connectedclient->ppp_PAPstatus[0] = 1; //Authenticated automatically!
					}
					connectedclient->ipxcp_negotiationstatus = 0; //No negotation yet!
				}
				result = 1; //Success!
			}
			goto ppp_finishpacketbufferqueue2; //Finish up!
			break;
		case 5: //Terminate-Request (Request termination of connection)
			//Send a Code-Reject packet to the client!
			memset(&response, 0, sizeof(response)); //Init the response!
			//Build the PPP header first!
			if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, protocol, 0x06, common_IdentifierField, PPP_streamdataleft(&pppstream)))
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			//Now, the rejected packet itself!
			for (; PPP_consumeStream(&pppstream, &datab);) //The data field itself follows!
			{
				if (!packetServerAddPacketBufferQueue(&response, datab))
				{
					goto ppp_finishpacketbufferqueue;
				}
			}
			//Calculate and add the checksum field!
			if (PPP_addFCS(&response,connectedclient,protocol))
			{
				goto ppp_finishpacketbufferqueue;
			}
			//Packet is fully built. Now send it!
			if (connectedclient->ppp_response.size) //Previous Response still valid?
			{
				goto ppp_finishpacketbufferqueue; //Keep pending!
			}
			if (response.buffer) //Any response to give?
			{
				memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
				ppp_responseforuser(connectedclient); //A response is ready!
				memset(&response, 0, sizeof(response)); //Parsed!
				//Now, apply the request properly!
				connectedclient->ppp_LCPstatus[0] = 0; //Closed!
				connectedclient->ppp_PAPstatus[0] = 0; //Closed!
				connectedclient->ppp_IPXCPstatus[0] = 0; //Closed!
				connectedclient->ppp_IPCPstatus[0] = 0; //Closed!
				connectedclient->PPP_MRU[0] = 1500; //Default: 1500
				connectedclient->PPP_headercompressed[0] = 0; //Default: uncompressed
				connectedclient->PPP_protocolcompressed[0] = 0; //Default: uncompressed
				connectedclient->have_magic_number[0] = 0; //Default: no magic number yet
			}
			result = 1; //Discard it!
			goto ppp_finishpacketbufferqueue2; //Finish up!
			break;
		case 9: //Echo-Request (Request Echo-Reply. Required for an open connection to reply).
			//Send a Code-Reject packet to the client!
			if (!connectedclient->ppp_LCPstatus[PPP_RECVCONF])
			{
				result = 1;
				goto ppp_finishpacketbufferqueue2; //Finish up!
			}
			memset(&response, 0, sizeof(response)); //Init the response!
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, protocol, 0x0A, common_IdentifierField, PPP_streamdataleft(&pppstream_requestfield)))
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[0])) //Length couldn't be read?
			{
				goto ppp_finishpacketbufferqueue2; //Finish up!
			}
			if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[1])) //Length couldn't be read?
			{
				goto ppp_finishpacketbufferqueue2; //Finish up!
			}
			if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[2])) //Length couldn't be read?
			{
				goto ppp_finishpacketbufferqueue2; //Finish up!
			}
			if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[3])) //Length couldn't be read?
			{
				goto ppp_finishpacketbufferqueue2; //Finish up!
			}
			if (connectedclient->have_magic_number[0]) //Magic number set?
			{
				if (memcmp(&request_magic_number, connectedclient->magic_number[PPP_RECVCONF], sizeof(request_magic_number)) != 0) //Magic number mismatch?
				{
					result = 1; //Discard!
					goto ppp_finishpacketbufferqueue2; //Finish up!
				}
			}
			else //Magic-number option isn't set? Assume zeroed for the sending party to be correct only!
			{
				if (memcmp(&request_magic_number, &no_magic_number[0], sizeof(request_magic_number)) != 0) //Magic number mismatch?
				{
					result = 1; //Discard!
					goto ppp_finishpacketbufferqueue2; //Finish up!
				}
			}
			if (connectedclient->magic_number[PPP_SENDCONF]) //Have magic number?
			{
				if (!packetServerAddPacketBufferQueue(&response, connectedclient->magic_number[PPP_SENDCONF][0])) //Magic-number option!
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
				if (!packetServerAddPacketBufferQueue(&response, connectedclient->magic_number[PPP_SENDCONF][1])) //Magic-number option!
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
				if (!packetServerAddPacketBufferQueue(&response, connectedclient->magic_number[PPP_SENDCONF][2])) //Magic-number option!
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
				if (!packetServerAddPacketBufferQueue(&response, connectedclient->magic_number[PPP_SENDCONF][3])) //Magic-number option!
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
			}
			else //Until the magic number is negotiated in LCP, it's zero!
			{
				if (!packetServerAddPacketBufferQueue(&response, no_magic_number[0])) //Magic-number option!
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
				if (!packetServerAddPacketBufferQueue(&response, no_magic_number[1])) //Magic-number option!
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
				if (!packetServerAddPacketBufferQueue(&response, no_magic_number[2])) //Magic-number option!
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
				if (!packetServerAddPacketBufferQueue(&response, no_magic_number[3])) //Magic-number option!
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
			}
			//Now, the rejected packet itself!
			for (; PPP_consumeStream(&pppstream_requestfield, &datab);) //The data field itself follows!
			{
				if (!packetServerAddPacketBufferQueue(&response, datab))
				{
					goto ppp_finishpacketbufferqueue;
				}
			}
			//Calculate and add the checksum field!
			if (PPP_addFCS(&response,connectedclient,protocol))
			{
				goto ppp_finishpacketbufferqueue;
			}
			//Packet is fully built. Now send it!
			if (connectedclient->ppp_response.size) //Previous Response still valid?
			{
				goto ppp_finishpacketbufferqueue; //Keep pending!
			}
			if (response.buffer) //Any response to give?
			{
				memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
				ppp_responseforuser(connectedclient); //A response is ready!
				memset(&response, 0, sizeof(response)); //Parsed!
			}
			goto ppp_finishpacketbufferqueue2; //Finish up!
			break;
		case 2: //Configure-Ack (All options OK)
			if (common_IdentifierField != connectedclient->ppp_servercurrentLCPidentifier) //Identifier mismatch?
			{
				result = 1; //Discard this packet!
				goto ppp_finishpacketbufferqueue2; //Finish up!
			}
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			request_pendingMRU = 1500; //Default MTU value to use!
			request_pendingProtocolFieldCompression = 0; //Default: no protocol field compression!
			request_pendingAddressAndControlFieldCompression = 0; //Default: no address-and-control-field compression!
			memset(&request_magic_number, 0, sizeof(request_magic_number)); //Default: none
			request_magic_number_used = 0; //Default: not used!
			request_authenticationspecified = 0; //Default: not used!
			request_asynccontrolcharactermap[0] = request_asynccontrolcharactermap[1] = request_asynccontrolcharactermap[2] = request_asynccontrolcharactermap[3] = 0xFF; //All ones by default!

			//Now, start parsing the options for the connection!
			for (; PPP_peekStream(&pppstream_requestfield, &common_TypeField);) //Gotten a new option to parse?
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &common_TypeField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (!PPP_consumeStream(&pppstream_requestfield, &common_OptionLengthField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (PPP_streamdataleft(&pppstream_requestfield) < (MAX(common_OptionLengthField, 2U) - 2U)) //Not enough room left for the option data?
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				switch (common_TypeField) //What type is specified for the option?
				{
				case 1: //Maximum Receive Unit
					if (common_OptionLengthField != 4) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 4)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueueBE16(&pppNakFields, 1500)) //Correct data!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpack; //Skip the data please!
					}
					if (!PPP_consumeStreamBE16(&pppstream_requestfield, &request_pendingMRU)) //Pending MRU field!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
					//Field is OK!
					break;
				case 7: //Protocol Field Compression
					if (common_OptionLengthField != 2) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 2)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpack; //Skip the data please!
					}
					request_pendingProtocolFieldCompression = 1; //Set the request!
					break;
				case 8: //Address-And-Control-Field-Compression
					if (common_OptionLengthField != 2) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 2)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpack; //Skip the data please!
					}
					request_pendingAddressAndControlFieldCompression = 1; //Set the request!
					break;
				case 5: //Magic Number
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpack; //Skip the data please!
					}
					request_magic_number_used = 1; //Set the request!
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[0])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[1])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[2])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[3])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					break;
				case 3: //Authentication Protocol
					if (common_OptionLengthField != 4) //Unsupported length?
					{
					invalidauthenticationprotocol_lcpack:
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 4)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueueBE16(&pppNakFields, 0xC023)) //PAP!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpack; //Skip the data please!
					}
					if (!PPP_consumeStreamBE16(&pppstream_requestfield, &request_authenticationprotocol)) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (request_authenticationprotocol != 0xC023) //Not a supported protocol?
					{
						goto invalidauthenticationprotocol_lcpack; //Count as invalid!
					}
					request_authenticationspecified = 1; //Request that authentication be used!
					break;
				case 2: //ASync-Control-Character-Map
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpack; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[0])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[1])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[2])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[3])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					break;
				case 4: //Quality protocol
				default: //Unknown option?
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_TypeField)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_OptionLengthField)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
				performskipdata_lcpack:
					if (common_OptionLengthField >= 2) //Enough length to skip?
					{
						skipdatacounter = common_OptionLengthField - 2; //How much to skip!
						for (; skipdatacounter;) //Skip it!
						{
							if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Failed to consume properly?
							{
								goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppRejectFields, datab)) //Correct data!
							{
								goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
							}
							--skipdatacounter;
						}
					}
					else //Malformed parameter!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
					break;
				}
			}

			//TODO: Finish parsing properly
			if (pppNakFields.buffer || pppRejectFields.buffer) //NAK or Rejected any fields? Then don't process to the connected phase!
			{
				connectedclient->ppp_serverLCPstatus = 2; //Reset the status check to try again afterwards if it's reset again!
			}
			else //OK! All parameters are fine!
			{
				//Apply the parameters to the session and start the connection!
				//Now, apply the request properly!
				connectedclient->ppp_LCPstatus[1] = 1; //Open!
				connectedclient->PPP_MRU[1] = request_pendingMRU; //MRU!
				connectedclient->PPP_headercompressed[1] = request_pendingAddressAndControlFieldCompression; //Header compression!
				connectedclient->PPP_protocolcompressed[1] = request_pendingProtocolFieldCompression; //Protocol compressed!
				connectedclient->asynccontrolcharactermap[1] = SDL_SwapBE32((request_asynccontrolcharactermap[0] | (request_asynccontrolcharactermap[1] << 8) | (request_asynccontrolcharactermap[2] << 16) | (request_asynccontrolcharactermap[3] << 24)));
				memcpy(&connectedclient->magic_number[1], &request_magic_number, sizeof(request_magic_number)); //Magic number
				connectedclient->have_magic_number[1] = request_magic_number_used; //Use magic number?
				if (request_authenticationspecified) //Authentication specified?
				{
					if (!connectedclient->ppp_PAPstatus[1]) //Not already verified?
					{
						connectedclient->ppp_IPXCPstatus[1] = 0; //Closed!
						connectedclient->ppp_IPCPstatus[1] = 0; //Closed!
						connectedclient->ppp_PAPstatus[1] = 0; //Not Authenticated yet!
					}
				}
				else
				{
					connectedclient->ppp_PAPstatus[1] = 1; //Authenticated automatically!
				}
				//connectedclient->ipxcp_negotiationstatus = 0; //No negotation yet!
				connectedclient->ppp_serverLCPstatus = 2; //Reset the status check to try again afterwards if it's reset again!

				//Extra: prepare the IPXCP (if used immediately) and PAP state for usage!
				connectedclient->ppp_serverPAPrequesttimer = (DOUBLE)0.0f; //Restart timing!
				connectedclient->ppp_serverIPXCPrequesttimer = (DOUBLE)0.0f; //Restart timing!
				connectedclient->ppp_serverIPCPrequesttimer = (DOUBLE)0.0f; //Restart timing!
				connectedclient->ppp_serverLCPrequesttimer = (DOUBLE)0.0f; //Restart timing!
			}
			result = 1; //Discard it!
			goto ppp_finishpacketbufferqueue2; //Finish up!
			break;
		case 3: //Configure-Nak (Some options unacceptable)
		case 4: //Configure-Reject (Some options not recognisable or acceptable for negotiation)
			if (common_IdentifierField != connectedclient->ppp_servercurrentLCPidentifier) //Identifier mismatch?
			{
				result = 1; //Discard this packet!
				goto ppp_finishpacketbufferqueue2; //Finish up!
			}
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			request_NakRejectpendingMRU = 0; //Not used by default!
			request_pendingMRU = 1500; //Default MTU value to use!
			request_pendingProtocolFieldCompression = 0; //Default: no protocol field compression!
			request_pendingAddressAndControlFieldCompression = 0; //Default: no address-and-control-field compression!
			memset(&request_magic_number, 0, sizeof(request_magic_number)); //Default: none
			request_magic_number_used = 0; //Default: not used!
			request_authenticationspecified = 0; //Default: not used!
			request_asynccontrolcharactermap[0] = request_asynccontrolcharactermap[1] = request_asynccontrolcharactermap[2] = request_asynccontrolcharactermap[3] = 0xFF; //All ones by default!
			request_asynccontrolcharactermapspecified = 0; //Default: not used!

			//Now, start parsing the options for the connection!
			for (; PPP_peekStream(&pppstream_requestfield, &common_TypeField);) //Gotten a new option to parse?
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &common_TypeField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (!PPP_consumeStream(&pppstream_requestfield, &common_OptionLengthField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (PPP_streamdataleft(&pppstream_requestfield) < (MAX(common_OptionLengthField, 2U) - 2U)) //Not enough room left for the option data?
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				switch (common_TypeField) //What type is specified for the option?
				{
				case 1: //Maximum Receive Unit
					if (common_OptionLengthField != 4) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 4)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueueBE16(&pppNakFields, 1500)) //Correct data!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpnakreject; //Skip the data please!
					}
					if (!PPP_consumeStreamBE16(&pppstream_requestfield, &request_pendingMRU)) //Pending MRU field!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
					request_NakRejectpendingMRU = 1; //This was Nak/Rejected!
					//Field is OK!
					break;
				case 7: //Protocol Field Compression
					if (common_OptionLengthField != 2) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 2)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpnakreject; //Skip the data please!
					}
					request_pendingProtocolFieldCompression = 1; //Set the request!
					break;
				case 8: //Address-And-Control-Field-Compression
					if (common_OptionLengthField != 2) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 2)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpnakreject; //Skip the data please!
					}
					request_pendingAddressAndControlFieldCompression = 1; //Set the request!
					break;
				case 5: //Magic Number
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpnakreject; //Skip the data please!
					}
					request_magic_number_used = 1; //Set the request!
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[0])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[1])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[2])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_magic_number[3])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					break;
				case 3: //Authentication Protocol
					if (common_OptionLengthField != 4) //Unsupported length?
					{
					invalidauthenticationprotocol_lcpnakreject:
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 4)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueueBE16(&pppNakFields, 0xC023)) //PAP!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpnakreject; //Skip the data please!
					}
					//request_magic_number_used = 1; //Set the request!
					if (!PPP_consumeStreamBE16(&pppstream_requestfield, &request_authenticationprotocol)) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (request_authenticationprotocol != 0xC023) //Not a supported protocol?
					{
						goto invalidauthenticationprotocol_lcpnakreject; //Count as invalid!
					}
					request_authenticationspecified = 1; //Request that authentication be used!
					break;
				case 2: //ASync-Control-Character-Map
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0xFF)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
						}
						goto performskipdata_lcpnakreject; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[0])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[1])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[2])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &request_asynccontrolcharactermap[3])) //Length couldn't be read?
					{
						result = 1; //Discard!
						goto ppp_finishpacketbufferqueue2; //Finish up!
					}
					request_asynccontrolcharactermapspecified = 1; //Used!
					break;
				case 4: //Quality protocol
				default: //Unknown option?
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_TypeField)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_OptionLengthField)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
				performskipdata_lcpnakreject:
					if (common_OptionLengthField >= 2) //Enough length to skip?
					{
						skipdatacounter = common_OptionLengthField - 2; //How much to skip!
						for (; skipdatacounter;) //Skip it!
						{
							if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Failed to consume properly?
							{
								goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppRejectFields, datab)) //Correct data!
							{
								goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
							}
							--skipdatacounter;
						}
					}
					else //Malformed parameter!
					{
						goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
					}
					break;
				}
			}
			if ((pppNakFields.length == 0) && (pppRejectFields.length == 0)) //OK to process?
			{
				if (request_NakRejectpendingMRU && (common_CodeField == 4)) //Reject-MRU?
				{
					connectedclient->ppp_serverLCP_haveMRU = 0; //Don't request anymore!
				}
				else if (request_NakRejectpendingMRU) //MRU change requested?
				{
					connectedclient->ppp_serverLCP_pendingMRU = request_pendingMRU; //The request MRU to use!
					connectedclient->ppp_serverLCP_haveMRU = 1; //Request now!
				}
				if (request_pendingProtocolFieldCompression && (common_CodeField == 4)) //Protocol field compression Nak/Reject?
				{
					connectedclient->ppp_serverLCP_haveProtocolFieldCompression = 0; //Not anymore!
				}
				else if (request_pendingProtocolFieldCompression)
				{
					connectedclient->ppp_serverLCP_haveProtocolFieldCompression = 1; //Request now!
				}
				if (request_pendingAddressAndControlFieldCompression && (common_CodeField==4)) //Address and Control Field Compression Nak/Reject?
				{
					connectedclient->ppp_serverLCP_haveAddressAndControlFieldCompression = 0; //Not anymore!
				}
				else
				{
					connectedclient->ppp_serverLCP_haveAddressAndControlFieldCompression = 1; //Request now!
				}
				if (request_magic_number_used && (common_CodeField == 4)) //Reject-Magic number?
				{
					connectedclient->ppp_serverLCP_haveMagicNumber = 0; //Not anymore!
				}
				else if (request_magic_number_used) //Magic number requested?
				{
					memcpy(&connectedclient->ppp_serverLCP_pendingMagicNumber, &request_magic_number, sizeof(request_magic_number)); //The magic number to use!
					connectedclient->ppp_serverLCP_haveMagicNumber = 1; //Request now!
				}
				if (request_asynccontrolcharactermapspecified && (common_CodeField == 4)) //Reject-Async control character map?
				{
					connectedclient->ppp_serverLCP_haveAsyncControlCharacterMap = 0; //Not anymore!
				}
				else if (request_asynccontrolcharactermapspecified) //Async control character map requested?
				{
					memcpy(&connectedclient->ppp_serverLCP_pendingASyncControlCharacterMap, &request_asynccontrolcharactermap, sizeof(request_asynccontrolcharactermap)); //ASync-Control-Character-Map to use?
					connectedclient->ppp_serverLCP_haveAsyncControlCharacterMap = 1; //Request now!
				}
				if (request_authenticationspecified && (common_CodeField == 4)) //Reject-Authentication-Protocol?
				{
					if (!connectedclient->ppp_autodetected) //Autodetect requires PAP, so don't remove it from our request to authenticate?
					{
						connectedclient->ppp_serverLCP_haveAuthenticationProtocol = 0; //Not anymore!
					}
				}
				else if (request_authenticationspecified) //Authentication-Protocol requested?
				{
					if (request_authenticationprotocol == 0xC023) //Requested correct?
					{
						connectedclient->ppp_serverLCP_haveAuthenticationProtocol = 1; //Use PAP!
					}
					else if (request_authenticationprotocol == 0xC223) //CHAP is tried?
					{
						connectedclient->ppp_serverLCP_haveAuthenticationProtocol = 1; //Use PAP instead!
					}
					else //Unknown protocol?
					{
						connectedclient->ppp_serverLCP_haveAuthenticationProtocol = 1; //Use PAP!
					}
				}
				connectedclient->ppp_serverLCPstatus = 3; //Reset the status check to try again afterwards if it's reset again!
			}
			result = 1; //Success!
			goto ppp_finishpacketbufferqueue2; //Finish up!
			break;
		case 8: //Protocol-Reject (Protocol field is rejected for an active connection)
			if (!connectedclient->ppp_LCPstatus[PPP_RECVCONF]) //LCP is closed?
			{
				goto ppp_finishpacketbufferqueue2_pap; //Invalid protocol!
			}
			//Identifier can be ignored for this!
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue_pap; //Finish up!
			}
			if (pppstream.size >= 2) //Long enough to parse?
			{
				if (!PPP_consumeStreamBE16(&pppstream_requestfield, &dataw)) //The protocoll
				{
					goto ppp_finishpacketbufferqueue_pap; //Incorrect packet: discard it!
				}
				switch (dataw) //What protocol was rejected?
				{
				case 0xC021: //LCP
					//Huh? This is a mandatory protocol and should be ignored!
					break;
				case 0xC023: //PAP
					//The same as LCP: This is a mandatory protocol?
					break;
				case 0x802B: //IPXCP
					//This is apparently unsupported by the server. Suppress IPXCP packets until reaching the network phase again.
					connectedclient->ppp_suppressIPXCP = 3; //Suppress IPXCP from sending from the server unless requested again!
					break;
				case 0x8021: //IPCP
					//This is apparently unsupported by the server. Suppress IPXCP packets until reaching the network phase again.
					connectedclient->ppp_suppressIPCP = 3; //Suppress IPXCP from sending from the server unless requested again!
					break;
				case 0x2B: //IPX
					//IPX is closed? This shouldn't happen?
					connectedclient->ppp_suppressIPX = 3; //Suppress IPXCP from sending from the server unless requested again!
					break;
				case 0x21: //IP
					//IP is closed? This shouldn't happen?
					connectedclient->ppp_suppressIP = 3; //Suppress IPXCP from sending from the server unless requested again!
					break;
				default: //Unknown protocol we're not using?
					//Ignore it entirely!
					break;
				}
			}
			//Invalid sizes aren't handled!
			goto ppp_finishpacketbufferqueue2;
			break;
		case 11: //Discard-Request
			if (connectedclient->ppp_LCPstatus[PPP_RECVCONF]) //LCP opened?
			{
				//Magic-NUmber is ignored.
				//This packet is fully discarded!
				result = 1; //Simply discard it, not doing anything with this packet!
				goto ppp_finishpacketbufferqueue2; //Simply 
				break;
			}
			//Is LCP is closed, an Code-Reject is issued instead?
		case 6: //Terminate-Ack (Acnowledge termination of connection)
			//Why would we need to handle this if the client can't have it's connection terminated by us!
			connectedclient->ppp_LCPstatus[1] = 0; //Close our connection?
		case 7: //Code-Reject (Code field is rejected because it's unknown)
			result = 1; //Discard!
			goto ppp_finishpacketbufferqueue2; //Finish up!
		case 10: //Echo-Reply
			//Echo replies aren't done by us, so ignore them.
			result = 1; //Success!
			goto ppp_finishpacketbufferqueue2; //Finish up!
			break; //Ignore it!
		default: //Unknown Code field?
			//Send a Code-Reject packet to the client!
			memset(&response, 0, sizeof(response)); //Init the response!
			//Build the PPP header first!
			if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, protocol, 0x07, common_IdentifierField, PPP_streamdataleft(&pppstream_informationfield)))
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			//Now, the rejected packet itself!
			for (; PPP_consumeStream(&pppstream_informationfield,&datab);) //The information field itself follows!
			{
				if (!packetServerAddPacketBufferQueue(&response, datab))
				{
					goto ppp_finishpacketbufferqueue;
				}
			}
			//Calculate and add the checksum field!
			if (PPP_addFCS(&response,connectedclient,protocol))
			{
				goto ppp_finishpacketbufferqueue;
			}
			break;
		}
		//Packet is fully built. Now send it!
		if (connectedclient->ppp_response.size) //Previous Response still valid?
		{
			goto ppp_finishpacketbufferqueue; //Keep pending!
		}
		if (response.buffer) //Any response to give?
		{
			memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
			ppp_responseforuser(connectedclient); //A response is ready!
			memset(&response, 0, sizeof(response)); //Parsed!
		}
		goto ppp_finishpacketbufferqueue2; //Success!
		ppp_finishpacketbufferqueue: //An error occurred during the response?
		result = 0; //Keep pending until we can properly handle it!
		ppp_finishpacketbufferqueue2:
		packetServerFreePacketBufferQueue(&response); //Free the queued response!
		packetServerFreePacketBufferQueue(&pppNakFields); //Free the queued response!
		packetServerFreePacketBufferQueue(&pppRejectFields); //Free the queued response!
		break;
	case 0xC023: //PAP?
		if (!LCP_OPEN) //LCP is closed?
		{
			goto ppp_invalidprotocol; //Invalid protocol!
		}
		//This is a special reversed case: the client isn't asking to want us to authenticate, the client is asking to authenticate with us (or responses are of us authenticating with them).
		if (!PPP_consumeStream(&pppstream, &common_CodeField)) //Code couldn't be read?
		{
			result = 1; //Incorrect packet: discard it!
			goto ppp_finishpacketbufferqueue2; //Incorrect packet: discard it!
		}
		if (!PPP_consumeStream(&pppstream, &common_IdentifierField)) //Identifier couldn't be read?
		{
			result = 1; //Incorrect packet: discard it!
			goto ppp_finishpacketbufferqueue2; //Incorrect packet: discard it!
		}
		if (!PPP_consumeStreamBE16(&pppstream, &common_LengthField)) //Length couldn't be read?
		{
			result = 1; //Incorrect packet: discard it!
			goto ppp_finishpacketbufferqueue2; //Incorrect packet: discard it!
		}
		if (common_LengthField < ((common_CodeField==1)?6:5)) //Not enough data?
		{
			result = 1; //Incorrect packet: discard it!
			goto ppp_finishpacketbufferqueue2; //Incorrect packet: discard it!
		}
		switch (common_CodeField) //What operation code?
		{
		case 1: //Authentication-Request
			//This depends on the verification type we're using. If the autodetect method of logging in is used, that's the specified user. Otherrwise, we're going to have to look up users in our database and find a valid user that matches (if any). If any does, authenticate them with said account and make it active.

			c = 0; //When using multiple users, start with the very first user!
			pap_authenticated = 0; //Default: not authenticated!
			retrynextuser:
				if (!(BIOS_Settings.ethernetserver_settings.users[c].username[c] && BIOS_Settings.ethernetserver_settings.users[c].password[c])) //Gotten no credentials?
				{
					if (c < (NUMITEMS(BIOS_Settings.ethernetserver_settings.users) - 1)) //Not all done yet?
					{
						++c; //Next user!
						goto retrynextuser; //Try the next available user!
					}
					else //All users done?
					{
						goto PAP_loginfailed; 
					}
				}
				if (BIOS_Settings.ethernetserver_settings.users[c].username[0] && BIOS_Settings.ethernetserver_settings.users[c].password[0]) //Gotten credentials?
				{
					safestrcpy(connectedclient->packetserver_username, sizeof(connectedclient->packetserver_username), BIOS_Settings.ethernetserver_settings.users[c].username); //The username to try!
					safestrcpy(connectedclient->packetserver_password, sizeof(connectedclient->packetserver_password), BIOS_Settings.ethernetserver_settings.users[c].password); //The username to try!
				}
				else //Invalid user?
				{
					if (c < (NUMITEMS(BIOS_Settings.ethernetserver_settings.users) - 1)) //Not all done yet?
					{
						++c; //Next user!
						goto retrynextuser; //Try the next available user!
					}
					else //All users done?
					{
						goto PAP_loginfailed;
					}
				}
			//Now, try the current or originally logged in user.
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue_pap; //Finish up!
			}

			if (!PPP_consumeStream(&pppstream_requestfield, &username_length))
			{
				goto ppp_finishpacketbufferqueue_pap; //Incorrect packet: discard it!
			}

			pap_authenticated = 1; //Default: authenticated properly!
			//First, the username!
			if (username_length != safe_strlen(connectedclient->packetserver_username, sizeof(connectedclient->packetserver_username))) //Length mismatch?
			{
				pap_authenticated = 0; //Not authenticated!
			}
			for (pap_fieldcounter = 0; pap_fieldcounter < username_length; ++pap_fieldcounter) //Now the username follows (for the specified length)
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Data to compare!
				{
					goto ppp_finishpacketbufferqueue_pap; //Incorrect packet: discard it!
				}
				if (pap_authenticated) //Still valid to compare?
				{
					if (connectedclient->packetserver_username[pap_fieldcounter] != datab) //Mismatch?
					{
						pap_authenticated = 0; //Going to NAK it!
					}
				}
			}
			//Now the password follows (for the specified length)
			if (!PPP_consumeStream(&pppstream_requestfield, &password_length))
			{
				goto ppp_finishpacketbufferqueue_pap; //Incorrect packet: discard it!
			}
			if (password_length != safe_strlen(connectedclient->packetserver_password, sizeof(connectedclient->packetserver_password))) //Length mismatch?
			{
				pap_authenticated = 0; //Not authenticated!
			}
			for (pap_fieldcounter = 0; pap_fieldcounter < password_length; ++pap_fieldcounter) //Now the username follows (for the specified length)
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Data to compare!
				{
					goto ppp_finishpacketbufferqueue_pap; //Incorrect packet: discard it!
				}
				if (pap_authenticated) //Still valid to compare?
				{
					if (connectedclient->packetserver_password[pap_fieldcounter] != datab) //Mismatch?
					{
						pap_authenticated = 0; //Going to NAK it!
					}
				}
			}

			if ((!pap_authenticated)) //Login failed for this user? Try the next one if any is available.
			{
				if (c < (NUMITEMS(BIOS_Settings.ethernetserver_settings.users) - 1)) //Not all done yet?
				{
					++c; //Next user!
					goto retrynextuser; //Try the next available user!
				}
				else //All users done?
				{
					goto PAP_loginfailed;
				}
			}
		PAP_loginfailed: //The login has failed?
			if (!pap_authenticated) //Went through all records available and couldn't login?
			{
				memset(&connectedclient->packetserver_username, 0, sizeof(connectedclient->packetserver_username)); //Clear it again!
				memset(&connectedclient->packetserver_password, 0, sizeof(connectedclient->packetserver_password)); //Clear it again!
			}
			//Otherwise, login succeeded and the username&password we're logged in as is stored in the client information now.

			//Apply the parameters to the session and send back an request-ACK/NAK!
			memset(&response, 0, sizeof(response)); //Init the response!
			//Build the PPP header first!
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue_pap; //Finish up!
			}
			if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, protocol, pap_authenticated ? 0x02 : 0x03, common_IdentifierField, 1)) //Authentication-Ack/Nak. No message(only it's length)
			{
				goto ppp_finishpacketbufferqueue_pap; //Finish up!
			}
			//No message for now!
			if (!packetServerAddPacketBufferQueue(&response, 0)) //Message length!
			{
				goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
			}
			//Calculate and add the checksum field!
			if (PPP_addFCS(&response,connectedclient,protocol))
			{
				goto ppp_finishpacketbufferqueue_pap;
			}
			//Packet is fully built. Now send it!
			if (connectedclient->ppp_response.size) //Previous Response still valid?
			{
				goto ppp_finishpacketbufferqueue_pap; //Keep pending!
			}
			if (response.buffer) //Any response to give?
			{
				memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
				ppp_responseforuser(connectedclient); //A response is ready!
				memset(&response, 0, sizeof(response)); //Parsed!
				//Now, apply the request properly!
				if (pap_authenticated) //Authenticated?
				{
					connectedclient->ppp_PAPstatus[1] = 1; //Authenticated!
					//Determine the IP address!
					memcpy(&connectedclient->packetserver_staticIP, &packetserver_defaultstaticIP, sizeof(connectedclient->packetserver_staticIP)); //Use the default IP!
					safestrcpy(connectedclient->packetserver_staticIPstr, sizeof(connectedclient->packetserver_staticIPstr), packetserver_defaultstaticIPstr); //Formulate the address!
					connectedclient->packetserver_useStaticIP = 0; //Default: not detected!
					if (safestrlen(&BIOS_Settings.ethernetserver_settings.users[c].IPaddress[0], 256) >= 12) //Valid length to convert IP addresses?
					{
						p = (char *)&BIOS_Settings.ethernetserver_settings.users[c].IPaddress[0]; //For scanning the IP!

						if (readIPnumber(&p, &IPnumbers[0]))
						{
							if (readIPnumber(&p, &IPnumbers[1]))
							{
								if (readIPnumber(&p, &IPnumbers[2]))
								{
									if (readIPnumber(&p, &IPnumbers[3]))
									{
										if (*p == '\0') //EOS?
										{
											//Automatic port?
											snprintf(connectedclient->packetserver_staticIPstr, sizeof(connectedclient->packetserver_staticIPstr), "%u.%u.%u.%u", IPnumbers[0], IPnumbers[1], IPnumbers[2], IPnumbers[3]); //Formulate the address!
											memcpy(&connectedclient->packetserver_staticIP, &IPnumbers, 4); //Set read IP!
											connectedclient->packetserver_useStaticIP = 1; //Static IP set!
										}
									}
								}
							}
						}
					}
					else if (safestrlen(&BIOS_Settings.ethernetserver_settings.users[c].IPaddress[0], 256) == 4) //Might be DHCP?
					{
						if ((strcmp(BIOS_Settings.ethernetserver_settings.users[c].IPaddress, "DHCP") == 0) || (strcmp(BIOS_Settings.ethernetserver_settings.users[0].IPaddress, "DHCP") == 0)) //DHCP used for this user or all users?
						{
							//connectedclient->packetserver_useStaticIP = 2; //DHCP requested instead of static IP! Not used yet!
						}
					}
					if (!connectedclient->packetserver_useStaticIP) //Not specified? Use default!
					{
						safestrcpy(connectedclient->packetserver_staticIPstr, sizeof(connectedclient->packetserver_staticIPstr), packetserver_defaultstaticIPstr); //Default!
						memcpy(&connectedclient->packetserver_staticIP, &packetserver_defaultstaticIP, 4); //Set read IP!
						connectedclient->packetserver_useStaticIP = packetserver_usedefaultStaticIP; //Static IP set!
					}
				}
				else
				{
					if (connectedclient->ppp_PAPstatus[1]) //Was authenticated?
					{
						connectedclient->ppp_PAPstatus[1] = 0; //Not authenticated!
						connectedclient->ppp_IPXCPstatus[1] = 0; //Logoff!
						connectedclient->ppp_IPCPstatus[1] = 0; //Logoff!
					}
				}
			}
			goto ppp_finishpacketbufferqueue2_pap; //Finish up!
			break;
		case 2: //Authentication-Ack
			if (!connectedclient->ppp_LCPstatus[PPP_SENDCONF]) //LCP is closed?
			{
				goto ppp_finishpacketbufferqueue2_pap; //Invalid protocol!
			}
			if (connectedclient->ppp_servercurrentPAPidentifier != common_IdentifierField) //Identifier mismatch?
			{
				goto ppp_finishpacketbufferqueue2_pap; //Discard it!
			}
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue_pap; //Finish up!
			}
			if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Actually message length, that we ignore anyways
			{
				goto ppp_finishpacketbufferqueue_pap; //Incorrect packet: discard it!
			}
			dataw = (word)datab; //To use when counting!
			for (; dataw;) //Consume it!
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Actually message length, that we ignore anyways
				{
					goto ppp_finishpacketbufferqueue_pap; //Incorrect packet: discard it!
				}
				--dataw; //One item consumed!
			}
			connectedclient->ppp_PAPstatus[0] = 1; //We're authenticated!
			connectedclient->ppp_serverPAPstatus = 2; //Reset the status check to try again afterwards if it's reset again!
			connectedclient->ppp_serverIPXCPrequesttimer = (DOUBLE)0.0f; //Restart timing!
			connectedclient->ppp_serverIPCPrequesttimer = (DOUBLE)0.0f; //Restart timing!
			goto ppp_finishpacketbufferqueue2_pap;
			break;
		case 3: //Authentication-Nak
			if (!connectedclient->ppp_LCPstatus[PPP_SENDCONF]) //LCP is closed?
			{
				goto ppp_finishpacketbufferqueue2_pap; //Invalid protocol!
			}
			if (connectedclient->ppp_servercurrentPAPidentifier != common_IdentifierField) //Identifier mismatch?
			{
				goto ppp_finishpacketbufferqueue2_pap; //Discard it!
			}
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue_pap; //Finish up!
			}
			if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Actually message length, that we ignore anyways
			{
				goto ppp_finishpacketbufferqueue_pap; //Incorrect packet: discard it!
			}
			dataw = (word)datab; //To use when counting!
			for (; dataw;) //Consume it!
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Actually message length, that we ignore anyways
				{
					goto ppp_finishpacketbufferqueue_pap; //Incorrect packet: discard it!
				}
				--dataw; //One item consumed!
			}
			//Don't authenticate, keep retrying?
			connectedclient->ppp_serverPAPstatus = 2; //Reset the status check to try again afterwards if it's reset again!
			goto ppp_finishpacketbufferqueue2_pap;
			break;
		default: //Unknown Code field?
			goto ppp_finishpacketbufferqueue2_pap; //Finish up only (NOP)!
			break;
		}
		if (response.buffer) //Any response to give?
		{
			memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
			ppp_responseforuser(connectedclient); //A response is ready!
			memset(&response, 0, sizeof(response)); //Parsed!
		}
		result = 1; //Handled!
		goto ppp_finishpacketbufferqueue2_pap; //Success!
	ppp_finishpacketbufferqueue_pap: //An error occurred during the response?
		result = 0; //Keep pending until we can properly handle it!
	ppp_finishpacketbufferqueue2_pap:
		packetServerFreePacketBufferQueue(&response); //Free the queued response!
		packetServerFreePacketBufferQueue(&pppNakFields); //Free the queued response!
		packetServerFreePacketBufferQueue(&pppRejectFields); //Free the queued response!
		break;
	case 0x802B: //IPXCP?
		if (!LCP_NCP) //NCP is Closed?
		{
			goto ppp_invalidprotocol; //Don't handle!
		}

		if (!PPP_consumeStream(&pppstream, &common_CodeField)) //Code couldn't be read?
		{
			return 1; //Incorrect packet: discard it!
		}
		if (!PPP_consumeStream(&pppstream, &common_IdentifierField)) //Identifier couldn't be read?
		{
			return 1; //Incorrect packet: discard it!
		}
		if (!PPP_consumeStreamBE16(&pppstream, &common_LengthField)) //Length couldn't be read?
		{
			return 1; //Incorrect packet: discard it!
		}
		if (common_LengthField < 4) //Not enough data?
		{
			return 1; //Incorrect packet: discard it!
		}
		connectedclient->ppp_suppressIPXCP &= ~3; //Don't suppress sending IPXCP packets to the client anymore now if we were suppressed!
		switch (common_CodeField) //What operation code?
		{
		case 1: //Configure-Request
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue_ipxcp; //Finish up!
			}

			memset(&ipxcp_pendingnetworknumber,0,sizeof(ipxcp_pendingnetworknumber)); //Default: none!
			memset(&ipxcp_pendingnodenumber,0,sizeof(ipxcp_pendingnodenumber)); //Node number!
			ipxcp_pendingroutingprotocol = 0; //No routing protocol!

			//Now, start parsing the options for the connection!
			for (; PPP_peekStream(&pppstream_requestfield, &common_TypeField);) //Gotten a new option to parse?
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &common_TypeField))
				{
					goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
				}
				if (!PPP_consumeStream(&pppstream_requestfield, &common_OptionLengthField))
				{
					goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
				}
				if (PPP_streamdataleft(&pppstream_requestfield) < (MAX(common_OptionLengthField, 2U) - 2U)) //Not enough room left for the option data?
				{
					goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
				}
				performskipdataNak = 0; //Default: not skipped already!
				switch (common_TypeField) //What type is specified for the option?
				{
				case 1: //IPX-Network-Number
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_ipx; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					memcpy(&ipxcp_pendingnetworknumber, &data4, 4); //Set the network number to use!
					//Field is OK!
					break;
				case 2: //IPX-Node-Number
					if (common_OptionLengthField != 8) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 8)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_ipx; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}

					if (!PPP_consumeStream(&pppstream_requestfield, &data6[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[4])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[5])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					memcpy(&ipxcp_pendingnodenumber, &data6, 6); //Set the network number to use!
					//Field is OK!
					break;
				case 4: //IPX-Routing-Protocol
					if (common_OptionLengthField != 4) //Unsupported length?
					{
						ipxcp_unsupportedroutingprotocol: //Unsupported routing protocol?
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 4)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_ipx; //Skip the data please!
					}
					if (!PPP_consumeStreamBE16(&pppstream_requestfield, &dataw)) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (dataw != 0) //Not supported?
					{
						goto ipxcp_unsupportedroutingprotocol;
					}
					ipxcp_pendingroutingprotocol = dataw; //Set the routing protocol to use!
					//Field is OK!
					break;
				case 5: //IPX-Router-Name
					performskipdataNak = 1; //Skipped already!
					goto performskipdata_ipx; //Unused parameter! Simply skip it!
				case 6: //IPX-Configuration-Complete
					/*
					if (common_OptionLengthField != 2)
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 2)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_ipx; //Skip the data anyways!
					}
					else //OK to parse normally?
					{
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_ipx; //Unused parameter! Simply skip it!
					}
					*/ //Unknown option, so Reject it!
				case 3: //IPX-Compression-Protocol
				default: //Unknown option?
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_TypeField)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_OptionLengthField)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					performskipdata_ipx:
					if (common_OptionLengthField >= 2) //Enough length to skip?
					{
						skipdatacounter = common_OptionLengthField - 2; //How much to skip!
						for (; skipdatacounter;) //Skip it!
						{
							if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Failed to consume properly?
							{
								goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
							}
							if (!performskipdataNak) //Not skipping data altogether?
							{
								if (!packetServerAddPacketBufferQueue(&pppRejectFields, datab)) //Correct length!
								{
									goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
								}
							}
							--skipdatacounter;
						}
					}
					else //Malformed parameter!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					break;
				}
			}
			//TODO: Finish parsing properly
			if (pppNakFields.buffer || pppRejectFields.buffer) //NAK or Rejected any fields? Then don't process to the connected phase!
			{
				ipxcp_requestfixnodenumber: //Fix network number supplied by authentication!
				memcpy(&connectedclient->ppp_nakfields_ipxcp, &pppNakFields, sizeof(pppNakFields)); //Give the response to the client!
				connectedclient->ppp_nakfields_ipxcp_identifier = common_IdentifierField; //Identifier!
				memcpy(&connectedclient->ppp_rejectfields_ipxcp, &pppRejectFields, sizeof(pppRejectFields)); //Give the response to the client!
				connectedclient->ppp_rejectfields_ipxcp_identifier = common_IdentifierField; //Identifier!
				memset(&pppNakFields, 0, sizeof(pppNakFields)); //Queued!
				memset(&pppRejectFields, 0, sizeof(pppRejectFields)); //Queued!
				result = 1; ///Discard!
			}
			else //OK! All parameters are fine!
			{
				if (connectedclient->ipxcp_negotiationstatus == 0) //Starting negotiation on the parameters?
				{
					if (!memcmp(&ipxcp_pendingnodenumber, &ipxnulladdr, 6)) //Null address?
					{
						connectedclient->ipxcp_negotiationstatus = 2; //NAK it!
					}
					else if (!memcmp(&ipxcp_pendingnodenumber, &ipxbroadcastaddr, 6)) //Broadcast address?
					{
						connectedclient->ipxcp_negotiationstatus = 2; //NAK it!
					}
					else if (!memcmp(&ipxcp_pendingnodenumber, &ipx_servernodeaddr, 6)) //Negotiation node server address?
					{
						connectedclient->ipxcp_negotiationstatus = 2; //NAK it!
					}
					else //Valid address to use? Start validation of existing clients!
					{
						//TODO: Check other clients for pending negotiations! Wait for other clients to complete first!
						memcpy(&connectedclient->ipxcp_networknumberecho[0], &ipxcp_pendingnetworknumber, sizeof(ipxcp_pendingnetworknumber)); //Network number specified or 0 for none!
						memcpy(&connectedclient->ipxcp_nodenumberecho[0], &ipxcp_pendingnodenumber, sizeof(ipxcp_pendingnodenumber)); //Node number or 0 for none!
						if (sendIPXechorequest(connectedclient)) //Properly sent an echo request?
						{
							connectedclient->ipxcp_negotiationstatus = 1; //Start negotiating the IPX node number!
							connectedclient->ipxcp_negotiationstatustimer = (DOUBLE)0.0f; //Restart timing!
						}
						else //Otherwise, keep pending!
						{
							goto ppp_finishpacketbufferqueue_ipxcp;
						}
					}
				}

				if (connectedclient->ipxcp_negotiationstatus == 1) //Timing the timer for negotiating the network/node address?
				{
					connectedclient->ipxcp_negotiationstatustimer += modem.networkpolltick; //Time!
					if (connectedclient->ipxcp_negotiationstatustimer >= 1500000000.0f) //Negotiation timeout?
					{
						connectedclient->ipxcp_negotiationstatus = 3; //Timeout reached! No other client responded to the request! Take the network/node address specified! 
					}
					else //Still pending?
					{
						goto ppp_finishpacketbufferqueue_ipxcp;
					}
				}

				if (connectedclient->ipxcp_negotiationstatus != 3) //Not ready yet?
				{
					if (connectedclient->ipxcp_negotiationstatus == 2) //NAK has been reached?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0x02)) //IPX node number!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						incIPXaddr(&ipxcp_pendingnodenumber[0]); //Increase the address to the first next valid address to use!
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 8)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, ipxcp_pendingnodenumber[0])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, ipxcp_pendingnodenumber[1])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, ipxcp_pendingnodenumber[2])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, ipxcp_pendingnodenumber[3])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, ipxcp_pendingnodenumber[4])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, ipxcp_pendingnodenumber[5])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						goto ipxcp_requestfixnodenumber; //Request a fix for the node number!
					}
				}

				//Apply the parameters to the session and send back an request-ACK!
				memset(&response, 0, sizeof(response)); //Init the response!
				//Build the PPP header first!
				if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
				{
					goto ppp_finishpacketbufferqueue_ipxcp; //Finish up!
				}
				if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, protocol, 0x02, common_IdentifierField, PPP_streamdataleft(&pppstream_requestfield)))
				{
					goto ppp_finishpacketbufferqueue_ipxcp; //Finish up!
				}
				for (; PPP_streamdataleft(&pppstream_requestfield);) //Data left?
				{
					if (!PPP_consumeStream(&pppstream_requestfield, &datab))
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&response, datab)) //Add it!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Finish up!
					}
				}
				//Calculate and add the checksum field!
				if (PPP_addFCS(&response,connectedclient,protocol))
				{
					goto ppp_finishpacketbufferqueue_ipxcp;
				}
				//Packet is fully built. Now send it!
				if (connectedclient->ppp_response.size) //Previous Response still valid?
				{
					goto ppp_finishpacketbufferqueue_ipxcp; //Keep pending!
				}
				if (response.buffer) //Any response to give?
				{
					memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
					ppp_responseforuser(connectedclient); //A response is ready!
					memset(&response, 0, sizeof(response)); //Parsed!
					//Now, apply the request properly!
					connectedclient->ppp_IPXCPstatus[0] = 1; //Open!
					connectedclient->ppp_suppressIPXCP &= ~1; //Default: not supressing as we're opened!
					connectedclient->ipxcp_negotiationstatus = 0; //No negotation anymore!
					if (connectedclient->ppp_IPXCPstatus[1]) //Open?
					{
						connectedclient->ppp_suppressIPX = 0; //Default: not supressing as we're opened!
					}
					memcpy(&connectedclient->ipxcp_networknumber[0],&ipxcp_pendingnetworknumber, sizeof(ipxcp_pendingnetworknumber)); //Network number specified or 0 for none!
					memcpy(&connectedclient->ipxcp_nodenumber[0],&ipxcp_pendingnodenumber, sizeof(ipxcp_pendingnodenumber)); //Node number or 0 for none!
					connectedclient->ipxcp_routingprotocol[0] = ipxcp_pendingroutingprotocol; //The routing protocol!
				}
			}
			goto ppp_finishpacketbufferqueue2_ipxcp; //Finish up!
			break;
		case 5: //Terminate-Request (Request termination of connection)
			//Send a Code-Reject packet to the client!
			memset(&response, 0, sizeof(response)); //Init the response!
			//Build the PPP header first!
			if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, protocol, 0x06, common_IdentifierField, PPP_streamdataleft(&pppstream)))
			{
				goto ppp_finishpacketbufferqueue_ipxcp;
			}
			//Now, the rejected packet itself!
			for (; PPP_consumeStream(&pppstream, &datab);) //The data field itself follows!
			{
				if (!packetServerAddPacketBufferQueue(&response, datab))
				{
					goto ppp_finishpacketbufferqueue_ipxcp;
				}
			}
			//Calculate and add the checksum field!
			if (PPP_addFCS(&response,connectedclient,protocol))
			{
				goto ppp_finishpacketbufferqueue_ipxcp;
			}
			//Packet is fully built. Now send it!
			if (connectedclient->ppp_response.size) //Previous Response still valid?
			{
				goto ppp_finishpacketbufferqueue_ipxcp; //Keep pending!
			}
			if (response.buffer) //Any response to give?
			{
				memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
				ppp_responseforuser(connectedclient); //A response is ready!
				memset(&response, 0, sizeof(response)); //Parsed!
				//Now, apply the request properly!
				connectedclient->ppp_IPXCPstatus[0] = 0; //Closed!
				connectedclient->ipxcp_negotiationstatus = 0; //No negotation yet!
			}
			goto ppp_finishpacketbufferqueue2_ipxcp; //Finish up!
			break;
		case 2: //Configure-Ack (All options OK)
			if (common_IdentifierField != connectedclient->ppp_servercurrentIPXCPidentifier) //Identifier mismatch?
			{
				result = 1; //Discard this packet!
				goto ppp_finishpacketbufferqueue2; //Finish up!
			}
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			memset(&ipxcp_pendingnetworknumber, 0, sizeof(ipxcp_pendingnetworknumber)); //Default: none!
			memset(&ipxcp_pendingnodenumber, 0, sizeof(ipxcp_pendingnodenumber)); //Node number!
			ipxcp_pendingroutingprotocol = 0; //No routing protocol!

			//Now, start parsing the options for the connection!
			for (; PPP_peekStream(&pppstream_requestfield, &common_TypeField);) //Gotten a new option to parse?
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &common_TypeField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (!PPP_consumeStream(&pppstream_requestfield, &common_OptionLengthField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (PPP_streamdataleft(&pppstream_requestfield) < (MAX(common_OptionLengthField, 2U) - 2U)) //Not enough room left for the option data?
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				switch (common_TypeField) //What type is specified for the option?
				{
				case 1: //IPX-Network-Number
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						goto performskipdata_ipxcp2; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					memcpy(&ipxcp_pendingnetworknumber, &data4, 4); //Set the network number to use!
					//Field is OK!
					break;
				case 2: //IPX-Node-Number
					if (common_OptionLengthField != 8) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 8)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						goto performskipdata_ipxcp2; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}

					if (!PPP_consumeStream(&pppstream_requestfield, &data6[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[4])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[5])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					memcpy(&ipxcp_pendingnodenumber, &data6, 6); //Set the network number to use!
					//Field is OK!
					break;
				case 4: //IPX-Routing-Protocol
					if (common_OptionLengthField != 4) //Unsupported length?
					{
					ipxcp_unsupportedroutingprotocol2: //Unsupported routing protocol?
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 4)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						goto performskipdata_ipxcp2; //Skip the data please!
					}
					if (!PPP_consumeStreamBE16(&pppstream_requestfield, &dataw)) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (dataw != 0) //Not supported?
					{
						goto ipxcp_unsupportedroutingprotocol2;
					}
					ipxcp_pendingroutingprotocol = dataw; //Set the routing protocol to use!
					//Field is OK!
					break;
				case 5: //IPX-Router-Name
					goto performskipdata_ipx; //Unused parameter! Simply skip it!
				case 6: //IPX-Configuration-Complete
					if (common_OptionLengthField != 2)
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 2)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						goto performskipdata_ipxcp2; //Skip the data anyways!
					}
					else //OK to parse normally?
					{
						goto performskipdata_ipxcp2; //Unused parameter! Simply skip it!
					}
				case 3: //IPX-Compression-Protocol
				default: //Unknown option?
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_TypeField)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_OptionLengthField)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
				performskipdata_ipxcp2:
					if (common_OptionLengthField >= 2) //Enough length to skip?
					{
						skipdatacounter = common_OptionLengthField - 2; //How much to skip!
						for (; skipdatacounter;) //Skip it!
						{
							if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Failed to consume properly?
							{
								goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppRejectFields, datab)) //Correct length!
							{
								goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
							}
							--skipdatacounter;
						}
					}
					else //Malformed parameter!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					break;
				}
			}

			//TODO: Finish parsing properly
			if (pppNakFields.buffer || pppRejectFields.buffer) //NAK or Rejected any fields? Then don't process to the connected phase!
			{
				connectedclient->ppp_serverIPXCPstatus = 2; //Reset the status check to try again afterwards if it's reset again!
			}
			else //OK! All parameters are fine!
			{
				//Apply the parameters to the session and start the connection!
				//Now, apply the request properly!
				connectedclient->ppp_IPXCPstatus[1] = 1; //Open!
				connectedclient->ppp_suppressIPXCP &= ~2; //Default: not supressing as we're opened!
				if (connectedclient->ppp_IPXCPstatus[0]) //Open?
				{
					connectedclient->ppp_suppressIPX = 0; //Default: not supressing as we're opened!
				}
				memcpy(&connectedclient->ipxcp_networknumber[1], &ipxcp_pendingnetworknumber, sizeof(ipxcp_pendingnetworknumber)); //Network number specified or 0 for none!
				memcpy(&connectedclient->ipxcp_nodenumber[1], &ipxcp_pendingnodenumber, sizeof(ipxcp_pendingnodenumber)); //Node number or 0 for none!
				connectedclient->ipxcp_routingprotocol[1] = ipxcp_pendingroutingprotocol; //The routing protocol!
		//connectedclient->ipxcp_negotiationstatus = 0; //No negotation yet!
				connectedclient->ppp_serverIPXCPstatus = 2; //Reset the status check to try again afterwards if it's reset again!
			}
			result = 1; //Discard it!
			goto ppp_finishpacketbufferqueue2; //Finish up!
			break;
		case 3: //Configure-Nak (Some options unacceptable)
		case 4: //Configure-Reject (Some options not recognisable or acceptable for negotiation)
			if (common_IdentifierField != connectedclient->ppp_servercurrentIPXCPidentifier) //Identifier mismatch?
			{
				result = 1; //Discard this packet!
				goto ppp_finishpacketbufferqueue2; //Finish up!
			}
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}

			request_NakRejectnetworknumber = 0;
			request_NakRejectnodenumber = 0;
			request_NakRejectroutingprotocol = 0;
			memcpy(&ipxcp_pendingnetworknumber,connectedclient->ppp_serverIPXCP_havenetworknumber?&connectedclient->ppp_serverIPXCP_pendingnetworknumber:&no_network_number, sizeof(ipxcp_pendingnetworknumber)); //Default: none!
			memcpy(&ipxcp_pendingnodenumber,connectedclient->ppp_serverIPXCP_havenodenumber? &connectedclient->ppp_serverIPXCP_pendingnodenumber:&no_node_number, sizeof(ipxcp_pendingnodenumber)); //Node number!
			ipxcp_pendingroutingprotocol = 0; //No routing protocol!

			//Now, start parsing the options for the connection!
			for (; PPP_peekStream(&pppstream_requestfield, &common_TypeField);) //Gotten a new option to parse?
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &common_TypeField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (!PPP_consumeStream(&pppstream_requestfield, &common_OptionLengthField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (PPP_streamdataleft(&pppstream_requestfield) < (MAX(common_OptionLengthField, 2U) - 2U)) //Not enough room left for the option data?
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				switch (common_TypeField) //What type is specified for the option?
				{
				case 1: //IPX-Network-Number
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						goto performskipdata_ipxcpnakreject; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					memcpy(&ipxcp_pendingnetworknumber, &data4, 4); //Set the network number to use!
					request_NakRejectnetworknumber = 1; //This was Nak/Rejected!
					//Field is OK!
					break;
				case 2: //IPX-Node-Number
					if (common_OptionLengthField != 8) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 8)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						goto performskipdata_ipxcpnakreject; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}

					if (!PPP_consumeStream(&pppstream_requestfield, &data6[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[4])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data6[5])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					memcpy(&ipxcp_pendingnodenumber, &data6, 6); //Set the network number to use!
					request_NakRejectnodenumber = 1; //This was Nak/Rejected!
					//Field is OK!
					break;
				case 4: //IPX-Routing-Protocol
					if (common_OptionLengthField != 4) //Unsupported length?
					{
					//ipxcp_unsupportedroutingprotocol3: //Unsupported routing protocol?
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 4)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
						}
						goto performskipdata_ipxcpnakreject; //Skip the data please!
					}
					if (!PPP_consumeStreamBE16(&pppstream_requestfield, &dataw)) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					/*
					* Do we need to check this?
					if (dataw != 0) //Not supported?
					{
						goto ipxcp_unsupportedroutingprotocol3;
					}
					*/
					ipxcp_pendingroutingprotocol = dataw; //Set the routing protocol to use!
					request_NakRejectroutingprotocol = 1; //This was Nak/Rejected!
					//Field is OK!
					break;
				default: //Unknown option?
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_TypeField)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_OptionLengthField)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
				performskipdata_ipxcpnakreject:
					if (common_OptionLengthField >= 2) //Enough length to skip?
					{
						skipdatacounter = common_OptionLengthField - 2; //How much to skip!
						for (; skipdatacounter;) //Skip it!
						{
							if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Failed to consume properly?
							{
								goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppRejectFields, datab)) //Correct data!
							{
								goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
							}
							--skipdatacounter;
						}
					}
					else //Malformed parameter!
					{
						goto ppp_finishpacketbufferqueue_ipxcp; //Incorrect packet: discard it!
					}
					break;
				}
			}
			if ((pppNakFields.length == 0) && (pppRejectFields.length == 0)) //OK to process?
			{
				if (request_NakRejectnetworknumber && (common_CodeField == 4)) //Reject-Network-Number?
				{
					connectedclient->ppp_serverIPXCP_havenetworknumber = 0; //Don't request anymore!
				}
				else if (request_NakRejectnetworknumber) //Network-Number change requested?
				{
					memcpy(&connectedclient->ppp_serverIPXCP_pendingnetworknumber, &ipxcp_pendingnetworknumber, sizeof(connectedclient->ppp_serverIPXCP_pendingnetworknumber)); //The request node number to use!
					connectedclient->ppp_serverIPXCP_havenetworknumber = 1; //Request now!
				}
				if (request_NakRejectnodenumber && (common_CodeField == 4)) //Reject-Node-Number?
				{
					connectedclient->ppp_serverIPXCP_havenodenumber = 0; //Don't request anymore!
				}
				else if (request_NakRejectnodenumber) //Node-Number change requested?
				{
					memcpy(&connectedclient->ppp_serverIPXCP_pendingnodenumber, &ipxcp_pendingnodenumber, sizeof(connectedclient->ppp_serverIPXCP_pendingnodenumber)); //The request node number to use!
					connectedclient->ppp_serverIPXCP_havenodenumber = 1; //Request now!
				}
				if (request_NakRejectroutingprotocol && (common_CodeField == 4)) //Reject-Routing-Protocol?
				{
					connectedclient->ppp_serverIPXCP_haveroutingprotocol = 0; //Don't request anymore!
				}
				else if (request_NakRejectroutingprotocol) //Routing-Protocol change requested?
				{
					connectedclient->ppp_serverIPXCP_pendingroutingprotocol = ipxcp_pendingroutingprotocol; //The request node number to use!
					connectedclient->ppp_serverIPXCP_haveroutingprotocol = 1; //Request now!
				}
				connectedclient->ppp_serverIPXCPstatus = 3; //Reset the status check to try again afterwards if it's reset again!
			}
			result = 1; //Success!
			goto ppp_finishpacketbufferqueue2_ipxcp; //Finish up!
			break;
		case 6: //Terminate-Ack (Acnowledge termination of connection)
			//Why would we need to handle this if the client can't have it's connection terminated by us!
			connectedclient->ppp_IPXCPstatus[1] = 0; //Close our connection?
			result = 1; //Success!
			goto ppp_finishpacketbufferqueue2_ipxcp; //Finish up!
			break;
		case 7: //Code-Reject (Code field is rejected because it's unknown)
			//Do anything with this?
			result = 1; //Discard!
			goto ppp_finishpacketbufferqueue2_ipxcp; //Finish up!
			break; //Not handled!
		default: //Unknown Code field?
			//Send a Code-Reject packet to the client!
			memset(&response, 0, sizeof(response)); //Init the response!
			//Build the PPP header first!
			if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, protocol, 0x07, common_IdentifierField, PPP_streamdataleft(&pppstream_informationfield)))
			{
				goto ppp_finishpacketbufferqueue_ipxcp; //Finish up!
			}
			//Now, the rejected packet itself!
			for (; PPP_consumeStream(&pppstream_informationfield, &datab);) //The information field itself follows!
			{
				if (!packetServerAddPacketBufferQueue(&response, datab))
				{
					goto ppp_finishpacketbufferqueue_ipxcp;
				}
			}
			//Calculate and add the checksum field!
			if (PPP_addFCS(&response,connectedclient,protocol))
			{
				goto ppp_finishpacketbufferqueue_ipxcp;
			}
			break;
		}
		//Packet is fully built. Now send it!
		if (connectedclient->ppp_response.size) //Previous Response still valid?
		{
			goto ppp_finishpacketbufferqueue_ipxcp; //Keep pending!
		}
		if (response.buffer) //Any response to give?
		{
			memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
			ppp_responseforuser(connectedclient); //A response is ready!
			memset(&response, 0, sizeof(response)); //Parsed!
		}
		goto ppp_finishpacketbufferqueue2_ipxcp; //Success!
	ppp_finishpacketbufferqueue_ipxcp: //An error occurred during the response?
		result = 0; //Keep pending until we can properly handle it!
	ppp_finishpacketbufferqueue2_ipxcp:
		packetServerFreePacketBufferQueue(&response); //Free the queued response!
		packetServerFreePacketBufferQueue(&pppNakFields); //Free the queued response!
		packetServerFreePacketBufferQueue(&pppRejectFields); //Free the queued response!
		break;
	case 0x8021: //IPCP?
		if (!LCP_NCP) //NCP is Closed?
		{
			goto ppp_invalidprotocol; //Don't handle!
		}

		if (!PPP_consumeStream(&pppstream, &common_CodeField)) //Code couldn't be read?
		{
			return 1; //Incorrect packet: discard it!
		}
		if (!PPP_consumeStream(&pppstream, &common_IdentifierField)) //Identifier couldn't be read?
		{
			return 1; //Incorrect packet: discard it!
		}
		if (!PPP_consumeStreamBE16(&pppstream, &common_LengthField)) //Length couldn't be read?
		{
			return 1; //Incorrect packet: discard it!
		}
		if (common_LengthField < 4) //Not enough data?
		{
			return 1; //Incorrect packet: discard it!
		}
		connectedclient->ppp_suppressIPCP &= ~3; //Don't suppress sending IPCP packets to the client anymore now if we were suppressed!
		switch (common_CodeField) //What operation code?
		{
		case 1: //Configure-Request
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue_ipcp; //Finish up!
			}

			memset(&ipcp_pendingipaddress,0,sizeof(ipcp_pendingipaddress)); //Default: none!
			memset(&ipcp_pendingDNS1ipaddress, 0, sizeof(ipcp_pendingDNS1ipaddress)); //Default: none!
			memset(&ipcp_pendingDNS2ipaddress, 0, sizeof(ipcp_pendingDNS2ipaddress)); //Default: none!
			memset(&ipcp_pendingNBNS1ipaddress, 0, sizeof(ipcp_pendingNBNS1ipaddress)); //Default: none!
			memset(&ipcp_pendingNBNS2ipaddress, 0, sizeof(ipcp_pendingNBNS2ipaddress)); //Default: none!
			memcpy(&ipcp_pendingsubnetmaskipaddress, &packetserver_subnetmaskIPaddr, sizeof(ipcp_pendingsubnetmaskipaddress)); //Default: specified in the settings!

			//Now, start parsing the options for the connection!
			for (; PPP_peekStream(&pppstream_requestfield, &common_TypeField);) //Gotten a new option to parse?
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &common_TypeField))
				{
					goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
				}
				if (!PPP_consumeStream(&pppstream_requestfield, &common_OptionLengthField))
				{
					goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
				}
				if (PPP_streamdataleft(&pppstream_requestfield) < (MAX(common_OptionLengthField, 2U) - 2U)) //Not enough room left for the option data?
				{
					goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
				}
				performskipdataNak = 0; //Default: not skipped already!
				switch (common_TypeField) //What type is specified for the option?
				{
				case 3: //IP address
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->packetserver_staticIP[0])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->packetserver_staticIP[1])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->packetserver_staticIP[2])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->packetserver_staticIP[3])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_ipcp; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					memcpy(&ipcp_pendingipaddress, &data4, 4); //Set the network number to use!
					//Field is OK!

					if (memcmp(&ipcp_pendingipaddress, &ipnulladdr, 4) == 0) //0.0.0.0 specified? The client asks for an IP address!
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //IP address!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->packetserver_staticIP[0])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->packetserver_staticIP[1])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->packetserver_staticIP[2])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, connectedclient->packetserver_staticIP[3])) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
					}
					break;
				case 0x81: //DNS #1 address
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS1IP ? packetserver_DNS1IPaddr[0] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS1IP ? packetserver_DNS1IPaddr[1] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS1IP ? packetserver_DNS1IPaddr[2] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS1IP ? packetserver_DNS1IPaddr[3] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_ipcp; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					memcpy(&ipcp_pendingDNS1ipaddress, &data4, 4); //Set the network number to use!
					//Field is OK!

					if ((memcmp(&ipcp_pendingDNS1ipaddress, &ipnulladdr, 4) == 0)) //0.0.0.0 specified? The client asks for an IP address!
					{
						if (packetserver_DNS1IP) //Can supply?
						{
							if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //IP address!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS1IPaddr[0])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS1IPaddr[1])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS1IPaddr[2])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS1IPaddr[3])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
						}
						else //Can't comply?
						{
							performskipdataNak = 2; //Read already in data4!
							goto performrejectfield_ipcp; //Reject it!
						}
					}
					break;
				case 0x82: //NBNS #1 address
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS1IP ? packetserver_NBNS1IPaddr[0] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS1IP ? packetserver_NBNS1IPaddr[1] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS1IP ? packetserver_NBNS1IPaddr[2] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS1IP ? packetserver_NBNS1IPaddr[3] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_ipcp; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					memcpy(&ipcp_pendingNBNS1ipaddress, &data4, 4); //Set the network number to use!
					//Field is OK!
					
					if ((memcmp(&ipcp_pendingNBNS1ipaddress, &ipnulladdr, 4) == 0)) //0.0.0.0 specified? The client asks for an IP address!
					{
						if (packetserver_NBNS1IP) //Can supply?
						{
							if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //IP address!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS1IPaddr[0])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS1IPaddr[1])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS1IPaddr[2])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS1IPaddr[3])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
						}
						else
						{
							performskipdataNak = 2; //Read already in data4!
							goto performrejectfield_ipcp; //Reject it!
						}
					}
					break;
				case 0x83: //DNS #2 address
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS2IP ? packetserver_DNS2IPaddr[0] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS2IP ? packetserver_DNS2IPaddr[1] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS2IP ? packetserver_DNS2IPaddr[2] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS2IP ? packetserver_DNS2IPaddr[3] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_ipcp; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					memcpy(&ipcp_pendingDNS2ipaddress, &data4, 4); //Set the network number to use!
					//Field is OK!

					if ((memcmp(&ipcp_pendingDNS2ipaddress, &ipnulladdr, 4) == 0)) //0.0.0.0 specified? The client asks for an IP address!
					{
						if (packetserver_DNS2IP) //Can supply?
						{
							if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //IP address!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS2IPaddr[0])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS2IPaddr[1])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS2IPaddr[2])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_DNS2IPaddr[3])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
						}
						else
						{
							performskipdataNak = 2; //Read already in data4!
							goto performrejectfield_ipcp; //Reject it!
						}
					}
					break;
				case 0x84: //NBNS #2 address
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS2IP ? packetserver_NBNS2IPaddr[0] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS2IP ? packetserver_NBNS2IPaddr[1] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS2IP ? packetserver_NBNS2IPaddr[2] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS2IP ? packetserver_NBNS2IPaddr[3] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_ipcp; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					memcpy(&ipcp_pendingNBNS2ipaddress, &data4, 4); //Set the network number to use!
					//Field is OK!
					
					if ((memcmp(&ipcp_pendingNBNS2ipaddress, &ipnulladdr, 4) == 0)) //0.0.0.0 specified? The client asks for an IP address!
					{
						if (packetserver_NBNS2IP) //Can supply?
						{
							if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //IP address!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS2IPaddr[0])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS2IPaddr[1])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS2IPaddr[2])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_NBNS2IPaddr[3])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
						}
						else
						{
							performskipdataNak = 2; //Read already in data4!
							goto performrejectfield_ipcp; //Reject it!
						}
					}
					break;
				case 0x90: //subnet mask
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_subnetmaskIP ? packetserver_subnetmaskIPaddr[0] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_subnetmaskIP ? packetserver_subnetmaskIPaddr[1] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_subnetmaskIP ? packetserver_subnetmaskIPaddr[2] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_subnetmaskIP ? packetserver_subnetmaskIPaddr[3] : 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						performskipdataNak = 1; //Skipped already!
						goto performskipdata_ipcp; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					memcpy(&ipcp_pendingsubnetmaskipaddress, &data4, 4); //Set the network number to use!
					//Field is OK!

					if ((memcmp(&ipcp_pendingsubnetmaskipaddress, &ipnulladdr, 4) == 0)) //0.0.0.0 specified? The client asks for an IP address!
					{
						if (packetserver_subnetmaskIP) //Can supply?
						{
							if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //IP address!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_subnetmaskIPaddr[0])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_subnetmaskIPaddr[1])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_subnetmaskIPaddr[2])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppNakFields, packetserver_subnetmaskIPaddr[3])) //None!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
						}
						else
						{
							performskipdataNak = 2; //Read already in data4!
							goto performrejectfield_ipcp; //Reject it!
						}
					}
					break;
				default: //Unknown option?
					performrejectfield_ipcp:
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_TypeField)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_OptionLengthField)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					performskipdata_ipcp:
					if (common_OptionLengthField >= 2) //Enough length to skip?
					{
						skipdatacounter = common_OptionLengthField - 2; //How much to skip!
						if (performskipdataNak == 2) //Field already read into data2?
						{
							if (!packetServerAddPacketBufferQueue(&pppRejectFields, data4[0])) //Correct length!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppRejectFields, data4[1])) //Correct length!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppRejectFields, data4[2])) //Correct length!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppRejectFields, data4[3])) //Correct length!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
						}
						else //Normal behaviour? Skipping or rejecting the field fully.
						{
							for (; skipdatacounter;) //Skip it!
							{
								if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Failed to consume properly?
								{
									goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
								}
								if (!performskipdataNak) //Not skipping data altogether?
								{
									if (!packetServerAddPacketBufferQueue(&pppRejectFields, datab)) //Correct length!
									{
										goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
									}
								}
								--skipdatacounter;
							}
						}
					}
					else //Malformed parameter!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					break;
				}
			}
			//TODO: Finish parsing properly
			if (pppNakFields.buffer || pppRejectFields.buffer) //NAK or Rejected any fields? Then don't process to the connected phase!
			{
				//ipcp_requestfixnodenumber: //Fix network number supplied by authentication!
				memcpy(&connectedclient->ppp_nakfields_ipcp, &pppNakFields, sizeof(pppNakFields)); //Give the response to the client!
				connectedclient->ppp_nakfields_ipcp_identifier = common_IdentifierField; //Identifier!
				memcpy(&connectedclient->ppp_rejectfields_ipcp, &pppRejectFields, sizeof(pppRejectFields)); //Give the response to the client!
				connectedclient->ppp_rejectfields_ipcp_identifier = common_IdentifierField; //Identifier!
				memset(&pppNakFields, 0, sizeof(pppNakFields)); //Queued!
				memset(&pppRejectFields, 0, sizeof(pppRejectFields)); //Queued!
				result = 1; ///Discard!
			}
			else //OK! All parameters are fine!
			{
				/*
				if (connectedclient->ipxcp_negotiationstatus == 0) //Starting negotiation on the parameters?
				{
					if (!memcmp(&ipxcp_pendingnodenumber, &ipxnulladdr, 6)) //Null address?
					{
						connectedclient->ipxcp_negotiationstatus = 2; //NAK it!
					}
					else if (!memcmp(&ipxcp_pendingnodenumber, &ipxbroadcastaddr, 6)) //Broadcast address?
					{
						connectedclient->ipxcp_negotiationstatus = 2; //NAK it!
					}
					else if (!memcmp(&ipxcp_pendingnodenumber, &ipx_servernodeaddr, 6)) //Negotiation node server address?
					{
						connectedclient->ipxcp_negotiationstatus = 2; //NAK it!
					}
					else //Valid address to use? Start validation of existing clients!
					{
						//TODO: Check other clients for pending negotiations! Wait for other clients to complete first!
						memcpy(&connectedclient->ipxcp_ipaddressecho[0], &ipcp_pendingipaddress, sizeof(ipcp_pendingipaddress)); //Network number specified or 0 for none!
						memcpy(&connectedclient->ipxcp_nodenumberecho[0], &ipxcp_pendingnodenumber, sizeof(ipxcp_pendingnodenumber)); //Node number or 0 for none!
						if (sendIPXechorequest(connectedclient)) //Properly sent an echo request?
						{
							connectedclient->ipxcp_negotiationstatus = 1; //Start negotiating the IPX node number!
							connectedclient->ipxcp_negotiationstatustimer = (DOUBLE)0.0f; //Restart timing!
						}
						else //Otherwise, keep pending!
						{
							goto ppp_finishpacketbufferqueue_ipcp;
						}
					}
				}

				if (connectedclient->ipxcp_negotiationstatus == 1) //Timing the timer for negotiating the network/node address?
				{
					connectedclient->ipxcp_negotiationstatustimer += modem.networkpolltick; //Time!
					if (connectedclient->ipxcp_negotiationstatustimer >= 1500000000.0f) //Negotiation timeout?
					{
						connectedclient->ipxcp_negotiationstatus = 3; //Timeout reached! No other client responded to the request! Take the network/node address specified! 
					}
					else //Still pending?
					{
						goto ppp_finishpacketbufferqueue_ipcp;
					}
				}
				*/

				//Apply the parameters to the session and send back an request-ACK!
				memset(&response, 0, sizeof(response)); //Init the response!
				//Build the PPP header first!
				if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
				{
					goto ppp_finishpacketbufferqueue_ipcp; //Finish up!
				}
				if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, protocol, 0x02, common_IdentifierField, PPP_streamdataleft(&pppstream_requestfield)))
				{
					goto ppp_finishpacketbufferqueue_ipcp; //Finish up!
				}
				for (; PPP_streamdataleft(&pppstream_requestfield);) //Data left?
				{
					if (!PPP_consumeStream(&pppstream_requestfield, &datab))
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&response, datab)) //Add it!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Finish up!
					}
				}
				//Calculate and add the checksum field!
				if (PPP_addFCS(&response,connectedclient,protocol))
				{
					goto ppp_finishpacketbufferqueue_ipcp;
				}
				//Packet is fully built. Now send it!
				if (connectedclient->ppp_response.size) //Previous Response still valid?
				{
					goto ppp_finishpacketbufferqueue_ipcp; //Keep pending!
				}
				if (response.buffer) //Any response to give?
				{
					memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
					ppp_responseforuser(connectedclient); //A response is ready!
					memset(&response, 0, sizeof(response)); //Parsed!
					//Now, apply the request properly!
					connectedclient->ppp_IPCPstatus[0] = 1; //Open!
					connectedclient->ppp_suppressIPCP &= ~1; //Default: not supressing as we're opened!
					if (connectedclient->ppp_IPCPstatus[1]) //Open?
					{
						connectedclient->ppp_suppressIP = 0; //Default: not supressing as we're opened!
					}
					//connectedclient->ipxcp_negotiationstatus = 0; //No negotation anymore!
					memcpy(&connectedclient->ipcp_ipaddress[0],&ipcp_pendingipaddress, sizeof(ipcp_pendingipaddress)); //Network number specified or 0 for none!
					memcpy(&connectedclient->ipcp_DNS1ipaddress[0], &ipcp_pendingDNS1ipaddress, sizeof(ipcp_pendingDNS1ipaddress)); //Network number specified or 0 for none!
					memcpy(&connectedclient->ipcp_DNS2ipaddress[0], &ipcp_pendingDNS2ipaddress, sizeof(ipcp_pendingDNS2ipaddress)); //Network number specified or 0 for none!
					memcpy(&connectedclient->ipcp_NBNS1ipaddress[0], &ipcp_pendingNBNS1ipaddress, sizeof(ipcp_pendingNBNS1ipaddress)); //Network number specified or 0 for none!
					memcpy(&connectedclient->ipcp_NBNS2ipaddress[0], &ipcp_pendingNBNS2ipaddress, sizeof(ipcp_pendingNBNS2ipaddress)); //Network number specified or 0 for none!
					memcpy(&connectedclient->ipcp_subnetmaskipaddress[0], &ipcp_pendingsubnetmaskipaddress, sizeof(ipcp_pendingsubnetmaskipaddress)); //Network number specified or 0 for none!
					memcpy(&connectedclient->ipcp_subnetmaskipaddressd,&connectedclient->ipcp_subnetmaskipaddress[0],4); //Subnet mask for the client to use!
				}
			}
			goto ppp_finishpacketbufferqueue2_ipcp; //Finish up!
			break;
		case 5: //Terminate-Request (Request termination of connection)
			//Send a Code-Reject packet to the client!
			memset(&response, 0, sizeof(response)); //Init the response!
			//Build the PPP header first!
			if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, protocol, 0x06, common_IdentifierField, PPP_streamdataleft(&pppstream)))
			{
				goto ppp_finishpacketbufferqueue_ipcp;
			}
			//Now, the rejected packet itself!
			for (; PPP_consumeStream(&pppstream, &datab);) //The data field itself follows!
			{
				if (!packetServerAddPacketBufferQueue(&response, datab))
				{
					goto ppp_finishpacketbufferqueue_ipcp;
				}
			}
			//Calculate and add the checksum field!
			if (PPP_addFCS(&response,connectedclient,protocol))
			{
				goto ppp_finishpacketbufferqueue_ipcp;
			}
			//Packet is fully built. Now send it!
			if (connectedclient->ppp_response.size) //Previous Response still valid?
			{
				goto ppp_finishpacketbufferqueue_ipcp; //Keep pending!
			}
			if (response.buffer) //Any response to give?
			{
				memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
				ppp_responseforuser(connectedclient); //A response is ready!
				memset(&response, 0, sizeof(response)); //Parsed!
				//Now, apply the request properly!
				connectedclient->ppp_IPCPstatus[0] = 0; //Closed!
				//connectedclient->ipxcp_negotiationstatus = 0; //No negotation yet!
			}
			goto ppp_finishpacketbufferqueue2_ipcp; //Finish up!
			break;
		case 2: //Configure-Ack (All options OK)
			if (common_IdentifierField != connectedclient->ppp_servercurrentIPCPidentifier) //Identifier mismatch?
			{
				result = 1; //Discard this packet!
				goto ppp_finishpacketbufferqueue2; //Finish up!
			}
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			memset(&ipcp_pendingipaddress, 0, sizeof(ipcp_pendingipaddress)); //Default: none!

			//Now, start parsing the options for the connection!
			for (; PPP_peekStream(&pppstream_requestfield, &common_TypeField);) //Gotten a new option to parse?
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &common_TypeField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (!PPP_consumeStream(&pppstream_requestfield, &common_OptionLengthField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (PPP_streamdataleft(&pppstream_requestfield) < (MAX(common_OptionLengthField, 2U) - 2U)) //Not enough room left for the option data?
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				switch (common_TypeField) //What type is specified for the option?
				{
				case 3: //IP address
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						goto performskipdata_ipcp2; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					memcpy(&ipcp_pendingipaddress, &data4, 4); //Set the network number to use!
					//Field is OK!
					break;
				default: //Unknown option?
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_TypeField)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_OptionLengthField)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
				performskipdata_ipcp2:
					if (common_OptionLengthField >= 2) //Enough length to skip?
					{
						skipdatacounter = common_OptionLengthField - 2; //How much to skip!
						for (; skipdatacounter;) //Skip it!
						{
							if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Failed to consume properly?
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppRejectFields, datab)) //Correct length!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							--skipdatacounter;
						}
					}
					else //Malformed parameter!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					break;
				}
			}

			//TODO: Finish parsing properly
			if (pppNakFields.buffer || pppRejectFields.buffer) //NAK or Rejected any fields? Then don't process to the connected phase!
			{
				connectedclient->ppp_serverIPCPstatus = 2; //Reset the status check to try again afterwards if it's reset again!
			}
			else //OK! All parameters are fine!
			{
				//Apply the parameters to the session and start the connection!
				//Now, apply the request properly!
				connectedclient->ppp_IPCPstatus[1] = 1; //Open!
				connectedclient->ppp_suppressIPCP &= ~2; //Default: not supressing as we're opened!
				if (connectedclient->ppp_IPCPstatus[0]) //Open?
				{
					connectedclient->ppp_suppressIP = 0; //Default: not supressing as we're opened!
				}
				memcpy(&connectedclient->ipcp_ipaddress[1], &ipcp_pendingipaddress, sizeof(ipcp_pendingipaddress)); //Network number specified or 0 for none!
		//connectedclient->ipxcp_negotiationstatus = 0; //No negotation yet!
				connectedclient->ppp_serverIPCPstatus = 2; //Reset the status check to try again afterwards if it's reset again!
			}
			result = 1; //Discard it!
			goto ppp_finishpacketbufferqueue2; //Finish up!
			break;
		case 3: //Configure-Nak (Some options unacceptable)
		case 4: //Configure-Reject (Some options not recognisable or acceptable for negotiation)
			if (common_IdentifierField != connectedclient->ppp_servercurrentIPCPidentifier) //Identifier mismatch?
			{
				result = 1; //Discard this packet!
				goto ppp_finishpacketbufferqueue2; //Finish up!
			}
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, MAX(common_LengthField, 4) - 4)) //Not enough room for the data?
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}

			request_NakRejectipaddress = 0;
			memcpy(&ipcp_pendingipaddress,connectedclient->ppp_serverIPCP_haveipaddress?&connectedclient->ppp_serverIPCP_pendingipaddress:&no_network_number, sizeof(ipcp_pendingipaddress)); //Default: none!

			//Now, start parsing the options for the connection!
			for (; PPP_peekStream(&pppstream_requestfield, &common_TypeField);) //Gotten a new option to parse?
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &common_TypeField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (!PPP_consumeStream(&pppstream_requestfield, &common_OptionLengthField))
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				if (PPP_streamdataleft(&pppstream_requestfield) < (MAX(common_OptionLengthField, 2U) - 2U)) //Not enough room left for the option data?
				{
					goto ppp_finishpacketbufferqueue; //Incorrect packet: discard it!
				}
				switch (common_TypeField) //What type is specified for the option?
				{
				case 3: //IP address
					if (common_OptionLengthField != 6) //Unsupported length?
					{
						if (!packetServerAddPacketBufferQueue(&pppNakFields, common_TypeField)) //NAK it!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 6)) //Correct length!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						if (!packetServerAddPacketBufferQueue(&pppNakFields, 0)) //None!
						{
							goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
						}
						goto performskipdata_ipcpnakreject; //Skip the data please!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[0])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[1])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[2])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!PPP_consumeStream(&pppstream_requestfield, &data4[3])) //Pending Node Number field!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					memcpy(&ipcp_pendingipaddress, &data4, 4); //Set the network number to use!
					request_NakRejectipaddress = 1; //This was Nak/Rejected!
					//Field is OK!
					break;
				default: //Unknown option?
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_TypeField)) //NAK it!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					if (!packetServerAddPacketBufferQueue(&pppRejectFields, common_OptionLengthField)) //Correct length!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
				performskipdata_ipcpnakreject:
					if (common_OptionLengthField >= 2) //Enough length to skip?
					{
						skipdatacounter = common_OptionLengthField - 2; //How much to skip!
						for (; skipdatacounter;) //Skip it!
						{
							if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Failed to consume properly?
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							if (!packetServerAddPacketBufferQueue(&pppRejectFields, datab)) //Correct data!
							{
								goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
							}
							--skipdatacounter;
						}
					}
					else //Malformed parameter!
					{
						goto ppp_finishpacketbufferqueue_ipcp; //Incorrect packet: discard it!
					}
					break;
				}
			}
			if ((pppNakFields.length == 0) && (pppRejectFields.length == 0)) //OK to process?
			{
				if (request_NakRejectipaddress && (common_CodeField == 4)) //Reject-IP-address?
				{
					connectedclient->ppp_serverIPCP_haveipaddress = 0; //Don't request anymore!
				}
				else if (request_NakRejectipaddress) //IPaddress change requested?
				{
					memcpy(&connectedclient->ppp_serverIPCP_pendingipaddress, &ipcp_pendingipaddress, sizeof(connectedclient->ppp_serverIPCP_pendingipaddress)); //The request node number to use!
					connectedclient->ppp_serverIPCP_haveipaddress = 1; //Request now!
				}
				connectedclient->ppp_serverIPCPstatus = 3; //Reset the status check to try again afterwards if it's reset again!
			}
			result = 1; //Success!
			goto ppp_finishpacketbufferqueue2_ipcp; //Finish up!
			break;
		case 6: //Terminate-Ack (Acnowledge termination of connection)
			//Why would we need to handle this if the client can't have it's connection terminated by us!
			connectedclient->ppp_IPCPstatus[1] = 0; //Close our connection?
			result = 1; //Success!
			goto ppp_finishpacketbufferqueue2_ipcp; //Finish up!
		case 7: //Code-Reject (Code field is rejected because it's unknown)
			//Do anything with this?
			result = 1; //Discard!
			goto ppp_finishpacketbufferqueue2_ipcp; //Finish up!
			break; //Don't handle it!
		default: //Unknown Code field?
			//Send a Code-Reject packet to the client!
			memset(&response, 0, sizeof(response)); //Init the response!
			//Build the PPP header first!
			if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, protocol, 0x07, common_IdentifierField, PPP_streamdataleft(&pppstream_informationfield)))
			{
				goto ppp_finishpacketbufferqueue_ipcp; //Finish up!
			}
			//Now, the rejected packet itself!
			for (; PPP_consumeStream(&pppstream_informationfield, &datab);) //The information field itself follows!
			{
				if (!packetServerAddPacketBufferQueue(&response, datab))
				{
					goto ppp_finishpacketbufferqueue_ipcp;
				}
			}
			//Calculate and add the checksum field!
			if (PPP_addFCS(&response,connectedclient,protocol))
			{
				goto ppp_finishpacketbufferqueue_ipcp;
			}
			break;
		}
		//Packet is fully built. Now send it!
		if (connectedclient->ppp_response.size) //Previous Response still valid?
		{
			goto ppp_finishpacketbufferqueue_ipcp; //Keep pending!
		}
		if (response.buffer) //Any response to give?
		{
			memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
			ppp_responseforuser(connectedclient); //A response is ready!
			memset(&response, 0, sizeof(response)); //Parsed!
		}
		goto ppp_finishpacketbufferqueue2_ipcp; //Success!
	ppp_finishpacketbufferqueue_ipcp: //An error occurred during the response?
		result = 0; //Keep pending until we can properly handle it!
	ppp_finishpacketbufferqueue2_ipcp:
		packetServerFreePacketBufferQueue(&response); //Free the queued response!
		packetServerFreePacketBufferQueue(&pppNakFields); //Free the queued response!
		packetServerFreePacketBufferQueue(&pppRejectFields); //Free the queued response!
		break;
	case 0x2B: //IPX packet?
		SNAP_sendIPXpacket:
		if (IPXCP_OPEN) //Fully authenticated and logged in for sending on the peer?
		{
			//Handle the IPX packet to be sent!
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, PPP_streamdataleft(&pppstream))) //Create a substream for the information field?
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			//Now, pppstream_requestfield contains the packet we're trying to send!

			//Now, construct the ethernet header!
			memcpy(&ppptransmitheader.src, &maclocal, 6); //From us!
			ppptransmitheader.dst[0] = 0xFF;
			ppptransmitheader.dst[1] = 0xFF; 
			ppptransmitheader.dst[2] = 0xFF; 
			ppptransmitheader.dst[3] = 0xFF; 
			ppptransmitheader.dst[4] = 0xFF;
			ppptransmitheader.dst[5] = 0xFF; //To a broadcast!
			ppptransmitheader.type = SDL_SwapBE16(0x8137); //We're an IPX packet!

			packetServerFreePacketBufferQueue(&response); //Clear the response to start filling it!

			for (skipdatacounter = 0; skipdatacounter < 14; ++skipdatacounter)
			{
				if (!packetServerAddPacketBufferQueue(&response, 0)) //Start making room for the header!
				{
					goto ppp_finishpacketbufferqueue; //Keep pending!
				}
			}

			memcpy(&response.buffer[0], &ppptransmitheader.data, sizeof(ppptransmitheader.data)); //The ethernet header!
			//Now, add the entire packet as the content!
			for (; PPP_peekStream(&pppstream_requestfield,&datab);) //Anything left to add?
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Data failed to read?
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
				if (!packetServerAddPacketBufferQueue(&response, datab)) //Start making room for the header!
				{
					goto ppp_finishpacketbufferqueue; //Keep pending!
				}
			}

			//Some post-processing!
			if (response.length >= (30+0xE)) //Full header?
			{
				memcpy(&ipxtransmitheader,&response.buffer[0xE],30); //Load the header!
				if (memcmp(&ipxtransmitheader.SourceNetworkNumber,&ipx_currentnetworknumber,4)==0) //Current network number?
				{
					memcpy(&ipxtransmitheader.SourceNetworkNumber,connectedclient->ipxcp_networknumber[PPP_RECVCONF],4); //Current network number!
				}
				if (memcmp(&ipxtransmitheader.DestinationNetworkNumber,&ipx_currentnetworknumber,4)==0) //Current network number?
				{
					memcpy(&ipxtransmitheader.DestinationNetworkNumber,connectedclient->ipxcp_networknumber[PPP_RECVCONF],4); //Current network number!
				}
				memcpy(&response.buffer[0xE],&ipxtransmitheader,30); //Replace the header!
				//Now, the packet we've stored has become the packet to send!
				if (!sendpkt_pcap(connectedclient,response.buffer, response.length)) //Send the response on the network!
				{
					goto ppp_finishpacketbufferqueue; //Failed to send!
				}
			}
			goto ppp_finishpacketbufferqueue2;
			break;
		}
		goto ppp_invalidprotocol; //Passthrough to invalid protocol!
		//TODO
		//break;
	case 0x21: //IP packet?
		if (IPCP_OPEN) //Fully authenticated and logged in for sending on the peer?
		{
			//Handle the IP packet to be sent!
			if (!createPPPsubstream(&pppstream, &pppstream_requestfield, PPP_streamdataleft(&pppstream))) //Create a substream for the information field?
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			//Now, pppstream_requestfield contains the packet we're trying to send!

			//Now, construct the ethernet header!
			memcpy(&ppptransmitheader.src, &packetserver_sourceMAC, 6); //From us!
			memcpy(&ppptransmitheader.dst, &packetserver_gatewayMAC, 6); //Gateway MAC is the destination!
			ppptransmitheader.type = SDL_SwapBE16(0x0800); //We're an IP packet!

			packetServerFreePacketBufferQueue(&response); //Clear the response to start filling it!

			for (skipdatacounter = 0; skipdatacounter < 14; ++skipdatacounter)
			{
				if (!packetServerAddPacketBufferQueue(&response, 0)) //Start making room for the header!
				{
					goto ppp_finishpacketbufferqueue; //Keep pending!
				}
			}

			memcpy(&response.buffer[0], &ppptransmitheader.data, sizeof(ppptransmitheader.data)); //The ethernet header!
			//Now, add the entire packet as the content!
			for (; PPP_peekStream(&pppstream_requestfield, &datab);) //Anything left to add?
			{
				if (!PPP_consumeStream(&pppstream_requestfield, &datab)) //Data failed to read?
				{
					goto ppp_finishpacketbufferqueue; //Finish up!
				}
				if (!packetServerAddPacketBufferQueue(&response, datab)) //Start making room for the header!
				{
					goto ppp_finishpacketbufferqueue; //Keep pending!
				}
			}

			//Now, the packet we've stored has become the packet to send!
			if (!sendpkt_pcap(connectedclient,response.buffer, response.length)) //Send the response on the network!
			{
				goto ppp_finishpacketbufferqueue; //Keep pending!
			}
			goto ppp_finishpacketbufferqueue2;
		}
	default: //Unknown protocol?
		goto checkotherprotocols; //Special: check for other protocols to have been sent over this link!
		ppp_invalidprotocol: //Invalid protocol used when not fully authenticated or verified?
		if (connectedclient->ppp_LCPstatus[PPP_RECVCONF]) //LCP is Open?
		{
			//Send a Code-Reject packet to the client!
			memset(&response, 0, sizeof(response)); //Init the response!
			//Build the PPP header first!
			if (PPP_addLCPNCPResponseHeader(connectedclient, &response, 1, 0xC021, 0x08, connectedclient->ppp_protocolreject_count, PPP_streamdataleft(&pppstream) + 2))
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			if (!packetServerAddPacketBufferQueueBE16(&response, protocol)) //Rejected Protocol!
			{
				goto ppp_finishpacketbufferqueue; //Finish up!
			}
			//Now, the rejected packet itself!
			for (; PPP_consumeStream(&pppstream, &datab);) //The data field itself follows!
			{
				if (!packetServerAddPacketBufferQueue(&response, datab))
				{
					goto ppp_finishpacketbufferqueue;
				}
			}
			//Calculate and add the checksum field!
			if (PPP_addFCS(&response,connectedclient,protocol))
			{
				goto ppp_finishpacketbufferqueue;
			}
			//Packet is fully built. Now send it!
			if (connectedclient->ppp_response.size) //Previous Response still valid?
			{
				goto ppp_finishpacketbufferqueue; //Keep pending!
			}
			if (response.buffer) //Any response to give?
			{
				memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
				ppp_responseforuser(connectedclient); //A response is ready!
				memset(&response, 0, sizeof(response)); //Parsed!
				//This doesn't affect any state otherwise!
				++connectedclient->ppp_protocolreject_count; //Increase the counter for each packet received incorrectly!
			}
			goto ppp_finishpacketbufferqueue2; //Finish up!
		}
		break;
	}
	return result; //Currently simply discard it!
}

//result: 0 to discard the packet. 1 to keep it pending in this stage until we're ready to send it to the client.
byte PPP_parseReceivedPacketForClient(byte protocol, PacketServer_clientp connectedclient)
{
	ETHERNETHEADER ethernetheader;
	IPXPACKETHEADER ipxheader;
	MODEM_PACKETBUFFER response;
	PPP_Stream pppstream, ipxechostream;
	byte result;
	byte datab;
	word packettype; //What PPP packet type to send!
	result = 0; //Default: discard!
	//This is supposed to check the packet, parse it and send packets to the connected client in response when it's able to!
	if (LCP_NCP) //Fully authenticated and logged in?
	{
		if ((protocol?connectedclient->IPpktlen:connectedclient->pktlen) > sizeof(ethernetheader.data)) //Length might be fine?
		{
			result = 1; //Default: pending!

			//TODO: Receiving IP packets. Ignore for now.
			memcpy(&ethernetheader.data, protocol?connectedclient->IPpacket:connectedclient->packet, sizeof(ethernetheader.data)); //Take a look at the ethernet header!
			if ((ethernetheader.type != SDL_SwapBE16(0x8137)) && (ethernetheader.type != SDL_SwapBE16(0x0800))) //We're not an IPX or IP packet!
			{
				return 0; //Unsupported packet type, discard!
			}
			
			if (ethernetheader.type == SDL_SwapBE16(0x8137)) //IPX packet?
			{
				if (connectedclient->pktlen >= (30 + sizeof(ethernetheader.data))) //Proper IPX packet received?
				{
					memcpy(&ipxheader, &connectedclient->packet[sizeof(ethernetheader.data)], 30); //Get the IPX header from the packet!
					createPPPstream(&ipxechostream, &connectedclient->packet[sizeof(ethernetheader.data) + 30], connectedclient->pktlen - (sizeof(ethernetheader.data) + 30)); //Create a stream out of the possible echo packet!
					createPPPstream(&pppstream, &connectedclient->packet[sizeof(ethernetheader.data) + 18], 12); //Create a stream out of the IPX packet source address!
					if (SDL_SwapBE16(ipxheader.DestinationSocketNumber) == 2) //Echo request?
					{
						if (memcmp(&ipxheader.DestinationNetworkNumber, &connectedclient->ipxcp_networknumber[PPP_RECVCONF], 4) == 0) //Network number match? Don't take the current network for this to prevent conflicts with it.
						{
							//Perform logic for determining what addresses are assigned here.
							if (memcmp(&ipxheader.DestinationNodeNumber, &ipxbroadcastaddr, 6) == 0) //Destination node is the broadcast address?
							{
								//We're replying to the echo packet!
								if (!IPXCP_OPEN) //Not authenticated yet?
								{
									return 0; //Handled, discard!
								}
								if (connectedclient->ipxcp_negotiationstatus == 1) //We're opened and trying to renegotiate our own address?
								{
									return 0; //Discard: don't send any reply because that would mean invalidating our own address during renegotiation!
								}
								//We're authenticated, so send a reply!
								if (sendIPXechoreply(connectedclient, &ipxechostream, &pppstream)) //Sent a reply?
								{
									goto handleIPXreplies; //Handled, discard, but handle raw packeting also!
								}
								else //Couldn't send a reply packet?
								{
									return 1; //Keep pending until we can send a reply!
								}
							}
						}
						handleIPXreplies:
						//Check for echo replies on our requests!
						if ((memcmp(&ipxheader.DestinationNodeNumber, &ipx_servernodeaddr, 6) == 0) && (memcmp(&ipxheader.DestinationNetworkNumber, &ipx_servernetworknumber, 4) == 0)) //Negotiation address and network is being sent to?
						{
							if (connectedclient->ipxcp_negotiationstatus == 1) //Waiting for negotiation answers?
							{
								if (
									(memcmp(&ipxheader.SourceNodeNumber, &connectedclient->ipxcp_nodenumberecho, 6) == 0) &&
									(memcmp(&ipxheader.SourceNetworkNumber, &connectedclient->ipxcp_networknumberecho, 4) == 0)) //The requested node number had been found already?
								{
									connectedclient->ipxcp_negotiationstatus = 2; //NAK the connection, as the requested node number had been found in the network!
								}
							}
						}
					}
					//Filter out unwanted IPX network/node numbers that aren't intended for us!
					if (!IPXCP_OPEN) //Not open yet?
					{
						return 0; //Handled, discard!
					}
					if (connectedclient->ppp_IPXCPstatus[PPP_RECVCONF]==1) //Raw mode? Use unfiltered when not in normal mode!
					{
						if (memcmp(&ipxheader.DestinationNetworkNumber, &connectedclient->ipxcp_networknumber[PPP_RECVCONF][0], 4) != 0) //Network number mismatch?
						{
							if ((memcmp(&ipxheader.DestinationNetworkNumber, &ipx_currentnetworknumber, 4) != 0) && (memcmp(&ipxheader.DestinationNetworkNumber, &ipx_broadcastnetworknumber, 4) != 0)) //Current and Broadcast network mismatch?
							{
								return 0; //Handled, discard!
							}
						}
						if (memcmp(&ipxheader.DestinationNodeNumber, &connectedclient->ipxcp_nodenumber[0][0], 6) != 0) //Node number mismatch?
						{
							if (memcmp(&ipxheader.DestinationNodeNumber, &ipxbroadcastaddr, 6) != 0) //Also not a broadcast?
							{
								return 0; //Handled, discard!
							}
						}
					}
				}
				else //Wrong length?
				{
					if (!IPXCP_OPEN) //Not open yet?
					{
						return 0; //Handled, discard!
					}
					if (connectedclient->ppp_IPXCPstatus[PPP_SENDCONF] <= 1) //Normal receiver or not receiving?
					{
						return 0; //Handled, discard!
					}
					//Otherwise, it's either a raw or valid IPX packet!
				}
				if (connectedclient->ppp_suppressIPX) //IPX suppressed?
				{
					return 0; //Suppressed type!
				}
				packettype = 0x2B; //IPX packet for PPP!
			}
			else if (ethernetheader.type == SDL_SwapBE16(0x0800)) //IP packet?
			{
				if (!IPCP_OPEN) //Not open yet?
				{
					return 0; //Handled, discard!
				}
				if (connectedclient->IPpktlen < (sizeof(ethernetheader.data) + 16 + 4)) //Not enough length?
				{
					return 0; //Incorrect packet: discard!
				}
				if ((memcmp(&connectedclient->IPpacket[sizeof(ethernetheader.data) + 16], &connectedclient->ipcp_ipaddress[PPP_RECVCONF], 4) != 0) && (memcmp(&connectedclient->IPpacket[sizeof(ethernetheader.data) + 16], &packetserver_broadcastIP, 4) != 0)) //Static IP mismatch?
				{
					return 0; //Invalid packet!
				}
				if (connectedclient->ppp_suppressIP) //IP suppressed?
				{
					return 0; //Suppressed type!
				}
				packettype = 0x21; //IP packet for PPP!
			}
			else //Unknown packet type?
			{
				return 0; //Don't handle it, discard it!
			}

			//PPP phase of handling the packet has been reached! This packet is meant to be received by the connected client!

			if (connectedclient->ppp_response.size) //Already receiving something?
			{
				return 1; //Keep pending until we can receive it!
			}

			//Form the PPP packet to receive for the client!
			memset(&response, 0, sizeof(response)); //Init the response!
			//Build the PPP header first!
			if (PPP_addPPPheader(connectedclient, &response, 1, packettype)) //Sending the packet type header to the client!
			{
				goto ppp_finishpacketbufferqueue_ppprecv;
			}
			createPPPstream(&pppstream, (ethernetheader.type == SDL_SwapBE16(0x0800))?&connectedclient->IPpacket[sizeof(ethernetheader.data)]:&connectedclient->packet[sizeof(ethernetheader.data)], ((ethernetheader.type == SDL_SwapBE16(0x0800))?connectedclient->IPpktlen:connectedclient->pktlen) - sizeof(ethernetheader.data)); //Create a stream out of the packet!
			//Now, the received packet itself!
			for (; PPP_consumeStream(&pppstream, &datab);) //The information field itself follows!
			{
				if (!packetServerAddPacketBufferQueue(&response, datab))
				{
					goto ppp_finishpacketbufferqueue_ppprecv;
				}
			}
			//Calculate and add the checksum field!
			if (PPP_addFCS(&response,connectedclient,packettype))
			{
				goto ppp_finishpacketbufferqueue_ppprecv;
			}
			//Packet is fully built. Now send it!
			//Buffer being available is already checked before forming the response!
			if (response.buffer) //Any response to give?
			{
				memcpy(&connectedclient->ppp_response, &response, sizeof(response)); //Give the response to the client!
				ppp_responseforuser(connectedclient); //A response is ready!
				memset(&response, 0, sizeof(response)); //Parsed!
			}
			result = 0; //Success!
			goto ppp_finishcorrectpacketbufferqueue2_ppprecv; //Success!
		ppp_finishpacketbufferqueue_ppprecv: //An error occurred during the response?
			result = 1; //Keep pending until we can properly handle it!
		ppp_finishcorrectpacketbufferqueue2_ppprecv: //Correctly finished!
			packetServerFreePacketBufferQueue(&response); //Free the queued response!
			return result; //Give the result!
		}
	}
	return 0; //Currently simply discard it!
}

void connectModem(char* number)
{
	if (modem_connect(number))
	{
		modem_responseResult(MODEMRESULT_CONNECT); //Accept!
		modem.offhook = 2; //On-hook(connect)!
		//Not to remain in command mode?
		if (modem.supported<2) //Normal mode?
		{
			modem.datamode = 2; //Enter data mode pending!
		}
		else
		{
			modem.datamode = 1; //Enter data mode!
		}
	}
}

byte modem_connected()
{
	return (modem.connected == 1); //Are we connected or not!
}

byte modem_passthrough()
{
	return (modem.supported >= 2); //In phassthough mode?
}

word performUDPchecksum(MODEM_PACKETBUFFER* buffer)
{
	word result;
	uint_32 r;
	uint_32 len;
	word* p;
	r = 0;
	len = buffer->length;
	p = (word*)buffer->buffer; //The data to check!
	for (; len > 1;) //Words left?
	{
		r += *p++; //Read and add!
		len -= 2; //Parsed!
	}
	if (len) //Left?
	{
		r += *((byte*)p); //Read byte left!
	}
	for (; r >> 16;) //Left to wrap?
	{
		r = (r & 0xFFFF) + (r >> 16); //Wrap!
	}
	result = ~r; //One's complement of the result is the result!
	if (result == 0) //0 needs to become FFFF?
	{
		result = 0xFFFF; //Special case!
	}
	return result; //Give the result!
}

//checkreceivechecksum: 0 for calculating the checksum for sending. 1 for performing the checksum (a resulting checksum value of 0 means that the checksum is correct).
word performIPv4checksum(MODEM_PACKETBUFFER* buffer, byte checkreceivechecksum)
{
	uint_32 r;
	uint_32 len;
	uint_32 pos;
	word* p;
	r = 0;
	len = buffer->length;
	p = (word*)buffer->buffer; //The data to check!
	pos = 0; //Init position!
	for (; len > 1;) //Words left?
	{
		if ((pos != 5) || checkreceivechecksum) //Not the checksum field or not including the checksum for sending(for validating it)?
		{
			r += *p++; //Read and add!
		}
		else
		{
			++p; //Add only, ignore the data!
		}
		++pos; //Next position!
		len -= 2; //Parsed!
	}
	//odd amount of bytes shouldn't happen!
	for (; r >> 16;) //Left to wrap?
	{
		r = (r & 0xFFFF) + (r >> 16); //Wrap!
	}
	return ~r; //One's complement of the result is the result!
}

//UDP checksum like IPv4 checksum above, but taking the proper inputs to perform the checksum! When checksum is non-zero, this must match! Otherwise, no checksum is used!
byte doUDP_checksum(byte* ih, byte *udp_header, byte *UDP_data, word UDP_datalength, word *checksum)
{
	word result;
	word dataleft;
	IPv4header curih;
	UDPpseudoheadercontainer uph;
	MODEM_PACKETBUFFER buffer; //For the data to checksum!
	memcpy(&curih, ih, sizeof(curih)); //Make a copy of the header to read!
	memset(&uph, 0x00, sizeof(uph));
	memcpy(&uph.header.srcaddr,&curih.sourceaddr,4); //Source address!
	memcpy(&uph.header.dstaddr,&curih.destaddr,4); //Destination address!
	uph.header.mustbezero = 0x00;
	uph.header.protocol = curih.protocol;
	uph.header.UDPlength = SDL_SwapBE16(8+UDP_datalength); //UDP header + UDP data size

	memset(&buffer, 0, sizeof(buffer)); //Init checksum buffer!
	//Pseudo header first!
	for (dataleft = 0; dataleft < sizeof(uph.data); ++dataleft)
	{
		if (!packetServerAddPacketBufferQueue(&buffer, uph.data[dataleft])) //Add the data to checksum!
		{
			packetServerFreePacketBufferQueue(&buffer); //Clean up!
			return 0; //Failure!
		}
	}
	//Followed by UDP header!
	for (dataleft = 0; dataleft < 8; ++dataleft)
	{
		if ((dataleft & ~1) == 6) //Word at position 6 is the checksum, skip it!
		{
			if (!packetServerAddPacketBufferQueue(&buffer, 0)) //Add the data to checksum, treating it as if it isn't set!
			{
				packetServerFreePacketBufferQueue(&buffer); //Clean up!
				return 0; //Failure!
			}
		}
		else if (!packetServerAddPacketBufferQueue(&buffer, udp_header[dataleft])) //Add the data to checksum!
		{
			packetServerFreePacketBufferQueue(&buffer); //Clean up!
			return 0; //Failure!
		}
	}
	//Followed by UDP data!
	for (dataleft = 0; dataleft < UDP_datalength; ++dataleft)
	{
		if (!packetServerAddPacketBufferQueue(&buffer, UDP_data[dataleft])) //Add the data to checksum!
		{
			packetServerFreePacketBufferQueue(&buffer); //Clean up!
			return 0; //Failure!
		}
	}
	result = performUDPchecksum(&buffer); //Perform the checksum!
	packetServerFreePacketBufferQueue(&buffer); //Clean up!
	*checksum = result; //The checksum!
	return 1; //Success!
}

//UDP checksum like IPv4 checksum above, but taking the proper inputs to perform the checksum!
//checkreceivechecksum: 0 for calculating the checksum for sending. 1 for performing the checksum (a resulting checksum value of 0 means that the checksum is correct).
byte doIPv4_checksum(byte* ih, word headerlength, byte checkreceivechecksum, word *checksum)
{
	word result;
	word dataleft;
	MODEM_PACKETBUFFER buffer; //For the data to checksum!
	memset(&buffer, 0, sizeof(buffer)); //Init!

	for (dataleft = 0; dataleft < headerlength; ++dataleft)
	{
		if (!packetServerAddPacketBufferQueue(&buffer, ih[dataleft])) //Add the data to checksum!
		{
			packetServerFreePacketBufferQueue(&buffer); //Clean up!
			return 0; //Failure!
		}
	}
	result = performIPv4checksum(&buffer, checkreceivechecksum); //Perform the checksum!
	packetServerFreePacketBufferQueue(&buffer); //Clean up!
	*checksum = result; //The checksum!
	return 1; //Success!
}

//Retrieves a 8-byte UDP header from data!
byte getUDPheader(byte* IPv4_header, byte *UDPheader_data, UDPheader* UDP_header, byte dataleft, byte performChecksumForReceive)
{
	word checksum;
	if (dataleft < 8) //Not enough left for a header?
	{
		return 0; //Failed: invalid header!
	}
	memcpy(UDP_header, UDPheader_data, 8); //Set the header directly from the data!
	if ((SDL_SwapBE16(UDP_header->length) + 8) < dataleft) //Not enough room left for the data?
	{
		return 0; //Failed to perform the checksum: this would overflow the buffer!
	}
	if (performChecksumForReceive) //Performing a checksum on it to validate it?
	{
		if (UDP_header->checksum) //Zero is no checksum present!
		{
			if (!doUDP_checksum(IPv4_header, UDPheader_data, UDPheader_data + 8, SDL_SwapBE16(UDP_header->length), &checksum))
			{
				return 0; //Failed to perform the checksum!
			}
			if (checksum != SDL_SwapBE16(UDP_header->checksum)) //Checksum failed?
			{
				return 0; //Failed: Checksum failed!
			}
		}
	}
	return 1; //OK: UDP header and data is OK!
}

//getIPv4header: Retrieves a IPv4 header from a packet and gives it's size. Can also perform checksum check on the input data.
//Retrieves a n-byte IPv4 header from data!
byte getIPv4header(byte* data, IPv4header* IPv4_header, word dataleft, byte performChecksumForReceive, word *result_headerlength)
{
	word checksum;
	word currentheaderlength;
	if (dataleft < 20) //Not enough left for a minimal header?
	{
		return 0; //Failed: invalid header!
	}
	memcpy(IPv4_header, data, 20); //Set the header directly from the data!
	currentheaderlength = ((IPv4_header->version_IHL & 0xF) << 2); //Header length, in doublewords!
	if (dataleft < currentheaderlength) //Not enough data for the full header?
	{
		return 0; //Failed: invalid header!
	}
	if (performChecksumForReceive) //Performing a checksum on it to validate it?
	{
		if (!doIPv4_checksum(data, currentheaderlength, 1, &checksum)) //Failed checksum?
		{
			return 0; //Failed: couldn't validate checksum!
		}
		if (checksum) //Checksum failed?
		{
			return 0; //Failed: checksum failed!
		}
	}
	*result_headerlength = currentheaderlength; //The detected header length!
	return 1; //Gotten packet!
}

/*
* setIPv4headerChecksum: Sets and updates the IPv4 header in the packet with checksum.
* data: points to IPv4 header in the packet!
* IPv4_header: the header to set.
*/
byte setIPv4headerChecksum(byte* data, IPv4header* IPv4_header)
{
	word checksum;
	word headerlength;
	headerlength = ((IPv4_header->version_IHL & 0xF) << 2); //Header length, in doublewords!
	memcpy(data, IPv4_header, 20); //Update the IP packet as requested!
	if (!doIPv4_checksum(data, headerlength, 0, &checksum)) //Failed checksum creation?
	{
		return 0; //Failed: couldn't validate checksum!
	}
	IPv4_header->headerchecksum = SDL_SwapBE16(checksum); //Set or update the checksum as requested!
	memcpy(data, IPv4_header, 20); //Update the IP header as requested!
	return 1; //Gotten header and updated!
}

/*
* setUDPheaderChecksum: Sets and updates the UDP header in the packet with checksum.
* ipheader: the IP header in the packet
* udp_header_data: the UDP header in the packet
* udpheader: the UDP header to set in the packet
* UDP_data: the start of UDP data in the packet
* UDP_datalength: the length of UDP data in the packet
*/
byte setUDPheaderChecksum(byte* ipheader, byte* udp_header_data, UDPheader *udpheader, byte* UDP_data, word UDP_datalength)
{
	word checksum;
	memcpy(udp_header_data, udpheader, 8); //Set the header directly from the data!
	if (!doUDP_checksum(ipheader,udp_header_data, UDP_data, UDP_datalength, &checksum)) //Failed checksum?
	{
		return 0; //Failed: couldn't validate checksum!
	}
	udpheader->checksum = SDL_SwapBE16(checksum); //Set or update the checksum as requested!
	memcpy(udp_header_data, udpheader, 8); //Update the UDP header as requested!
	return 1; //Gotten header and updated!
}

void packetserver_initStartPPP(PacketServer_clientp connectedclient, byte autodetected)
{
	connectedclient->packetserver_delay = (DOUBLE)0; //Finish the delay!
	PacketServer_startNextStage(connectedclient, PACKETSTAGE_PACKETS); //Start the SLIP service!
	connectedclient->packetserver_slipprotocol = 3;
	connectedclient->packetserver_slipprotocol_pppoe = 0; //PPP?
	connectedclient->PPP_MRU[0] = connectedclient->PPP_MRU[1] = 1500; //Default: 1500
	connectedclient->PPP_headercompressed[0] = connectedclient->PPP_headercompressed[1] = 0; //Default: uncompressed
	connectedclient->PPP_protocolcompressed[0] = connectedclient->PPP_protocolcompressed[1] = 0; //Default: uncompressed
	connectedclient->ppp_protocolreject_count = 0; //Default: 0!
	connectedclient->ppp_serverLCPstatus = 0; //Start out with initialized PPP LCP connection for the server to client connection!
	connectedclient->ppp_serverPAPstatus = 0; //Start out with initialized PPP PAP connection for the server to client connection!
	connectedclient->ppp_serverIPXCPstatus = 0; //Start out with initialized PPP IPXCP connection for the server to client connection!
	connectedclient->ppp_serverIPCPstatus = 0; //Start out with initialized PPP IPCP connection for the server to client connection!
	connectedclient->ppp_serverLCPrequesttimer = (DOUBLE)0.0f; //Restart timing!
	connectedclient->ppp_serverPAPrequesttimer = (DOUBLE)0.0f; //Restart timing!
	connectedclient->ppp_serverIPXCPrequesttimer = (DOUBLE)0.0f; //Restart timing!
	connectedclient->ppp_serverIPCPrequesttimer = (DOUBLE)0.0f; //Restart timing!
	connectedclient->ipxcp_negotiationstatustimer = (DOUBLE)0.0f; //Restart timing!
	connectedclient->ppp_LCPstatus[0] = connectedclient->ppp_PAPstatus[0] = connectedclient->ppp_IPXCPstatus[0] = connectedclient->ppp_IPCPstatus[0] = 0; //Reset all protocols to init state!
	connectedclient->ppp_LCPstatus[1] = connectedclient->ppp_PAPstatus[1] = connectedclient->ppp_IPXCPstatus[1] = connectedclient->ppp_IPCPstatus[1] = 0; //Reset all protocols to init state!
	connectedclient->asynccontrolcharactermap[0] = connectedclient->asynccontrolcharactermap[1] = 0xFFFFFFFF; //Initialize the Async Control Character Map to init value!
	packetServerFreePacketBufferQueue(&connectedclient->ppp_response); //Free the response that's queued for packets to be sent to the client if anything is left!
	connectedclient->ppp_sendframing = 0; //Init: no sending active framing yet!
	connectedclient->PPP_packetstartsent = 0; //Init: no packet start has been sent yet!
}

void updateModem(DOUBLE timepassed) //Sound tick. Executes every instruction.
{
	byte *LCPbuf;
	byte handledreceived; //Receiving status for the current client packet type!
	byte **pktsrc; //source of the packet to receive!
	uint16_t *pktlen; //length of the packet to receive
	byte pkttype; //type of the packet we're trying to receive
	uint_32 ppp_transmitasynccontrolcharactermap;
	ARPpackettype ARPpacket, ARPresponse; //ARP packet to send/receive!
	PacketServer_clientp connectedclient, tempclient;
	sword connectionid;
	byte datatotransmit;
	ETHERNETHEADER ethernetheader, ppptransmitheader;
	memset(&ppptransmitheader, 0, sizeof(ppptransmitheader));
	word headertype; //What header type are we?
	uint_32 currentpos;
	modem.timer += timepassed; //Add time to the timer!
	if (modem.escaping) //Escapes buffered and escaping?
	{
		if (modem.timer>=modem.escapecodeguardtime) //Long delay time?
		{
			if (modem.escaping==3) //3 escapes?
			{
				modem.escaping = 0; //Stop escaping!
				modem.datamode = 0; //Return to command mode!
				modem.ATcommandsize = 0; //Start a new command!
				modem_responseResult(MODEMRESULT_OK); //OK message to escape!
			}
			else //Not 3 escapes buffered to be sent?
			{
				for (;modem.escaping;) //Send the escaped data after all!
				{
					--modem.escaping;
					modem_writeCommandData(modem.escapecharacter); //Send the escaped data!
				}
			}
		}
	}

	if (modem.wascommandcompletionechoTimeout) //Timer running?
	{
		modem.wascommandcompletionechoTimeout -= timepassed;
		if (modem.wascommandcompletionechoTimeout <= (DOUBLE)0.0f) //Expired?
		{
			modem.wascommandcompletionecho = 0; //Disable the linefeed echo!
			modem.wascommandcompletionechoTimeout = (DOUBLE)0; //Stop the timeout!
			modem_flushCommandCompletion(); //Execute the command immediately!
		}
	}

	if (modem.detectiontimer[0]) //Timer running?
	{
		modem.detectiontimer[0] -= timepassed;
		if (modem.detectiontimer[0]<=(DOUBLE)0.0f) //Expired?
			modem.detectiontimer[0] = (DOUBLE)0; //Stop timer!
	}
	if (modem.detectiontimer[1]) //Timer running?
	{
		modem.detectiontimer[1] -= timepassed;
		if (modem.detectiontimer[1]<=(DOUBLE)0.0f) //Expired?
			modem.detectiontimer[1] = (DOUBLE)0; //Stop timer!
	}
	if (modem.RTSlineDelay) //Timer running?
	{
		modem.RTSlineDelay -= timepassed;
	}
	if (modem.DTRlineDelay) //Timer running?
	{
		modem.DTRlineDelay -= timepassed;
	}
	if (modem.RTSlineDelay && modem.DTRlineDelay) //Both timing?
	{
		if ((modem.RTSlineDelay<=(DOUBLE)0.0f) && (modem.DTRlineDelay<=(DOUBLE)0.0f)) //Both expired?
		{
			modem.RTSlineDelay = (DOUBLE)0; //Stop timer!
			modem.DTRlineDelay = (DOUBLE)0; //Stop timer!
			modem_updatelines(3); //Update both lines at the same time!
		}
	}
	if (modem.RTSlineDelay) //Timer running?
	{
		if (modem.RTSlineDelay<=(DOUBLE)0.0f) //Expired?
		{
			modem.RTSlineDelay = (DOUBLE)0; //Stop timer!
			modem_updatelines(2); //Update line!
		}
	}
	if (modem.DTRlineDelay) //Timer running?
	{
		if (modem.DTRlineDelay<=(DOUBLE)0.0f) //Expired?
		{
			modem.DTRlineDelay = (DOUBLE)0; //Stop timer!
			modem_updatelines(1); //Update line!
		}
	}

	if ((modem.supported >= 3) && (modem.passthroughlinestatusdirty & 7)) //Dirty lines to handle in passthrough mode?
	{
		if (fifobuffer_freesize(modem.blockoutputbuffer[0])==fifobuffer_size(modem.blockoutputbuffer[0])) //Enough to send a packet to describe our status change?
		{
			//Send a break(bit 2)/DTR(bit 1)/RTS(bit 0) packet!
			writefifobuffer(modem.blockoutputbuffer[0], 0xFF); //Escape!
			writefifobuffer(modem.blockoutputbuffer[0], ((modem.outputline & 1) << 1) | ((modem.outputline & 2) >> 1) | ((modem.outputline & 0x20) >> 3)); //Send DTR, RTS and Break!
			modem.passthroughlinestatusdirty &= ~7; //Acknowledge the new lines!
		}
	}

	modem.serverpolltimer += timepassed;
	if ((modem.serverpolltimer>=modem.serverpolltick) && modem.serverpolltick) //To poll?
	{
		modem.serverpolltimer = fmod(modem.serverpolltimer,modem.serverpolltick); //Polling once every turn!
		if (!(((((modem.linechanges & 1) == 0) && (modem.supported<2)) || ((modem.supported>=2) && ((modem.connected==1) || (modem.ringing)))) && (PacketServer_running == 0))) //Able to accept? Never accept in passthrough mode!
		{
			if ((connectionid = acceptTCPServer()) >= 0) //Are we connected to?
			{
				if (PacketServer_running) //Packet server is running?
				{
					connectedclient = allocPacketserver_client(); //Try to allocate!
					if (connectedclient) //Allocated?
					{
						connectedclient->connectionid = connectionid; //We're connected like this!
						modem.connected = 2; //Connect as packet server instead, we start answering manually instead of the emulated modem!
						modem.ringing = 0; //Never ring!
						initPacketServer(connectedclient); //Initialize the packet server to be used!
						unlock(LOCK_PCAP); //Unlock PCAP which was locked by the allocation function!
					}
					else //Failed to allocate?
					{
						unlock(LOCK_PCAP); //Finished using it!
						TCP_DisconnectClientServer(connectionid); //Try and disconnect, if possible!
					}
				}
				else if (connectionid == 0) //Normal behaviour: start ringing!
				{
					modem.connectionid = connectionid; //We're connected like this!
					modem.ringing = 1; //We start ringing!
					modem.registers[1] = 0; //Reset ring counter!
					modem.ringtimer = timepassed; //Automatic time timer, start immediately!
					if ((modem.supported >= 2) && (PacketServer_running == 0)) //Passthrough mode accepted without packet server?
					{
						TCPServer_Unavailable(); //We're unavailable to connect to from now on!
					}
				}
				else //Invalid ID to handle right now(single host only atm)?
				{
					TCP_DisconnectClientServer(connectionid); //Try and disconnect, if possible!
				}
			}
		}
		else //We can't be connected to, stop the server if so!
		{
			TCPServer_Unavailable(); //We're unavailable to connect to!
			if (modem.supported < 2) //Not in passthrough mode? Disconnect any if connected!
			{
				if ((modem.connected == 1) || modem.ringing) //We're connected as a modem?
				{
					TCP_DisconnectClientServer(modem.connectionid);
					modem.connectionid = -1; //Not connected anymore!
					fifobuffer_clear(modem.inputdatabuffer[0]); //Clear the output buffer for the next client!
					fifobuffer_clear(modem.outputbuffer[0]); //Clear the output buffer for the next client!
					fifobuffer_clear(modem.blockoutputbuffer[0]); //Clear the output buffer for the next client!
				}
			}
		}
	}

	if (modem.ringing) //Are we ringing?
	{
		modem.ringtimer -= timepassed; //Time!
		if (modem.ringtimer<=0.0) //Timed out?
		{
			if (modem.ringing & 2) //Ring completed?
			{
				++modem.registers[1]; //Increase numbr of rings!
				if (((modem.registers[0] > 0) && (modem.registers[1] >= modem.registers[0])) || (modem.supported>=2)) //Autoanswer or passthrough mode?
				{
					handleModemAutoAnswer:
					modem.registers[1] = 0; //When connected, clear the register!
					if (modem_connect(NULL)) //Accept incoming call?
					{
						modem_Answered(); //We've answered!
						return; //Abort: not ringing anymore!
					}
				}
				//Wait for the next ring to start!
				modem.ringing &= ~2; //Wait to start a new ring!
				#ifdef IS_LONGDOUBLE
					modem.ringtimer += 3000000000.0L; //3s timer for every ring!
				#else
					modem.ringtimer += 3000000000.0; //3s timer for every ring!
				#endif
			}
			else //Starting a ring?
			{
				if (modem.supported < 2) //Not passthrough mode?
				{
					modem_responseResult(MODEMRESULT_RING); //We're ringing!
					#ifdef IS_LONGDOUBLE
						modem.ringtimer += 3000000000.0L; //3s timer for every ring!
					#else
						modem.ringtimer += 3000000000.0; //3s timer for every ring!
					#endif
				}
				else //Silent autoanswer mode?
				{
					modem.ringing |= 2; //Wait to start a new ring!
					goto handleModemAutoAnswer; //Autoanswer immediately!
				}
				//Wait for the next ring to start!
				modem.ringing |= 2; //Wait to start a new ring!
			}
		}
	}



	modem.networkdatatimer += timepassed;
	if ((modem.networkdatatimer>=modem.networkpolltick) && modem.networkpolltick) //To poll?
	{
		for (;modem.networkdatatimer>=modem.networkpolltick;) //While polling!
		{
			modem.networkdatatimer -= modem.networkpolltick; //Timing this byte by byte!
			if (modem.connected || modem.ringing) //Are we connected?
			{
				if (modem.connected == 2) //Running the packet server?
				{
					//First, a receiver loop!

					lock(LOCK_PCAP); //Lock for pcap!
					if (net.packet) //Anything to receive?
					{
						//Move the packet to all available clients!
						for (connectedclient = Packetserver_allocatedclients; connectedclient; connectedclient = connectedclient->next) //Check all connected clients!
						{
							if ((connectedclient->packet == NULL) && (!connectedclient->packet)) //Ready to receive?
							{
								connectedclient->packet = zalloc(net.pktlen, "SERVER_PACKET", NULL); //Allocate a packet to receive!
								if (connectedclient->packet) //Allocated?
								{
									connectedclient->pktlen = net.pktlen; //Save the length of the packet!
									memcpy(connectedclient->packet, net.packet, net.pktlen); //Copy the packet to the active buffer!
								}
								if (!connectedclient->packetserver_slipprotocol_pppoe && (connectedclient->packetserver_slipprotocol == 3)) //Not suitable for consumption by the client yet?
								{
									//This is handled by the protocol itself! It has it's own packet handling code!
								}
								else //Packet ready for sending to the client!
								{
									connectedclient->PPP_packetreadyforsending = 1; //Ready to send to client always!
									connectedclient->PPP_packetpendingforsending = 0; //Not pending for sending by default!
								}
							}
						}
						//We've received the packets on all clients, allow the next one to arrive!
						freez((void**)&net.packet, net.pktlen, "MODEM_PACKET");
						net.packet = NULL; //Discard if failed to deallocate!
						net.pktlen = 0; //Not allocated!
					}
					if (IPnet.packet) //Anything to receive?
					{
						//Move the packet to all available clients!
						for (connectedclient = Packetserver_allocatedclients; connectedclient; connectedclient = connectedclient->next) //Check all connected clients!
						{
							if ((connectedclient->IPpacket == NULL) && (!connectedclient->IPpacket)) //Ready to receive?
							{
								connectedclient->IPpacket = zalloc(IPnet.pktlen, "SERVER_PACKET", NULL); //Allocate a packet to receive!
								if (connectedclient->IPpacket) //Allocated?
								{
									connectedclient->IPpktlen = IPnet.pktlen; //Save the length of the packet!
									memcpy(connectedclient->IPpacket, IPnet.packet, IPnet.pktlen); //Copy the packet to the active buffer!
								}
								if (!connectedclient->packetserver_slipprotocol_pppoe && (connectedclient->packetserver_slipprotocol == 3)) //Not suitable for consumption by the client yet?
								{
									//This is handled by the protocol itself! It has it's own packet handling code!
								}
								else //Packet ready for sending to the client!
								{
									connectedclient->PPP_packetreadyforsending = 1; //Ready to send to client always!
									connectedclient->PPP_packetpendingforsending = 0; //Not pending for sending by default!
								}
							}
						}
						//We've received the packets on all clients, allow the next one to arrive!
						freez((void**)&IPnet.packet, IPnet.pktlen, "MODEM_PACKET");
						IPnet.packet = NULL; //Discard if failed to deallocate!
						IPnet.pktlen = 0; //Not allocated!
					}
					unlock(LOCK_PCAP);
					for (connectedclient = Packetserver_allocatedclients; connectedclient; connectedclient = connectedclient->next) //Check all connected clients!
					{
						if (connectedclient->used == 0) continue; //Skip unused clients!
						pkttype = connectedclient->roundrobinpackettype; //First type to receive: generic packet!
						handledreceived = 0; //Default: NOP!
						retrypkttype:
						if (pkttype) //IPv4?
						{
							pktsrc = &connectedclient->IPpacket; //Packet to receive
							pktlen = &connectedclient->IPpktlen; //Packet length to receive
						}
						else //Other?
						{
							pktsrc = &connectedclient->packet; //Packet to receive
							pktlen = &connectedclient->pktlen; //Packet length to receive
						}
						//Handle packet server packet data transfers into the inputdatabuffer/outputbuffer to the network!
						if (modem.blockoutputbuffer[connectedclient->connectionnumber]) //Properly allocated?
						{
							if ((*pktsrc) || ((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe))) //Packet has been received or processing? Try to start transmit it!
							{
								if (fifobuffer_freesize(modem.blockoutputbuffer[connectedclient->connectionnumber]) == fifobuffer_size(modem.blockoutputbuffer[connectedclient->connectionnumber])) //Valid to produce more data?
								{
									if ((((connectedclient->packetserver_packetpos == 0) && (connectedclient->packetserver_packetack == 0)) || ((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe))) && ((*pktsrc))) //New packet?
									{
										if (*pktlen >= (sizeof(ethernetheader.data) + ((connectedclient->packetserver_slipprotocol!=3)?20:(connectedclient->packetserver_slipprotocol_pppoe?7:1)))) //Length OK(at least one byte of data and complete IP header) or the PPP packet size (7 extra bytes for PPPOE, 1 byte minimal for PPP)?
										{
											memcpy(&ethernetheader.data, (*pktsrc), sizeof(ethernetheader.data)); //Copy to the client buffer for inspection!
											//Next, check for supported packet types!
											if (connectedclient->packetserver_slipprotocol == 3) //PPP protocol used?
											{
												if (ethernetheader.type == SDL_SwapBE16(0x8863)) //Are we a discovery packet?
												{
													if (connectedclient->packetserver_slipprotocol_pppoe) //Using PPPOE?
													{
														if (PPPOE_handlePADreceived(connectedclient)) //Handle the received PAD packet!
														{
															//Discard the received packet, so nobody else handles it too!
															goto invalidpacket; //Invalid packet!
														}
													}
													//Using PPP, ignore the header type and parse this later!
												}
											}
											headertype = ethernetheader.type; //The requested header type!
											//Now, check the normal receive parameters!
											if (connectedclient->packetserver_useStaticIP && (headertype == SDL_SwapBE16(0x0800)) && (((connectedclient->packetserver_slipprotocol == 1)) || ((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe) && (connectedclient->ppp_IPCPstatus[PPP_RECVCONF])))) //IP filter to apply for IPv4 connections and PPPOE connections?
											{
												if ((memcmp(&(*pktsrc)[sizeof(ethernetheader.data) + 16], &connectedclient->packetserver_staticIP, 4) != 0) && (memcmp(&(*pktsrc)[sizeof(ethernetheader.data) + 16], &packetserver_broadcastIP, 4) != 0)) //Static IP mismatch?
												{
													goto invalidpacket; //Invalid packet!
												}
											}
											if ((memcmp(&ethernetheader.dst, &packetserver_sourceMAC, sizeof(ethernetheader.dst)) != 0) && (memcmp(&ethernetheader.dst, &packetserver_broadcastMAC, sizeof(ethernetheader.dst)) != 0)) //Invalid destination(and not broadcasting)?
											{
												goto invalidpacket; //Invalid packet!
											}
											if (connectedclient->packetserver_slipprotocol == 3) //PPP protocol used?
											{
												if (ethernetheader.type == SDL_SwapBE16(0x8863)) //Are we a discovery packet?
												{
													if (connectedclient->packetserver_slipprotocol_pppoe) //PPPOE?
													{
														if (PPPOE_handlePADreceived(connectedclient)) //Handle the received PAD packet!
														{
															//Discard the received packet, so nobody else handles it too!
															goto invalidpacket; //Invalid packet!
														}
														//Otherwise, keep pending?
													}
													else
													{
														goto invalidpacket; //Invalid for us!
													}
												}
												else if ((headertype != SDL_SwapBE16(0x8864)) && connectedclient->packetserver_slipprotocol_pppoe) //Receiving uses normal PPP packets to transfer/receive on the receiver line only!
												{
													goto invalidpacket; //Invalid for us!
												}
												else if (headertype == SDL_SwapBE16(0x8864)) //Invalid for PPP?
												{
													goto invalidpacket; //Invalid for us!
												}
												if (!connectedclient->packetserver_slipprotocol_pppoe) //PPP requires extra filtering?
												{
													if (headertype == SDL_SwapBE16(0x0800)) //IPv4?
													{
														if (!connectedclient->ppp_IPCPstatus[PPP_RECVCONF]) //IPv4 not used on PPP?
														{
															goto invalidpacket; //Invalid for us!
														}
													}
													else if (headertype == SDL_SwapBE16(0x0806)) //ARP?
													{
														if (!connectedclient->ppp_IPCPstatus[PPP_RECVCONF]) //IPv4 not used on PPP?
														{
															goto invalidpacket; //Invalid for us!
														}
													}
													else if (headertype == SDL_SwapBE16(0x8137)) //We're an IPX packet?
													{
														if (!connectedclient->ppp_IPXCPstatus[PPP_RECVCONF]) //IPX not used on PPP?
														{
															goto invalidpacket; //Invalid for us!
														}
													}
													else //Unknown packet type?
													{
														goto invalidpacket; //Invalid for us!
													}
												}
												else if ((headertype == SDL_SwapBE16(0x0800)) || ((headertype == SDL_SwapBE16(0x8137)))) //IPv4 or IPX on PPPOE?
												{
													goto invalidpacket; //Invalid for us!
												}
											}
											else if (connectedclient->packetserver_slipprotocol == 2) //IPX protocol used?
											{
												if (headertype != SDL_SwapBE16(0x8137)) //We're an IPX packet!
												{
													goto invalidpacket; //Invalid for us!
												}
											}
											else //IPv4?
											{
												if ((headertype != SDL_SwapBE16(0x0800)) && (headertype!=SDL_SwapBE16(0x0806))) //We're an IP or ARP packet!
												{
													goto invalidpacket; //Invalid for us!
												}
											}
											if (connectedclient->packetserver_stage != PACKETSTAGE_PACKETS) goto invalidpacket; //Don't handle SLIP/PPP/IPX yet!
											if (ethernetheader.type == SDL_SwapBE16(0x0806)) //ARP?
											{
												if ((connectedclient->packetserver_slipprotocol == 1) || //IPv4 used?
													((connectedclient->packetserver_slipprotocol==3) && (!connectedclient->packetserver_slipprotocol_pppoe) && IPCP_OPEN) //IPv4 used on PPP?
													) //IPv4 protocol used?
												{
													//Always handle ARP packets, if we're IPv4 type!
													if (*pktlen != (28 + sizeof(ethernetheader.data))) //Unsupported length?
													{
														goto invalidpacket; //Invalid packet!
													}
													//TODO: Check if it's a request for us. If so, reply with our IPv4 address!
													memcpy(&ARPpacket,&(*pktsrc)[sizeof(ethernetheader.data)],28); //Retrieve the ARP packet!
													if ((SDL_SwapBE16(ARPpacket.htype)==1) && (ARPpacket.ptype==SDL_SwapBE16(0x0800)) && (ARPpacket.hlen==6) && (ARPpacket.plen==4) && (SDL_SwapBE16(ARPpacket.oper)==1))
													{
														//IPv4 ARP request
														//Check it's our IP, send a response if it's us!
														if (connectedclient->packetserver_useStaticIP) //IP filter is used?
														{
															if (memcmp(&ARPpacket.TPA, ((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe) && IPCP_OPEN)?&connectedclient->ipcp_ipaddress[PPP_SENDCONF][0]:&connectedclient->packetserver_staticIP[0], 4) != 0) //Static IP mismatch?
															{
																goto invalidpacket; //Invalid packet!
															}
															//It's for us, send a response!
															//Construct the ARP packet!
															ARPresponse.htype = ARPpacket.htype;
															ARPresponse.ptype = ARPpacket.ptype;
															ARPresponse.hlen = ARPpacket.hlen;
															ARPresponse.plen = ARPpacket.plen;
															ARPresponse.oper = SDL_SwapBE16(2); //Reply!
															memcpy(&ARPresponse.THA,&ARPpacket.SHA,6); //To the originator!
															memcpy(&ARPresponse.TPA,&ARPpacket.SPA,4); //Destination IP!
															memcpy(&ARPresponse.SHA,&maclocal,6); //Our MAC address!
															memcpy(&ARPresponse.SPA,&ARPpacket.TPA,4); //Our IP!
															//Construct the ethernet header!
															memcpy(&(*pktsrc)[sizeof(ethernetheader.data)],&ARPresponse,28); //Paste the response in the packet we're handling (reuse space)!
															//Now, construct the ethernet header!
															memcpy(&ppptransmitheader,&ethernetheader,sizeof(ethernetheader.data)); //Copy the header!
															memcpy(&ppptransmitheader.src,&maclocal,6); //From us!
															memcpy(&ppptransmitheader.dst,&ARPpacket.SHA,6); //To the requester!
															memcpy(&(*pktsrc)[0],ppptransmitheader.data,sizeof(ppptransmitheader.data)); //The ethernet header!
															//Now, the packet we've stored has become the packet to send back!
															if (sendpkt_pcap(connectedclient,(*pktsrc), (28 + 0xE))) //Send the response back to the originator!
															{
																//Discard the received packet, so nobody else handles it too!
																goto invalidpacket; //Finish up: we're parsed!
															}
															else
															{
																goto skippacketreceiving; //Keep waiting until we can send it!
															}
														}
														else
														{
															goto invalidpacket; //Invalid for our use, discard it!
														}
													}
													else
													{
														goto invalidpacket; //Invalid for our use, discard it!
													}
												}
												else
												{
													goto invalidpacket; //Invalid for our use, discard it!
												}
											}
											//Valid packet! Receive it!
											if (connectedclient->packetserver_slipprotocol) //Using slip or PPP protocol?
											{
												if (connectedclient->packetserver_slipprotocol == 3) //PPP?
												{
													if (connectedclient->packetserver_slipprotocol_pppoe) //Using PPPOE?
													{
														if (connectedclient->pppoe_discovery_PADS.length == 0) //No PADS received yet? Invalid packet!
														{
															goto invalidpacket; //Invalid packet: not ready yet!
														}
														if ((*pktsrc)[sizeof(ethernetheader.data) + 0] != 0x11) //Invalid VER/type?
														{
															goto invalidpacket; //Invalid packet!
														}
														if ((*pktsrc)[sizeof(ethernetheader.data) + 1] != 0) //Invalid Type?
														{
															goto invalidpacket; //Invalid packet!
														}
														word length, sessionid, requiredsessionid, pppoe_protocol;
														memcpy(&length, &(*pktsrc)[sizeof(ethernetheader.data) + 4], sizeof(length)); //The length field!
														memcpy(&sessionid, &(*pktsrc)[sizeof(ethernetheader.data) + 2], sizeof(sessionid)); //The length field!
														memcpy(&pppoe_protocol, &(*pktsrc)[sizeof(ethernetheader.data) + 6], sizeof(sessionid)); //The length field!
														memcpy(&requiredsessionid, &connectedclient->pppoe_discovery_PADS.buffer[sizeof(ethernetheader.data) + 4], sizeof(requiredsessionid)); //The required session id field!
														if (SDL_SwapBE16(length) < 4) //Invalid Length?
														{
															goto invalidpacket; //Invalid packet!
														}
														if (sessionid != requiredsessionid) //Invalid required session id(other client)?
														{
															goto invalidpacket; //Invalid packet!
														}
														if (SDL_SwapBE16(pppoe_protocol) != 0xC021) //Invalid packet type?
														{
															goto invalidpacket; //Invalid packet!
														}
														connectedclient->packetserver_packetpos = sizeof(ethernetheader.data) + 0x8; //Skip the ethernet header and give the raw IP data!
														connectedclient->packetserver_bytesleft = *pktlen - connectedclient->packetserver_packetpos; //How much is left to send?
													}
													else //Filter the packet depending on the packet type we're receiving!
													{
														if (PPP_parseReceivedPacketForClient((ethernetheader.type==SDL_SwapBE16(0x0800))?1:0,connectedclient)) //The packet is pending?
														{
															connectedclient->PPP_packetpendingforsending = 1; //Not ready, pending still!
														}
														else //Processed, discard it!
														{
															connectedclient->PPP_packetpendingforsending = 0; //Ready, not pending!
															goto invalidpacket; //Invalid packet!
														}
													}
												}
												else //SLIP?
												{
													connectedclient->packetserver_packetpos = sizeof(ethernetheader.data); //Skip the ethernet header and give the raw IP data!
													connectedclient->packetserver_bytesleft = MIN(*pktlen - connectedclient->packetserver_packetpos, SDL_SwapBE16(*((word*)&(*pktsrc)[sizeof(ethernetheader.data) + 2]))); //How much is left to send?
												}
											}
											else //We're using the ethernet header protocol?
											{
												//else, we're using ethernet header protocol, so take the packet and start sending it to the client!
												connectedclient->packetserver_packetack = 1; //We're acnowledging the packet, so start transferring it!
												connectedclient->packetserver_packetpos = 0; //Use the ethernet header as well!
												connectedclient->packetserver_bytesleft = *pktlen; //Use the entire packet, unpatched!
											}
										}
										else //Invalid length?
										{
										invalidpacket:
											//Discard the invalid packet!
											handledreceived = 1; //We're received!
											freez((void **)pktsrc, *pktlen, "SERVER_PACKET"); //Release the packet to receive new packets again!
											(*pktsrc) = NULL; //No packet!
											if (!((((connectedclient->packetserver_slipprotocol == 3)) && (!connectedclient->packetserver_slipprotocol_pppoe)))) //Not PPP?
											{
												connectedclient->packetserver_packetpos = 0; //Reset packet position for the new packets!
											}
											connectedclient->packetserver_packetack = 0; //Not acnowledged yet!
										}
									}
									skippacketreceiving:
									if (connectedclient->packetserver_stage != PACKETSTAGE_PACKETS)
									{
										if ((*pktsrc)) //Still have a packet allocated to discard?
										{
											goto invalidpacket; //Discard the received packet!
										}
										goto skipSLIP_PPP; //Don't handle SLIP/PPP because we're not ready yet!
									}
									if (
										(
											((*pktsrc) && (!((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe)))) || //Direct packet to be received by the client in encrypted form?
											((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe) && (connectedclient->ppp_response.size && connectedclient->ppp_response.buffer)) //Response ready for client in PPP form?
										) //Packet might be ready for sending?
										&& ( //Extra conditions for sending:
											(connectedclient->PPP_packetreadyforsending && (connectedclient->ppp_response.size && connectedclient->ppp_response.buffer)) || //PPP response ready for sending!
											((connectedclient->packetserver_slipprotocol!=3) || //Either not PPP? ...
											(connectedclient->packetserver_slipprotocol_pppoe && (connectedclient->packetserver_slipprotocol==3)) //... or is using PPPOE?
											)
										)
									) //Still a valid packet to send and allowed to send the packet that's stored?
									{
										//Convert the buffer into transmittable bytes using the proper encoding!
										if ((connectedclient->packetserver_bytesleft)) //Not finished yet?
										{
											if ((!connectedclient->PPP_packetstartsent) && (connectedclient->packetserver_slipprotocol == 3)) //Packet hasn't been started yet and needs to be started properly?
											{
												writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], PPP_END); //Start of frame!
												connectedclient->PPP_packetstartsent = 1; //Start has been sent!
												goto doPPPtransmit; //Handle the tranmit of the PPP frame start!
											}
											//Start transmitting data into the buffer, according to the protocol!
											--connectedclient->packetserver_bytesleft;
											if ((connectedclient->packetserver_slipprotocol == 3) && (connectedclient->packetserver_slipprotocol_pppoe == 0)) //PPP?
											{
												datatotransmit = connectedclient->ppp_response.buffer[connectedclient->packetserver_packetpos++]; //Take the PPP packet from the buffer that's responding instead of the raw packet that's received (which is parsed already and in a different format)!
											}
											else //Normal packet that's sent?
											{
												datatotransmit = (*pktsrc)[connectedclient->packetserver_packetpos++]; //Read the data to construct!
											}
											if (connectedclient->packetserver_slipprotocol==3) //PPP?
											{
												if (PPPOE_ENCODEDECODE || (!connectedclient->packetserver_slipprotocol_pppoe)) //Encoding PPP?
												{
													if (datatotransmit == PPP_END) //End byte?
													{
														writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], PPP_ESC); //Escaped ...
														writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], PPP_ENCODEESC(PPP_END)); //END raw data!
													}
													else if (datatotransmit == PPP_ESC) //ESC byte?
													{
														writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], PPP_ESC); //Escaped ...
														writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], PPP_ENCODEESC(PPP_ESC)); //ESC raw data!
													}
													else //Normal data?
													{
														if ((!connectedclient->packetserver_slipprotocol_pppoe) && (datatotransmit < 0x20)) //Might need to be escaped?
														{
															ppp_transmitasynccontrolcharactermap = connectedclient->asynccontrolcharactermap[PPP_SENDCONF]; //The map to use!
															LCPbuf = connectedclient->ppp_response.buffer; //What are we sending?
															if ((
																(LCPbuf[0]==0xFF) && //All-stations
																(LCPbuf[1]==0x03) && //UI
																(LCPbuf[2]==0xC0) && //LCP ...
																(LCPbuf[3]==0x21) && //... protocol
																((LCPbuf[4]>=0x01) && //Codes 1 through ...
																(LCPbuf[4]<=0x07)) //... 7
																)
																||(!connectedclient->ppp_LCPstatus[PPP_SENDCONF])) //LCP options not setup?
															{
																ppp_transmitasynccontrolcharactermap = 0xFFFFFFFFU; //Force-escape all control characters!
															}
															if ((ppp_transmitasynccontrolcharactermap & (1 << (datatotransmit & 0x1F)))) //To be escaped?
															{
																writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], PPP_ESC); //Escaped ...
																writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], PPP_ENCODEESC(datatotransmit)); //ESC raw data!
															}
															else //Not escaped!
															{
																writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], datatotransmit); //Unescaped!
															}
														}
														else
														{
															writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], datatotransmit); //Unescaped!
														}
													}
												}
												else //Not encoding PPP?
												{
													writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], datatotransmit); //Raw!
												}
											}
											else //SLIP?
											{
												if (datatotransmit == SLIP_END) //End byte?
												{
													writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], SLIP_ESC); //Escaped ...
													writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], SLIP_ESC_END); //END raw data!
												}
												else if (datatotransmit == SLIP_ESC) //ESC byte?
												{
													writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], SLIP_ESC); //Escaped ...
													writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], SLIP_ESC_ESC); //ESC raw data!
												}
												else //Normal data?
												{
													writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], datatotransmit); //Unescaped!
												}
											}
										}
										else //Finished transferring a frame?
										{
											if (connectedclient->packetserver_slipprotocol==3) //PPP?
											{
												if (connectedclient->packetserver_slipprotocol == 3) //PPP?
												{
													writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], PPP_END); //END of frame!
													connectedclient->PPP_packetstartsent = 0; //Last wasn't END! This is ignored for PPP frames (always send them)!
													packetServerFreePacketBufferQueue(&connectedclient->ppp_response); //Free the response that's queued for packets to be sent to the client!
													goto doPPPtransmit; //Don't perform normal receive buffer cleanup, as this isn't used here!
												}
											}
											else //SLIP?
											{
												writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], SLIP_END); //END of frame!
											}
											freez((void **)pktsrc, *pktlen, "SERVER_PACKET"); //Release the packet to receive new packets again!
											(*pktsrc) = NULL; //Discard the packet anyway, no matter what!
											connectedclient->packetserver_packetpos = 0; //Reset packet position!
											connectedclient->packetserver_packetack = 0; //Not acnowledged yet!
										}
									}
								}
							}
						}

						doPPPtransmit: //NOP operation for the PPP packet that's transmitted!
						//Transmit the encoded buffer to the client at the used speed!

						if (connectedclient->packetserver_stage != PACKETSTAGE_PACKETS)
						{
							goto skipSLIP_PPP; //Don't handle SLIP/PPP because we're not ready yet!
						}

						//Handle transmitting packets(with automatically increasing buffer sizing, as a packet can be received of any size theoretically)!
						if (peekfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &datatotransmit)) //Is anything transmitted yet?
						{
							if ((connectedclient->packetserver_transmitlength == 0) && (!((connectedclient->packetserver_slipprotocol==3) && (!connectedclient->packetserver_slipprotocol_pppoe)))) //We might need to create an ethernet header?
							{
								//Build an ethernet header, platform dependent!
								//Use the data provided by the settings!
								byte b;
								if ((connectedclient->packetserver_slipprotocol == 3) && connectedclient->pppoe_discovery_PADS.buffer && connectedclient->pppoe_discovery_PADS.length) //PPP?
								{
									memcpy(&ppptransmitheader.data, &connectedclient->pppoe_discovery_PADS.buffer,sizeof(ppptransmitheader.data)); //Make a local copy for usage!
								}
								for (b = 0; b < 6; ++b) //Process MAC addresses!
								{
									if ((connectedclient->packetserver_slipprotocol == 3) && connectedclient->pppoe_discovery_PADS.buffer && connectedclient->pppoe_discovery_PADS.length) //PPP?
									{
										ethernetheader.dst[b] = ppptransmitheader.src[b]; //The used server MAC is the destination!
										ethernetheader.src[b] = ppptransmitheader.dst[b]; //The Packet server MAC is the source!
									}
									else //SLIP
									{
										ethernetheader.dst[b] = packetserver_gatewayMAC[b]; //Gateway MAC is the destination!
										ethernetheader.src[b] = packetserver_sourceMAC[b]; //Packet server MAC is the source!
									}
								}
								if (connectedclient->packetserver_slipprotocol==3) //PPP?
								{
									if (Packetserver_clients->packetserver_slipprotocol_pppoe) //Using PPPOE?
									{
										if (connectedclient->pppoe_discovery_PADS.buffer && connectedclient->pppoe_discovery_PADS.length) //Valid to send?
										{
											ethernetheader.type = SDL_SwapBE16(0x8864); //Our packet type!
										}
										else goto noPPPtransmit; //Ignore the transmitter for now!
									}
									//Otherwise, PPP packet to send? Are we to do something with this now?
								}
								else if (connectedclient->packetserver_slipprotocol==2) //IPX?
								{
									ethernetheader.type = SDL_SwapBE16(0x8137); //We're an IPX packet!
								}
								else //IPv4?
								{
									ethernetheader.type = SDL_SwapBE16(0x0800); //We're an IP packet!
								}
								for (b = 0; b < 14; ++b) //Use the provided ethernet packet header!
								{
									if (!packetServerAddWriteQueue(connectedclient, ethernetheader.data[b])) //Failed to add?
									{
										break; //Stop adding!
									}
								}
								if ((connectedclient->packetserver_slipprotocol == 3) && (connectedclient->packetserver_slipprotocol_pppoe) && connectedclient->pppoe_discovery_PADS.buffer && connectedclient->pppoe_discovery_PADS.length) //PPP?
								{
									if (!packetServerAddWriteQueue(connectedclient, 0x11)) //V/T?
									{
										goto noPPPtransmit; //Stop adding!
									}
									if (!packetServerAddWriteQueue(connectedclient, 0x00)) //Code?
									{
										goto noPPPtransmit; //Stop adding!
									}
									NETWORKVALSPLITTER.bval[0] = connectedclient->pppoe_discovery_PADS.buffer[0x10]; //Session_ID!
									NETWORKVALSPLITTER.bval[1] = connectedclient->pppoe_discovery_PADS.buffer[0x11]; //Session_ID!
									if (!packetServerAddWriteQueue(connectedclient, NETWORKVALSPLITTER.bval[0])) //First byte?
									{
										goto noPPPtransmit; //Stop adding!
									}
									if (!packetServerAddWriteQueue(connectedclient, NETWORKVALSPLITTER.bval[1])) //Second byte?
									{
										goto noPPPtransmit; //Stop adding!
									}
									NETWORKVALSPLITTER.wval = SDL_SwapBE16(0); //Length: to be filled in later!
									if (!packetServerAddWriteQueue(connectedclient, NETWORKVALSPLITTER.bval[0])) //First byte?
									{
										goto noPPPtransmit; //Stop adding!
									}
									if (!packetServerAddWriteQueue(connectedclient, NETWORKVALSPLITTER.bval[1])) //Second byte?
									{
										goto noPPPtransmit; //Stop adding!
									}
									NETWORKVALSPLITTER.wval = SDL_SwapBE16(0xC021); //Protocol!
									if (!packetServerAddWriteQueue(connectedclient, NETWORKVALSPLITTER.bval[0])) //First byte?
									{
										goto noPPPtransmit; //Stop adding!
									}
									if (!packetServerAddWriteQueue(connectedclient, NETWORKVALSPLITTER.bval[1])) //Second byte?
									{
										goto noPPPtransmit; //Stop adding!
									}
								}
								if (
									((connectedclient->packetserver_transmitlength != 14) && (connectedclient->packetserver_slipprotocol!=3)) || 
									((connectedclient->packetserver_transmitlength != 22) && (connectedclient->packetserver_slipprotocol == 3) && (connectedclient->packetserver_slipprotocol_pppoe))
									) //Failed to generate header?
								{
									dolog("ethernetcard", "Error: Transmit initialization failed. Resetting transmitter!");
									noPPPtransmit:
									if (!(connectedclient->pppoe_discovery_PADS.buffer && connectedclient->pppoe_discovery_PADS.length) && connectedclient->packetserver_slipprotocol_pppoe) //Not ready to send?
									{
										if (!(connectedclient->pppoe_discovery_PADI.buffer && connectedclient->pppoe_discovery_PADI.length)) //No PADI sent yet? Start sending one now to restore the connection!
										{
											PPPOE_requestdiscovery(connectedclient); //Try to request a new discovery for transmitting PPP packets!
										}
										goto skipSLIP_PPP; //Don't handle the sent data yet, prepare for sending by reconnecting to the PPPOE server!
									}
									connectedclient->packetserver_transmitlength = 0; //Abort the packet generation!
								}
							}
							
							//Now, parse the normal packet and decrypt it!
							if (((datatotransmit == SLIP_END) && (connectedclient->packetserver_slipprotocol!=3))
									|| ((datatotransmit==PPP_END) && (connectedclient->packetserver_slipprotocol==3))) //End-of-frame? Send the frame!
							{
								if (connectedclient->packetserver_transmitstate && (connectedclient->packetserver_slipprotocol!=3)) //Were we already escaping?
								{
									if (packetServerAddWriteQueue(connectedclient, SLIP_ESC)) //Ignore the escaped sequence: it's invalid, thus parsed raw!
									{
										connectedclient->packetserver_transmitstate = 0; //We're not escaping something anymore!
									}
								}
								else if (connectedclient->packetserver_transmitstate) //Escaped with  PPP? This is an abort sequence!
								{
									connectedclient->packetserver_transmitstate = 0; //Stop escaping!
									//PPP is sending a frame abort?
									goto discardPPPsentframe;
								}
								/*
								if (connectedclient->packetserver_slipprotocol == 3) //PPP has a different concept of this than SLIP!
								{
									//PPP END is a toggle for active data!
									connectedclient->ppp_sendframing ^= 1; //Toggle sender framing!
									if (connectedclient->ppp_sendframing) //Was just toggled on? Discard the packet that was sending before, as it wasn't a packet! If only bit 2 is set at this point, it's a packet discard instead (and bit 2 needs to be cleared).
									{
										if (connectedclient->packetserver_slipprotocol_pppoe) //Using PPPOE?
										{
											if (!packetServerAddWriteQueue(connectedclient, PPP_END))
											{
												connectedclient->ppp_sendframing ^= 1; //Untoggle sender framing: still pending!
												goto skipSLIP_PPP; //Don't handle the sending of the packet yet: not ready!
											}
										}
										connectedclient->ppp_sendframing &= 1; //Ignore the discard frame flag, which is bit 1 being set to cause a discard only once for the end-of-frame! The value here is 2(discard) instead of 0(transmit/process). So to make future packets behave again, clear bit 1.
										goto discardPPPsentframe; //Discard the frame that's currently buffered, if there's any!
									}
								}
								*/
								if (connectedclient->packetserver_transmitstate == 0) //Ready to send the packet(not waiting for the buffer to free)?
								{
									//Clean up the packet container!
									if (
										((connectedclient->packetserver_transmitlength > sizeof(ethernetheader.data)) && (connectedclient->packetserver_slipprotocol!=3)) || //Anything buffered(the header is required)?
										((connectedclient->packetserver_transmitlength > 0x22) && (connectedclient->packetserver_slipprotocol == 3) && (connectedclient->packetserver_slipprotocol_pppoe)) //Anything buffered(the header is required)?
										|| ((connectedclient->packetserver_transmitlength > 0) && (connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe)) //Anything buffered(the header is required)?
										)
									{
										//Send the frame to the server, if we're able to!
										if ((connectedclient->packetserver_transmitlength <= 0xFFFF) || (connectedclient->packetserver_slipprotocol == 3)) //Within length range?
										{
											if (connectedclient->packetserver_slipprotocol == 3) //PPP?
											{
												if (connectedclient->packetserver_slipprotocol_pppoe) //Using PPPOE?
												{
													if (!packetServerAddWriteQueue(connectedclient, PPP_END))
													{
														connectedclient->ppp_sendframing ^= 1; //Untoggle sender framing: still pending!
														goto skipSLIP_PPP; //Don't handle the sending of the packet yet: not ready!
													}
												}
											}
											if ((connectedclient->packetserver_slipprotocol == 3) && (connectedclient->packetserver_slipprotocol_pppoe)) //Length field needs fixing up?
											{
												NETWORKVALSPLITTER.wval = SDL_SwapBE16(connectedclient->packetserver_transmitlength-0x22); //The length of the PPP packet itself!
												connectedclient->packetserver_transmitbuffer[0x12] = NETWORKVALSPLITTER.bval[0]; //First byte!
												connectedclient->packetserver_transmitbuffer[0x13] = NETWORKVALSPLITTER.bval[1]; //Second byte!
											}
											if ((!connectedclient->packetserver_slipprotocol_pppoe) && (connectedclient->packetserver_slipprotocol == 3)) //Able to send the packet for the PPP connection we manage?
											{
												if (!PPP_parseSentPacketFromClient(connectedclient, 1)) //Parse PPP packets to their respective ethernet or IPv4 protocols for sending to the ethernet layer, as supported!
												{
													connectedclient->ppp_sendframing ^= 1; //Toggle sender framing!
													goto skipSLIP_PPP; //Keep the packet parsing pending!
												}
											}
											else //Able to send the packet always?
											{
												if (!sendpkt_pcap(connectedclient,connectedclient->packetserver_transmitbuffer, connectedclient->packetserver_transmitlength)) //Send the packet!
												{
													goto skipSLIP_PPP; //Keep the packet parsing pending!
												}
											}
										}
										else
										{
											dolog("ethernetcard", "Error: Can't send packet: packet is too large to send(size: %u)!", connectedclient->packetserver_transmitlength);
										}
										discardPPPsentframe: //Discard a sent frame!
										//Now, cleanup the buffered frame!
										freez((void**)&connectedclient->packetserver_transmitbuffer, connectedclient->packetserver_transmitsize, "MODEM_SENDPACKET"); //Free 
										connectedclient->packetserver_transmitsize = 1024; //How large is out transmit buffer!
										connectedclient->packetserver_transmitbuffer = zalloc(1024, "MODEM_SENDPACKET", NULL); //Simple transmit buffer, the size of a packet byte(when encoded) to be able to buffer any packet(since any byte can be doubled)!
									}
									//Silently discard the empty packets!
									connectedclient->packetserver_transmitlength = 0; //We're at the start of this buffer, nothing is sent yet!
									connectedclient->packetserver_transmitstate = 0; //Not escaped anymore!
									readfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &datatotransmit); //Ignore the data, just discard the packet END!
								}
							}
							else if ((datatotransmit==PPP_ESC) && (connectedclient->packetserver_slipprotocol==3) && ((!connectedclient->packetserver_slipprotocol_pppoe) || PPPOE_ENCODEDECODE)) //PPP ESC?
							{
								readfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &datatotransmit); //Discard, as it's processed!
								connectedclient->packetserver_transmitstate = 1; //We're escaping something! Multiple escapes are ignored and not sent!
							}
							else if ((connectedclient->packetserver_transmitstate) && (connectedclient->packetserver_slipprotocol==3) && ((!connectedclient->packetserver_slipprotocol_pppoe) || PPPOE_ENCODEDECODE)) //PPP ESCaped value?
							{
								if (connectedclient->packetserver_transmitlength || ((connectedclient->packetserver_slipprotocol == 3) && ((!connectedclient->packetserver_slipprotocol_pppoe)))) //Gotten a valid packet to start adding an escaped value to?
								{
									if (packetServerAddWriteQueue(connectedclient, PPP_DECODEESC(datatotransmit))) //Added to the queue?
									{
										readfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &datatotransmit); //Ignore the data, just discard the packet byte!
										connectedclient->packetserver_transmitstate = 0; //We're not escaping something anymore!
									}
								}
								else //Unable to parse into the buffer? Discard!
								{
									readfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &datatotransmit); //Ignore the data, just discard the packet byte!
									connectedclient->packetserver_transmitstate = 0; //We're not escaping something anymore!
								}
							}
							else if ((datatotransmit == SLIP_ESC) && (connectedclient->packetserver_slipprotocol!=3)) //Escaped something?
							{
								if (connectedclient->packetserver_transmitstate) //Were we already escaping?
								{
									if (packetServerAddWriteQueue(connectedclient, SLIP_ESC)) //Ignore the escaped sequence: it's invalid, thus parsed raw!
									{
										connectedclient->packetserver_transmitstate = 0; //We're not escaping something anymore!
									}
								}
								if (connectedclient->packetserver_transmitstate == 0) //Can we start a new escape?
								{
									readfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &datatotransmit); //Discard, as it's processed!
									connectedclient->packetserver_transmitstate = 1; //We're escaping something! Multiple escapes are ignored and not sent!
								}
							}
							else if (connectedclient->packetserver_slipprotocol==3) //Active PPP data?
							{
								if (connectedclient->packetserver_transmitlength || (!connectedclient->packetserver_slipprotocol_pppoe)) //Gotten a valid packet?
								{
									if ((!connectedclient->packetserver_slipprotocol_pppoe) && (datatotransmit < 0x20)) //Might need to be escaped?
									{
										if ((connectedclient->asynccontrolcharactermap[PPP_RECVCONF] & (1 << (datatotransmit & 0x1F)))||(!connectedclient->ppp_LCPstatus[PPP_RECVCONF])) //To be escaped?
										{
											readfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &datatotransmit); //Ignore the data, just discard the packet byte!
										}
										else //Not escaped!
										{
											goto addUnescapedValue; //Process an unescaped PPP value!
										}
									}
									else
									{
										goto addUnescapedValue; //Process an unescaped PPP value!
									}
								}
							}
							else if (connectedclient->packetserver_slipprotocol!=3) //Active SLIP data?
							{
								if (connectedclient->packetserver_transmitlength) //Gotten a valid packet?
								{
									if (connectedclient->packetserver_transmitstate && (datatotransmit == SLIP_ESC_END)) //Transposed END sent?
									{
										if (packetServerAddWriteQueue(connectedclient,SLIP_END)) //Added to the queue?
										{
											readfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &datatotransmit); //Ignore the data, just discard the packet byte!
											connectedclient->packetserver_transmitstate = 0; //We're not escaping something anymore!
										}
									}
									else if (connectedclient->packetserver_transmitstate && (datatotransmit == SLIP_ESC_ESC)) //Transposed ESC sent?
									{
										if (packetServerAddWriteQueue(connectedclient,SLIP_ESC)) //Added to the queue?
										{
											readfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &datatotransmit); //Ignore the data, just discard the packet byte!
											connectedclient->packetserver_transmitstate = 0; //We're not escaping something anymore!
										}
									}
									else //Parse as a raw data when invalidly escaped or sent unescaped! Also terminate escape sequence as required!
									{
										if (connectedclient->packetserver_transmitstate) //Were we escaping?
										{
											if (packetServerAddWriteQueue(connectedclient, SLIP_ESC)) //Ignore the escaped sequence: it's invalid, thus parsed unescaped!
											{
												connectedclient->packetserver_transmitstate = 0; //We're not escaping something anymore!
											}
										}
										addUnescapedValue:
										if (connectedclient->packetserver_transmitstate==0) //Can we parse the raw data?
										{
											if (packetServerAddWriteQueue(connectedclient, datatotransmit)) //Added to the queue?
											{
												readfifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], &datatotransmit); //Ignore the data, just discard the packet byte!
												connectedclient->packetserver_transmitstate = 0; //We're not escaping something anymore!
											}
										}
									}
								}
							}
						}
						//Perform automatic packet handling with lower priority than the client!
						if (connectedclient->packetserver_slipprotocol == 3) //PPP?
						{
							if (!connectedclient->packetserver_slipprotocol_pppoe) //Not using PPPOE?
							{
								if (!PPP_parseSentPacketFromClient(connectedclient, 0)) //Parse PPP packets to their respective ethernet or IPv4 protocols for sending to the ethernet layer, as supported!
								{
									goto skipSLIP_PPP; //Keep the packet parsing pending!
								}
							}
						}
					skipSLIP_PPP: //SLIP isn't available?
					//Handle an authentication stage
						if (connectedclient->packetserver_stage == PACKETSTAGE_REQUESTUSERNAME)
						{
							authstage_startrequest(timepassed,connectedclient,"username:",PACKETSTAGE_ENTERUSERNAME);
						}

						if (connectedclient->packetserver_stage == PACKETSTAGE_ENTERUSERNAME)
						{
							switch (authstage_enterfield(timepassed, connectedclient, &connectedclient->packetserver_username[0], sizeof(connectedclient->packetserver_username),0,(char)0))
							{
							case 0: //Do nothing!
								break;
							case 1: //Finished stage!
								PacketServer_startNextStage(connectedclient, PACKETSTAGE_REQUESTPASSWORD); //Next stage: password!
								break;
							case 2: //Send the output buffer!
								goto sendoutputbuffer;
								break;
							}
						}

						if (connectedclient->packetserver_stage == PACKETSTAGE_REQUESTPASSWORD)
						{
							authstage_startrequest(timepassed,connectedclient,"password:",PACKETSTAGE_ENTERPASSWORD);
						}

						if (connectedclient->packetserver_stage == PACKETSTAGE_ENTERPASSWORD)
						{
							switch (authstage_enterfield(timepassed, connectedclient, &connectedclient->packetserver_password[0], sizeof(connectedclient->packetserver_password), 0, '*'))
							{
							case 0: //Do nothing!
								break;
							case 1: //Finished stage!
								PacketServer_startNextStage(connectedclient, PACKETSTAGE_REQUESTPROTOCOL); //Next stage: protocol!
								break;
							case 2: //Send the output buffer!
								goto sendoutputbuffer;
								break;
							}
						}

						if (connectedclient->packetserver_stage == PACKETSTAGE_REQUESTPROTOCOL)
						{
							authstage_startrequest(timepassed,connectedclient,"protocol:",PACKETSTAGE_ENTERPROTOCOL);
						}

						if (connectedclient->packetserver_stage == PACKETSTAGE_ENTERPROTOCOL)
						{
							switch (authstage_enterfield(timepassed, connectedclient, &connectedclient->packetserver_protocol[0], sizeof(connectedclient->packetserver_protocol),1,(char)0))
							{
							case 0: //Do nothing!
								break;
							case 1: //Finished stage!
								if (connectedclient->packetserver_credentials_invalid) goto packetserver_autherror; //Authentication error!
								if (packetserver_authenticate(connectedclient)) //Authenticated?
								{
									connectedclient->packetserver_slipprotocol = ((strcmp(connectedclient->packetserver_protocol, "ppp") == 0) || (strcmp(connectedclient->packetserver_protocol, "pppoe") == 0))?3:((strcmp(connectedclient->packetserver_protocol, "ipxslip") == 0)?2:((strcmp(connectedclient->packetserver_protocol, "slip") == 0) ? 1 : 0)); //Are we using the slip protocol?
									connectedclient->packetserver_slipprotocol_pppoe = (strcmp(connectedclient->packetserver_protocol, "pppoe") == 0) ? 1 : 0; //Use PPPOE instead of PPP?
									PacketServer_startNextStage(connectedclient, (connectedclient->packetserver_useStaticIP==2)?PACKETSTAGE_DHCP:PACKETSTAGE_INFORMATION); //We're logged in! Give information stage next!
									if (connectedclient->packetserver_slipprotocol_pppoe) //Using PPPOE?
									{
										PPPOE_requestdiscovery(connectedclient); //Start the discovery phase of the connected client!
									}
								}
								else goto packetserver_autherror; //Authentication error!
								break;
							case 2: //Send the output buffer!
								goto sendoutputbuffer;
								break;
							}
						}

						if (connectedclient->packetserver_stage == PACKETSTAGE_DHCP)
						{
							if (connectedclient->packetserver_useStaticIP == 2) //Sending discovery packet of DHCP?
							{
								//Create and send a discovery packet! Use the packetServerAddPacketBufferQueue to create the packet!
								packetServerFreePacketBufferQueue(&connectedclient->DHCP_discoverypacket); //Free the old one first, if present!
								//Now, create the packet to send using a function!
								//Send it!

								connectedclient->packetserver_useStaticIP = 3; //Start waiting for the Offer!
								connectedclient->packetserver_stage_byte = 0; //Init to start of string!
								connectedclient->packetserver_delay = PACKETSERVER_DHCP_TIMEOUT; //Delay this until we timeout!
							}

							if (connectedclient->packetserver_useStaticIP == 3) //Waiting for the DHCP Offer?
							{
								connectedclient->packetserver_delay -= timepassed; //Delaying!
								if ((connectedclient->packetserver_delay <= 0.0) || (!connectedclient->packetserver_delay)) //Finished?
								{
									connectedclient->packetserver_delay = (DOUBLE)0; //Finish the delay!
									//Timeout has occurred! Disconnect!
									goto packetserver_autherror; //Disconnect the client: we can't help it!
								}
								lock(LOCK_PCAP);
								if ((*pktsrc)) //Packet has been received before the timeout?
								{
									if (0) //Gottten a DHCP packet?
									{
										//Check if it's ours?
										if (0) //It's ours?
										{
											//If an Offer packet, do the following:
											packetServerFreePacketBufferQueue(&connectedclient->DHCP_offerpacket); //Free the old one first, if present!
											//Save it in the storage!
											for (currentpos = 0; currentpos < *pktlen;) //Parse the entire packet!
											{
												if (!packetServerAddPacketBufferQueue(&connectedclient->DHCP_offerpacket, (*pktsrc)[currentpos++])) //Failed to save the packet?
												{
													goto packetserver_autherror; //Error out: disconnect!
												}
											}
											connectedclient->packetserver_useStaticIP = 4; //Start sending the Request!
											connectedclient->packetserver_stage_byte = 0; //Init to start of string!
											connectedclient->packetserver_delay = PACKETSERVER_DHCP_TIMEOUT; //Delay this until we timeout!
										}
									}
								}
								unlock(LOCK_PCAP);
							}
							if (connectedclient->packetserver_useStaticIP == 4) //Sending request packet of DHCP?
							{
								//Create and send a discovery packet! Use the packetServerAddPacketBufferQueue to create the packet!
								packetServerFreePacketBufferQueue(&connectedclient->DHCP_requestpacket); //Free the old one first, if present!
								//Now, create the packet to send using a function!
								//Send it!

								connectedclient->packetserver_useStaticIP = 5; //Start waiting for the Acknowledgement!
								connectedclient->packetserver_stage_byte = 0; //Init to start of string!
								connectedclient->packetserver_delay = PACKETSERVER_DHCP_TIMEOUT; //Delay this until we timeout!
							}

							if (connectedclient->packetserver_useStaticIP == 5) //Waiting for the DHCP Acknoledgement?
							{
								connectedclient->packetserver_delay -= timepassed; //Delaying!
								if ((connectedclient->packetserver_delay <= 0.0) || (!connectedclient->packetserver_delay)) //Finished?
								{
									connectedclient->packetserver_delay = (DOUBLE)0; //Finish the delay!
									//Timeout has occurred! Disconnect!
									goto packetserver_autherror; //Disconnect the client: we can't help it!
								}
								lock(LOCK_PCAP);
								if ((*pktsrc)) //Packet has been received before the timeout?
								{
									if (0) //Gottten a DHCP packet?
									{
										//Check if it's ours?
										if (0) //It's ours?
										{
											//If it's a NACK or Decline, abort!
											if (0) //NACK or Decline?
											{
												goto packetserver_autherror; //Disconnect the client: we can't help it!
											}
											//If an Acknowledgement packet, do the following:
											packetServerFreePacketBufferQueue(&connectedclient->DHCP_offerpacket); //Free the old one first, if present!
											//Save it in the storage!
											for (currentpos = 0; currentpos < *pktlen;) //Parse the entire packet!
											{
												if (!packetServerAddPacketBufferQueue(&connectedclient->DHCP_offerpacket, (*pktsrc)[currentpos++])) //Failed to save the packet?
												{
													goto packetserver_autherror; //Error out: disconnect!
												}
											}
											connectedclient->packetserver_useStaticIP = 6; //Always wait for NACK!
											connectedclient->packetserver_stage_byte = 0; //Init to start of string!
											connectedclient->packetserver_delay = PACKETSERVER_DHCP_TIMEOUT; //Delay this until we timeout!
										}
									}
								}
								unlock(LOCK_PCAP);
							}

							if (connectedclient->packetserver_useStaticIP == 7) //Sending release packet of DHCP?
							{
								//Create and send a discovery packet! Use the packetServerAddPacketBufferQueue to create the packet!
								packetServerFreePacketBufferQueue(&connectedclient->DHCP_releasepacket); //Free the old one first, if present!
								//Now, create the packet to send using a function!
								//Send it!

								connectedclient->packetserver_useStaticIP = 8; //Start waiting for the Acknowledgement!
								connectedclient->packetserver_stage_byte = 0; //Init to start of string!
								connectedclient->packetserver_delay = PACKETSERVER_DHCP_TIMEOUT; //Delay this until we timeout!
							}
							if (connectedclient->packetserver_useStaticIP == 8) //Waiting for the DHCP Acknoledgement?
							{
								connectedclient->packetserver_delay -= timepassed; //Delaying!
								if ((connectedclient->packetserver_delay <= 0.0) || (!connectedclient->packetserver_delay)) //Finished?
								{
									connectedclient->packetserver_delay = (DOUBLE)0; //Finish the delay!
									//Timeout has occurred! Disconnect!
									goto packetserver_autherror; //Disconnect the client: we can't help it!
								}
								lock(LOCK_PCAP);
								if ((*pktsrc)) //Packet has been received before the timeout?
								{
									if (0) //Gottten a DHCP packet?
									{
										//Check if it's ours?
										if (0) //It's ours?
										{
											//If it's a NACK or Decline, abort!
											if (0) //NACK or Decline?
											{
												goto packetserver_autherror; //Disconnect the client: we can't help it!
											}
											//If an Acknowledgement packet, do the following:
											packetServerFreePacketBufferQueue(&connectedclient->DHCP_discoverypacket); //Free the old one first, if present!
											packetServerFreePacketBufferQueue(&connectedclient->DHCP_offerpacket); //Free the old one first, if present!
											packetServerFreePacketBufferQueue(&connectedclient->DHCP_requestpacket); //Free the old one first, if present!
											packetServerFreePacketBufferQueue(&connectedclient->DHCP_acknowledgepacket); //Free the old one first, if present!
											packetServerFreePacketBufferQueue(&connectedclient->DHCP_releasepacket); //Free the old one first, if present!
											connectedclient->packetserver_useStaticIP = 0; //No request anymore!
											connectedclient->packetserver_stage_byte = 0; //Init to start of string!
											connectedclient->packetserver_delay = PACKETSERVER_DHCP_TIMEOUT; //Delay this until we timeout!
										}
									}
								}
								unlock(LOCK_PCAP);
							}
						}

						//Check for DHCP release requirement always, even when connected!
						if (connectedclient->packetserver_useStaticIP == 6) //Looking for the DHCP NACK?
						{
							lock(LOCK_PCAP);
							if ((*pktsrc)) //Packet has been received before the timeout?
							{
								if (0) //Gottten a DHCP packet?
								{
									//Check if it's ours?
									if (0) //It's ours?
									{
										//If it's a NACK or Decline, abort!
										if (0) //NACK or Decline?
										{
											packetServerFreePacketBufferQueue(&connectedclient->DHCP_discoverypacket); //Free the old one first, if present!
											packetServerFreePacketBufferQueue(&connectedclient->DHCP_offerpacket); //Free the old one first, if present!
											packetServerFreePacketBufferQueue(&connectedclient->DHCP_requestpacket); //Free the old one first, if present!
											packetServerFreePacketBufferQueue(&connectedclient->DHCP_acknowledgepacket); //Free the old one first, if present!
											goto packetserver_autherror; //Disconnect the client: we can't help it anymore!
										}
									}
								}
							}
							unlock(LOCK_PCAP);
						}

						if (connectedclient->packetserver_stage == PACKETSTAGE_INFORMATION)
						{
							if (connectedclient->packetserver_stage_byte == PACKETSTAGE_INITIALIZING)
							{
								memset(&connectedclient->packetserver_stage_str, 0, sizeof(connectedclient->packetserver_stage_str));
								snprintf(connectedclient->packetserver_stage_str, sizeof(connectedclient->packetserver_stage_str), "\r\nMACaddress:%02x:%02x:%02x:%02x:%02x:%02x\r\ngatewayMACaddress:%02x:%02x:%02x:%02x:%02x:%02x\r\n", packetserver_sourceMAC[0], packetserver_sourceMAC[1], packetserver_sourceMAC[2], packetserver_sourceMAC[3], packetserver_sourceMAC[4], packetserver_sourceMAC[5], packetserver_gatewayMAC[0], packetserver_gatewayMAC[1], packetserver_gatewayMAC[2], packetserver_gatewayMAC[3], packetserver_gatewayMAC[4], packetserver_gatewayMAC[5]);
								if (connectedclient->packetserver_useStaticIP && (connectedclient->packetserver_slipprotocol!=3)) //IP filter?
								{
									memset(&connectedclient->packetserver_staticIPstr_information, 0, sizeof(connectedclient->packetserver_staticIPstr_information));
									snprintf(connectedclient->packetserver_staticIPstr_information, sizeof(connectedclient->packetserver_staticIPstr_information), "IPaddress:%s\r\n", connectedclient->packetserver_staticIPstr); //Static IP!
									safestrcat(connectedclient->packetserver_stage_str, sizeof(connectedclient->packetserver_stage_str), connectedclient->packetserver_staticIPstr_information); //Inform about the static IP!
								}
								connectedclient->packetserver_stage_byte = 0; //Init to start of string!
								connectedclient->packetserver_delay = PACKETSERVER_MESSAGE_DELAY; //Delay this until we start transmitting!
							}
							connectedclient->packetserver_delay -= timepassed; //Delaying!
							if ((connectedclient->packetserver_delay <= 0.0) || (!connectedclient->packetserver_delay)) //Finished?
							{
								connectedclient->packetserver_delay = (DOUBLE)0; //Finish the delay!
								if (writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], connectedclient->packetserver_stage_str[connectedclient->packetserver_stage_byte])) //Transmitted?
								{
									if (++connectedclient->packetserver_stage_byte == safestrlen(connectedclient->packetserver_stage_str, sizeof(connectedclient->packetserver_stage_str))) //Finished?
									{
										PacketServer_startNextStage(connectedclient,PACKETSTAGE_READY); //Start ready stage next!
									}
								}
							}
						}

						if (connectedclient->packetserver_stage == PACKETSTAGE_READY)
						{
							if (connectedclient->packetserver_stage_byte == PACKETSTAGE_INITIALIZING)
							{
								memset(&connectedclient->packetserver_stage_str, 0, sizeof(connectedclient->packetserver_stage_str));
								safestrcpy(connectedclient->packetserver_stage_str, sizeof(connectedclient->packetserver_stage_str), "\rCONNECTED\r");
								connectedclient->packetserver_stage_byte = 0; //Init to start of string!
								connectedclient->packetserver_delay = PACKETSERVER_MESSAGE_DELAY; //Delay this until we start transmitting!
							}
							connectedclient->packetserver_delay -= timepassed; //Delaying!
							if ((connectedclient->packetserver_delay <= 0.0) || (!connectedclient->packetserver_delay)) //Finished?
							{
								if ((connectedclient->packetserver_slipprotocol == 3) && connectedclient->packetserver_slipprotocol_pppoe) //Requires PAD connection!
								{
									if ((connectedclient->pppoe_discovery_PADS.length && connectedclient->pppoe_discovery_PADS.buffer) == 0) goto sendoutputbuffer; //Don't finish connecting yet! We're requiring an active PADS packet to have been received(PPPOE connection setup)!
								}
								connectedclient->packetserver_delay = (DOUBLE)0; //Finish the delay!
								if (writefifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], connectedclient->packetserver_stage_str[connectedclient->packetserver_stage_byte])) //Transmitted?
								{
									if (++connectedclient->packetserver_stage_byte == safestrlen(connectedclient->packetserver_stage_str, sizeof(connectedclient->packetserver_stage_str))) //Finished?
									{
										if ((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe)) //PPP starts immediately?
										{
											goto startPPPimmediately;
										}
										connectedclient->packetserver_delay = PACKETSERVER_SLIP_DELAY; //Delay this much!
										PacketServer_startNextStage(connectedclient, PACKETSTAGE_SLIPDELAY); //Start delay stage next before starting the server fully!
									}
								}
							}
						}

						if (connectedclient->packetserver_stage == PACKETSTAGE_SLIPDELAY) //Delay before starting SLIP communications?
						{
							connectedclient->packetserver_delay -= timepassed; //Delaying!
							if ((connectedclient->packetserver_delay <= 0.0) || (!connectedclient->packetserver_delay)) //Finished?
							{
								startPPPimmediately: //Start PPP immediately?
								connectedclient->packetserver_delay = (DOUBLE)0; //Finish the delay!
								PacketServer_startNextStage(connectedclient, PACKETSTAGE_PACKETS); //Start the SLIP service!
								if ((connectedclient->packetserver_slipprotocol == 3) && (!connectedclient->packetserver_slipprotocol_pppoe)) //PPP?
								{
									packetserver_initStartPPP(connectedclient,0); //Init!
								}
								if (connectedclient->packetserver_slipprotocol == 3) //PPP?
								{
									connectedclient->ppp_sendframing = 0; //Init: no sending active framing yet!
									connectedclient->PPP_packetstartsent = 0; //Init: no packet start has been sent yet!
								}
							}
						}
						sendoutputbuffer: //Send the output buffer to the client or other side, as required!
						if ((handledreceived) || (!*pktsrc)) //Received a packet or nothing left pending?
						{
							pkttype = !pkttype; //Next type to check!
							if (pkttype) //First type checked? Check second type now!
							{
								pktsrc = &connectedclient->IPpacket; //Packet to receive
								pktlen = &connectedclient->IPpktlen; //Packet length to receive
							}
							else //Second type checked? Check first type now!
							{
								pktsrc = &connectedclient->packet; //Packet to receive
								pktlen = &connectedclient->pktlen; //Packet length to receive
							}
							if (pkttype!=connectedclient->roundrobinpackettype) //Starting position not reached again?
							{
								goto retrypkttype; //Try the next packet type!
							}
						}
						else
						{
							//Otherwise, keep the current type pending, as it's to be processed!
							connectedclient->roundrobinpackettype = pkttype; //The packet type to keep pending!
						}
					}
				}

				//Transfer the data, one byte at a time if required!
				if ((modem.connected == 1) && (modem.connectionid>=0)) //Normal connection?
				{
					if (fifobuffer_freesize(modem.outputbuffer[0]) && peekfifobuffer(modem.blockoutputbuffer[0], &datatotransmit)) //Able to transmit something?
					{
						for (; fifobuffer_freesize(modem.outputbuffer[0]) && peekfifobuffer(modem.blockoutputbuffer[0], &datatotransmit);) //Can we still transmit something more?
						{
							if (writefifobuffer(modem.outputbuffer[0], datatotransmit)) //Transmitted?
							{
								datatotransmit = readfifobuffer(modem.blockoutputbuffer[0], &datatotransmit); //Discard the data that's being transmitted!
							}
						}
					}
					if (peekfifobuffer(modem.outputbuffer[0], &datatotransmit)) //Byte available to send?
					{
						switch (TCP_SendData(modem.connectionid, datatotransmit)) //Send the data?
						{
						case 0: //Failed to send?
							break; //Simply keep retrying until we can send it!
							modem.connected = 0; //Not connected anymore!
							if (PacketServer_running == 0) //Not running a packet server?
							{
								TCP_DisconnectClientServer(modem.connectionid); //Disconnect!
								modem.connectionid = -1;
								fifobuffer_clear(modem.inputdatabuffer[0]); //Clear the output buffer for the next client!
								fifobuffer_clear(modem.outputbuffer[0]); //Clear the output buffer for the next client!
								fifobuffer_clear(modem.blockoutputbuffer[0]); //Clear the output buffer for the next client!
								modem.connected = 0; //Not connected anymore!
								if (modem.supported < 2) //Normal mode?
								{
									modem_responseResult(MODEMRESULT_NOCARRIER);
								}
								modem.datamode = 0; //Drop out of data mode!
								modem.ringing = 0; //Not ringing anymore!
							}
							else //Disconnect from packet server?
							{
								//terminatePacketServer(modem.connectionid); //Clean up the packet server!
								fifobuffer_clear(modem.inputdatabuffer[0]); //Clear the output buffer for the next client!
								fifobuffer_clear(modem.outputbuffer[0]); //Clear the output buffer for the next client!
								fifobuffer_clear(modem.blockoutputbuffer[0]); //Clear the output buffer for the next client!
							}
							break; //Abort!
						case 1: //Sent?
							readfifobuffer(modem.outputbuffer[0], &datatotransmit); //We're send!
							break;
						default: //Unknown function?
							break;
						}
					}
					if (fifobuffer_freesize(modem.inputdatabuffer[0])) //Free to receive?
					{
						if (likely(modem.breakPending == 0)) //Not a pending break? If pending, don't receive new data until processed!
						{
							switch (TCP_ReceiveData(modem.connectionid, &datatotransmit))
							{
							case 0: //Nothing received?
								break;
							case 1: //Something received?
								if (modem.supported >= 3) //Passthrough mode with data lines that can be escaped?
								{
									if (modem.passthroughescaped) //Was the last byte an escape?
									{
										if (datatotransmit == 0xFF) //Escaped non-escaped byte?
										{
											if ((modem.passthroughlines & 4) == 0) //Not in break state?
											{
												writefifobuffer(modem.inputdatabuffer[0], datatotransmit); //Add the transmitted data to the input buffer!
											}
										}
										else //DTR/RTS/break received?
										{
											if (unlikely(((modem.passthroughlines & (modem.passthroughlines ^ datatotransmit)) & 4))) //Break was raised?
											{
												modem.breakPending = 0x10; //Pending break has been received!
											}
											modem.passthroughlines = datatotransmit; //The received lines!
										}
										modem.passthroughescaped = 0; //Not escaped anymore!
									}
									else if (datatotransmit == 0xFF) //New command! Escaped?
									{
										modem.passthroughescaped = 1; //Escaped now!
									}
									else //Non-escaped data!
									{
										if ((modem.passthroughlines & 4) == 0) //Not in break state?
										{
											writefifobuffer(modem.inputdatabuffer[0], datatotransmit); //Add the transmitted data to the input buffer!
										}
									}
								}
								else //Normal mode?
								{
									writefifobuffer(modem.inputdatabuffer[0], datatotransmit); //Add the transmitted data to the input buffer!
								}
								break;
							case -1: //Disconnected?
								modem.connected = 0; //Not connected anymore!
								if (PacketServer_running == 0) //Not running a packet server?
								{
									TCP_DisconnectClientServer(modem.connectionid); //Disconnect!
									modem.connectionid = -1;
									fifobuffer_clear(modem.inputdatabuffer[0]); //Clear the output buffer for the next client!
									fifobuffer_clear(modem.outputbuffer[0]); //Clear the output buffer for the next client!
									fifobuffer_clear(modem.blockoutputbuffer[0]); //Clear the output buffer for the next client!
									modem.connected = 0; //Not connected anymore!
									if (modem.supported < 2) //Not in passthrough mode?
									{
										modem_responseResult(MODEMRESULT_NOCARRIER);
									}
									modem.datamode = 0; //Drop out of data mode!
									modem.ringing = 0; //Not ringing anymore!
								}
								else //Disconnect from packet server?
								{
									//terminatePacketServer(modem.connectionid); //Clean up the packet server!
									fifobuffer_clear(modem.inputdatabuffer[0]); //Clear the output buffer for the next client!
									fifobuffer_clear(modem.outputbuffer[0]); //Clear the output buffer for the next client!
									fifobuffer_clear(modem.blockoutputbuffer[0]); //Clear the output buffer for the next client!
								}
								break;
							default: //Unknown function?
								break;
							}
						}
					}
				}
				//Next, process the connected clients!
				else if (modem.connected == 2) //SLIP server connection is active?
				{
					for (connectedclient = Packetserver_allocatedclients; connectedclient; connectedclient = connectedclient->next) //Check all connected clients!
					{
					performnextconnectedclient: //For now deallocated purposes.
						if (connectedclient == NULL) break; //Finish when required (deallocated last entry in the allocated list)!
						if (fifobuffer_freesize(modem.outputbuffer[connectedclient->connectionnumber]) && peekfifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], &datatotransmit)) //Able to transmit something?
						{
							for (; fifobuffer_freesize(modem.outputbuffer[connectedclient->connectionnumber]) && peekfifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], &datatotransmit);) //Can we still transmit something more?
							{
								if (writefifobuffer(modem.outputbuffer[connectedclient->connectionnumber], datatotransmit)) //Transmitted?
								{
									datatotransmit = readfifobuffer(modem.blockoutputbuffer[connectedclient->connectionnumber], &datatotransmit); //Discard the data that's being transmitted!
								}
							}
						}
						if (peekfifobuffer(modem.outputbuffer[connectedclient->connectionnumber], &datatotransmit)) //Byte available to send?
						{
							switch (TCP_SendData(connectedclient->connectionid, datatotransmit)) //Send the data?
							{
							case 0: //Failed to send?
								break; //Simply keep retrying until we can send it!
							packetserver_autherror: //Packet server authentication error?
								if (PacketServer_running == 0) //Not running a packet server?
								{
									PPPOE_finishdiscovery(connectedclient); //Finish discovery, if needed!
									TCP_DisconnectClientServer(modem.connectionid); //Disconnect!
									modem.connectionid = -1;
									fifobuffer_clear(modem.inputdatabuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
									fifobuffer_clear(modem.outputbuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
									fifobuffer_clear(modem.blockoutputbuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
									modem.connected = 0; //Not connected anymore!
									modem_responseResult(MODEMRESULT_NOCARRIER);
									modem.datamode = 0; //Drop out of data mode!
									modem.ringing = 0; //Not ringing anymore!
								}
								else //Disconnect from packet server?
								{
									PPPOE_finishdiscovery(connectedclient); //Finish discovery, if needed!
									TCP_DisconnectClientServer(connectedclient->connectionid); //Clean up the packet server!
									connectedclient->connectionid = -1; //Not connected!
									terminatePacketServer(connectedclient); //Stop the packet server, if used!
									if (connectedclient->DHCP_acknowledgepacket.length) //We're still having a lease?
									{
										PacketServer_startNextStage(connectedclient, PACKETSTAGE_DHCP);
										connectedclient->packetserver_useStaticIP = 7; //Start the release of the lease!
										connectedclient->used = 2; //Special use case: we're in the DHCP release-only state!
										tempclient = NULL; //Not used!
									}
									else //Normal release?
									{
										normalFreeDHCP(connectedclient);
										tempclient = connectedclient->next; //Who to parse next!
										freePacketserver_client(connectedclient); //Free the client list item!
									}
									fifobuffer_clear(modem.inputdatabuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
									fifobuffer_clear(modem.outputbuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
									fifobuffer_clear(modem.blockoutputbuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
									if (!Packetserver_allocatedclients) //All cleared?
									{
										modem.connected = 0; //Not connected anymore!
									}
									if (tempclient && (connectedclient->used == 0)) //Properly Deallocated?
									{
										connectedclient = tempclient; //Who to parse next! Since our old client is unavailable now!
										goto performnextconnectedclient; //Who to perform next!
									}
									else if (connectedclient->used == 0) //Finished all clients?
									{
										goto finishpollingnetwork; //Finish polling the network
									}
								}
								break; //Abort!
							case 1: //Sent?
								readfifobuffer(modem.outputbuffer[connectedclient->connectionnumber], &datatotransmit); //We're send!
								break;
							default: //Unknown function?
								break;
							}
						}
						if (fifobuffer_freesize(modem.inputdatabuffer[connectedclient->connectionnumber])) //Free to receive?
						{
							switch (TCP_ReceiveData(connectedclient->connectionid, &datatotransmit))
							{
							case 0: //Nothing received?
								break;
							case 1: //Something received?
								writefifobuffer(modem.inputdatabuffer[connectedclient->connectionnumber], datatotransmit); //Add the transmitted data to the input buffer!
								break;
							case -1: //Disconnected?
								if (PacketServer_running == 0) //Not running a packet server?
								{
									TCP_DisconnectClientServer(modem.connectionid); //Disconnect!
									modem.connectionid = -1;
									fifobuffer_clear(modem.inputdatabuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
									fifobuffer_clear(modem.outputbuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
									fifobuffer_clear(modem.blockoutputbuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
									modem.connected = 0; //Not connected anymore!
									modem_responseResult(MODEMRESULT_NOCARRIER);
									modem.datamode = 0; //Drop out of data mode!
									modem.ringing = 0; //Not ringing anymore!
								}
								else //Disconnect from packet server?
								{
									if (connectedclient->used) //Still an used client? Prevent us from working on a disconnected client!
									{
										PPPOE_finishdiscovery(connectedclient); //Finish discovery, if needed!
										terminatePacketServer(connectedclient); //Clean up the packet server!
										if (connectedclient->DHCP_acknowledgepacket.length) //We're still having a lease?
										{
											PacketServer_startNextStage(connectedclient, PACKETSTAGE_DHCP);
											connectedclient->packetserver_useStaticIP = 7; //Start the release of the lease!
											connectedclient->used = 2; //Special use case: we're in the DHCP release-only state!
											tempclient = NULL; //Not used!
										}
										else //Normal release?
										{
											normalFreeDHCP(connectedclient);
											tempclient = connectedclient->next; //Who to parse next!
											freePacketserver_client(connectedclient); //Free the client list item!
										}
										fifobuffer_clear(modem.inputdatabuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
										fifobuffer_clear(modem.outputbuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
										fifobuffer_clear(modem.blockoutputbuffer[connectedclient->connectionnumber]); //Clear the output buffer for the next client!
										if (!Packetserver_allocatedclients) //All cleared?
										{
											modem.connected = 0; //Not connected anymore!
										}
										if (tempclient && (connectedclient->used == 0)) //Properly Deallocated?
										{
											connectedclient = tempclient; //Who to parse next! Since our old client is unavailable now!
											goto performnextconnectedclient; //Who to perform next!
										}
										else if (connectedclient->used == 0) //Finished all clients?
										{
											goto finishpollingnetwork; //Finish polling the network
										}
									}
								}
								break;
							default: //Unknown function?
								break;
							}
						}
					}
				}
			} //Connected?
		finishpollingnetwork:
			continue; //Continue onwards!
		} //While polling?
	} //To poll?
}
