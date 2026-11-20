/**
 * @file net_sys.c
 *
 * 2.1.0: the network system calls (group 0x80, KFN_NET_* in include/syscalls.h).
 *
 * Programs use the MCP's FELINC through these calls. On the program side FELINC's stub library
 * (felinc_mcp.a, with the normal felinc.h) turns every felinc_* function into one of them, so a
 * program written for FELINC only needs to be linked with that library instead of felinc.a.
 *
 * The MCP remembers which sockets a program opened, and closes the ones still open when the
 * program ends (also when it stops with an error). Programs can start other programs, so each
 * socket keeps the nesting level of the program that opened it: only the ending program's sockets
 * are closed, not those of the program that started it.
 */

#include <stdint.h>
#include <string.h>
#include "errors.h"
#include "net/net.h"
#include "net/net_sys.h"
#include "net/felinc/felinc.h"

/* The nesting level of the program that owns each socket; 0 = no program owns it */
static uint8_t socket_level[FELINC_MAX_SOCKETS];

/* How deep programs are nested now: 0 at the command line, 1 in a program, 2 in a program it
 * started, ... */
static uint8_t program_level = 0;

/*
 * own - remember that the running program opened a socket
 *
 * Input:  socket = a socket handle from FELINC, or a negative error
 * Output: the same value (to return it to the program)
 */
static int32_t own(int socket) {
    if (socket >= 0 && socket < FELINC_MAX_SOCKETS) {
        socket_level[socket] = program_level;
    }
    return socket;
}

/*
 * close_level - close the sockets of one program level
 *
 * Input:  level = the nesting level (> 0)
 * Output: none
 */
static void close_level(uint8_t level) {
    short s;
    for (s = 0; s < FELINC_MAX_SOCKETS; s++) {
        if (socket_level[s] == level) {
            felinc_close(s);
            socket_level[s] = 0;
        }
    }
}

/*
 * net_program_start - a program is about to run (proc_run)
 *
 * Input:  none
 * Output: none
 */
void net_program_start(void) {
    program_level++;
}

/*
 * net_program_end - a program has ended: close the sockets it left open
 *
 * Input:  none
 * Output: none
 */
void net_program_end(void) {
    if (program_level > 0) {
        if (net_state() == NET_STATE_ON) {
            close_level(program_level);
        } else {
            /* The network was stopped (NET OFF from the program, or an error): FELINC closed
             * every socket already; only forget them */
            short s;
            for (s = 0; s < FELINC_MAX_SOCKETS; s++) {
                if (socket_level[s] == program_level) {
                    socket_level[s] = 0;
                }
            }
        }
        program_level--;
    }
}

/*
 * net_program_reset - the command line was restarted (after a crash): no program runs any more
 *
 * Input:  none
 * Output: none. Closes the sockets of all programs: their normal end (net_program_end) was
 *         skipped.
 */
void net_program_reset(void) {
    while (program_level > 0) {
        net_program_end();
    }
}

/*
 * net_syscall - carry out a network system call
 *
 * Input:  function = KFN_NET_INIT ... KFN_NET_PRINT; p0-p5 = the program's arguments
 * Output: the result for the program (FELINC's result; 32 bits)
 */
int32_t net_syscall(int32_t function, int32_t p0, int32_t p1, int32_t p2, int32_t p3, int32_t p4) {
    switch (function) {
        case KFN_NET_INIT:
            /* The MCP's settings decide (SET NET), not the program's arguments. If the network
             * is off it is started now, without waiting for DHCP: the program polls as usual. */
            if (net_state() != NET_STATE_ON) {
                net_start(-1);
            }
            return (net_state() == NET_STATE_ON) ? 1 : 0;

        case KFN_NET_SHUTDOWN:
            /* Only the program's own sockets: the network stays on for the MCP and others */
            if (net_state() == NET_STATE_ON) {
                close_level(program_level);
            }
            return 0;

        case KFN_NET_POLL:
            net_poll();
            return 0;

        case KFN_NET_SOCKET:
            return own(felinc_socket((int)p0, (int)p1, (int)p2, (size_t)p3));

        case KFN_NET_BIND:
            return felinc_bind((int)p0, (uint32_t)p1, (uint16_t)p2);

        case KFN_NET_CLOSE:
            if (p0 >= 0 && p0 < FELINC_MAX_SOCKETS) {
                socket_level[p0] = 0;
            }
            return felinc_close((int)p0);

        case KFN_NET_RECVFROM:
            return (int32_t)felinc_recvfrom((int)p0, (void *)p1, (size_t)p2, (int)p3, (struct sockaddr *)p4);

        case KFN_NET_SENDTO:
            return (int32_t)felinc_sendto((int)p0, (const void *)p1, (size_t)p2, (int)p3, (struct sockaddr *)p4);

        case KFN_NET_LISTEN:
            return felinc_listen((int)p0, (int)p1);

        case KFN_NET_ACCEPT:
            return own(felinc_accept((int)p0, (struct sockaddr *)p1));

        case KFN_NET_CONNECT:
            return felinc_connect((int)p0, (const struct sockaddr *)p1);

        case KFN_NET_RECV:
            return (int32_t)felinc_recv((int)p0, (void *)p1, (size_t)p2, (int)p3);

        case KFN_NET_SEND:
            return (int32_t)felinc_send((int)p0, (const void *)p1, (size_t)p2, (int)p3);

        case KFN_NET_RESOLVE:
            return felinc_resolve((const char *)p0, (uint32_t *)p1);

        case KFN_NET_INFO:
            switch (p0) {
                case NET_INFO_IP:   return (int32_t)felinc_get_ip();
                case NET_INFO_NTP:  return (int32_t)felinc_get_ntp_server();
                case NET_INFO_MAC:  felinc_get_mac((uint8_t *)p1); return 0;
                case NET_INFO_STATS: return (int32_t)felinc_get_stats();
                default:            return ERR_BAD_ARGUMENT;
            }

        case KFN_NET_PRINT:
            /* To the program's own console (channel 0) */
            net_print_status(0, (short)p0);
            return 0;

        default:
            return ERR_GENERAL;
    }
}
