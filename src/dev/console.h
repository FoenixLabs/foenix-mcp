/**
 * Implementation of the console channel device
 *
 * The console maps to the main screen and keyboard.
 *
 */

#ifndef __CONSOLE_H
#define __CONSOLE_H

#define CON_IOCTRL_ANSI_ON  0x01            /* IOCTRL Command: turn on ANSI terminal codes */
#define CON_IOCTRL_ANSI_OFF 0x02            /* IOCTRL Command: turn off ANSI terminal codes */
#define CON_IOCTRL_ECHO_ON  0x03            /* IOCTRL Command: turn on echo of input characters */
#define CON_IOCTRL_ECHO_OFF 0x04            /* IOCTRL Command: turn off echo of input characters */
#define CON_IOCTRL_BREAK    0x05            /* IOCTRL Command: return the status of the keyboard BREAK */
#define CON_IOCTRL_CURS_ON  0x06            /* IOCTRL Command: show the cursor */
#define CON_IOCTRL_CURS_OFF 0x07            /* IOCTRL Command: hide the cursor */

//
// Install the console device driver
//
extern short con_install();

/* Where console output is copied to (2.1.2, SET CONSOLE) */
#define CON_COPY_OFF    0               /* not copied (the default) */
#define CON_COPY_COM1   1               /* the serial port COM1 */
#define CON_COPY_COM2   2               /* the serial port COM2 */

/*
 * Choose where console output is copied to (2.1.2, SET CONSOLE OFF|COM1|COM2)
 *
 * Input:  target = CON_COPY_OFF, CON_COPY_COM1 or CON_COPY_COM2
 * Output: 0, or ERR_BAD_ARGUMENT for another value
 */
extern short con_set_copy(short target);

/*
 * Where is console output copied to? (2.1.2)
 *
 * Input:  none
 * Output: CON_COPY_OFF, CON_COPY_COM1 or CON_COPY_COM2
 */
extern short con_get_copy();

#endif
