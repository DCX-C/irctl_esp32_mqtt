#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define NTP_TIMESTAMP_DELTA 2208988800ull
#define BEIJING_TIME_DELTA 8*60*60

void error( char* msg )
{
    perror( msg ); // Print the error message to stderr.

    exit( 0 ); // Quit the process.
}

typedef struct
{
  uint8_t li_vn_mode;      // Eight bits. li, vn, and mode.
                           // li.   Two bits.   Leap indicator.
                           // vn.   Three bits. Version number of the protocol.
                           // mode. Three bits. Client will pick mode 3 for client.
  uint8_t stratum;         // Eight bits. Stratum level of the local clock.
  uint8_t poll;            // Eight bits. Maximum interval between successive messages.
  uint8_t precision;       // Eight bits. Precision of the local clock.
  uint32_t rootDelay;      // 32 bits. Total round trip delay time.
  uint32_t rootDispersion; // 32 bits. Max error aloud from primary clock source.
  uint32_t refId;          // 32 bits. Reference clock identifier.
  uint32_t refTm_s;        // 32 bits. Reference time-stamp seconds.
  uint32_t refTm_f;        // 32 bits. Reference time-stamp fraction of a second.
  uint32_t origTm_s;       // 32 bits. Originate time-stamp seconds.
  uint32_t origTm_f;       // 32 bits. Originate time-stamp fraction of a second.
  uint32_t rxTm_s;         // 32 bits. Received time-stamp seconds.
  uint32_t rxTm_f;         // 32 bits. Received time-stamp fraction of a second.
  uint32_t txTm_s;         // 32 bits and the most important field the client cares about. Transmit time-stamp seconds.
  uint32_t txTm_f;         // 32 bits. Transmit time-stamp fraction of a second.
} ntp_packet;              // Total: 384 bits or 48 bytes.

struct tm* time_print()
{
  int sockfd, n; // Socket file descriptor and the n return result from writing/reading from the socket.

  int portno = 123; // NTP UDP port number.

  char* host_name = "ntp2.aliyun.com"; // NTP server host-name.

  // Structure that defines the 48 byte NTP packet protocol.

  // Create and zero out the packet. All 48 bytes worth.

  ntp_packet packet = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

  memset( &packet, 0, sizeof( ntp_packet ) );

  // Set the first byte's bits to 00,011,011 for li = 0, vn = 3, and mode = 3. The rest will be left set to zero.

  *( ( char * ) &packet + 0 ) = 0x1b; // Represents 27 in base 10 or 00011011 in base 2.

  // Create a UDP socket, convert the host-name to an IP address, set the port number,
  // connect to the server, send the packet, and then read in the return packet.

  struct sockaddr_in serv_addr; // Server address data structure.
  struct hostent *server;      // Server data structure.

  sockfd = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP ); // Create a UDP socket.

  if ( sockfd < 0 )
    error( "ERROR opening socket" );

  server = gethostbyname( host_name ); // Convert URL to IP.

  if ( server == NULL )
    error( "ERROR, no such host" );

  // Zero out the server address structure.

  bzero( ( char* ) &serv_addr, sizeof( serv_addr ) );

  serv_addr.sin_family = AF_INET;

  // Copy the server's IP address to the server address structure.

  bcopy( ( char* )server->h_addr, ( char* ) &serv_addr.sin_addr.s_addr, server->h_length );

  // Convert the port number integer to network big-endian style and save it to the server address structure.

  serv_addr.sin_port = htons( portno );

  // Call up the server using its IP address and port number.

  if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr) ) < 0 )
    error( "ERROR connecting" );

  // Send it the NTP packet it wants. If n == -1, it failed.

  n = write( sockfd, ( char* ) &packet, sizeof( ntp_packet ) );

  if ( n < 0 )
    error( "ERROR writing to socket" );

  // Wait and receive the packet back from the server. If n == -1, it failed.

  n = read( sockfd, ( char* ) &packet, sizeof( ntp_packet ) );

  if ( n < 0 )
    error( "ERROR reading from socket" );

  close(sockfd);
  // These two fields contain the time-stamp seconds as the packet left the NTP server.
  // The number of seconds correspond to the seconds passed since 1900.
  // ntohl() converts the bit/byte order from the network's to host's "endianness".

  packet.txTm_s = ntohl( packet.txTm_s ); // Time-stamp seconds.
  packet.txTm_f = ntohl( packet.txTm_f ); // Time-stamp fraction of a second.

  // Extract the 32 bits that represent the time-stamp seconds (since NTP epoch) from when the packet left the server.
  // Subtract 70 years worth of seconds from the seconds since 1900.
  // This leaves the seconds since the UNIX epoch of 1970.
  // (1900)------------------(1970)**************************************(Time Packet Left the Server)

  time_t txTm = ( time_t ) ( packet.txTm_s - NTP_TIMESTAMP_DELTA + BEIJING_TIME_DELTA );

  // Print the time we got from the server, accounting for local timezone and conversion from UTC time.

  printf( "Time: %s", ctime( ( const time_t* ) &txTm ) );
  return localtime(&txTm);
}

time_t time_stamp()
{
  int sockfd, n; // Socket file descriptor and the n return result from writing/reading from the socket.

  int portno = 123; // NTP UDP port number.

  char host_name[] = "ntpx.aliyun.com"; // NTP server host-name.

  // Structure that defines the 48 byte NTP packet protocol.

  // Create and zero out the packet. All 48 bytes worth.

  ntp_packet packet = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

  memset( &packet, 0, sizeof( ntp_packet ) );

  // Set the first byte's bits to 00,011,011 for li = 0, vn = 3, and mode = 3. The rest will be left set to zero.

  *( ( char * ) &packet + 0 ) = 0x1b; // Represents 27 in base 10 or 00011011 in base 2.

  // Create a UDP socket, convert the host-name to an IP address, set the port number,
  // connect to the server, send the packet, and then read in the return packet.

  struct sockaddr_in serv_addr; // Server address data structure.
  struct hostent *server;      // Server data structure.

  sockfd = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP ); // Create a UDP socket.
  struct timeval timeout;
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;
  setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
  setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);

  if ( sockfd < 0 )
    error( "ERROR opening socket" );

  for(int i = 0;i<4;i++)
  {
      sprintf(host_name, "ntp%01d.aliyun.com", i);
      printf("host: %s\n", host_name);
      server = gethostbyname( host_name ); // Convert URL to IP.

      if ( server == NULL ) {
        continue;
      } 

      // Zero out the server address structure.
  
      bzero( ( char* ) &serv_addr, sizeof( serv_addr ) );
  
      serv_addr.sin_family = AF_INET;
  
      // Copy the server's IP address to the server address structure.
  
      bcopy( ( char* )server->h_addr, ( char* ) &serv_addr.sin_addr.s_addr, server->h_length );
  
      // Convert the port number integer to network big-endian style and save it to the server address structure.
  
      serv_addr.sin_port = htons( portno );
  
      // Call up the server using its IP address and port number.
      printf("connect...\n");
      if ( connect( sockfd, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr) ) < 0 )
        continue;
  
  
      // Send it the NTP packet it wants. If n == -1, it failed.
      printf("write...\n");
      n = write( sockfd, ( char* ) &packet, sizeof( ntp_packet ) );
  
      if ( n < sizeof( ntp_packet ) )
        continue;
  
      // Wait and receive the packet back from the server. If n == -1, it failed.
      printf("read...\n");
      n = read( sockfd, ( char* ) &packet, sizeof( ntp_packet ) );
  
      if ( n < sizeof( ntp_packet ) )
        continue;
  }
  printf("close...\n");
  close(sockfd);
  // These two fields contain the time-stamp seconds as the packet left the NTP server.
  // The number of seconds correspond to the seconds passed since 1900.
  // ntohl() converts the bit/byte order from the network's to host's "endianness".

  packet.txTm_s = ntohl( packet.txTm_s ); // Time-stamp seconds.
  packet.txTm_f = ntohl( packet.txTm_f ); // Time-stamp fraction of a second.

  // Extract the 32 bits that represent the time-stamp seconds (since NTP epoch) from when the packet left the server.
  // Subtract 70 years worth of seconds from the seconds since 1900.
  // This leaves the seconds since the UNIX epoch of 1970.
  // (1900)------------------(1970)**************************************(Time Packet Left the Server)
    printf("ctime()...\n");
  time_t txTm = ( time_t ) ( packet.txTm_s - NTP_TIMESTAMP_DELTA + BEIJING_TIME_DELTA );
  printf( "Time: %s", ctime( ( const time_t* ) &txTm ) );
  // Print the time we got from the server, accounting for local timezone and conversion from UTC time.
  return txTm;
}


