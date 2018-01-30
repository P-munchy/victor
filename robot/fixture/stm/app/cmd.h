#ifndef CMD_H
#define CMD_H

#include <stdint.h>
#include <limits.h>

//-----------------------------------------------------------------------------
//                  Master/Send
//-----------------------------------------------------------------------------

//parameterized command/response delimiters (added/removed internally)
#define CMD_PREFIX        ">>"
#define RSP_PREFIX        "<<"
#define ASYNC_PREFIX      "!!"
#define LOG_CMD_PREFIX    ">"
#define LOG_RSP_PREFIX    "<"

//io channels - Helper head vs DUT uarts etc.
enum cmd_io_e {
  CMD_IO_SIMULATOR  = 0,
  CMD_IO_HELPER     = 1,
  CMD_IO_CONSOLE    = CMD_IO_HELPER,
  CMD_IO_DUT_UART   = 2,
  CMD_IO_CONTACTS   = 3, //Charge Contact comms
};
typedef enum cmd_io_e cmd_io;

#define CMD_DEFAULT_TIMEOUT           100

//option flags
#define CMD_OPTS_EXCEPTION_EN         0x0001  //enable exceptions
#define CMD_OPTS_LOG_ERRORS           0x0002  //log errors (to console)
#define CMD_OPTS_REQUIRE_STATUS_CODE  0x0004  //missing status code is an error
#define CMD_OPTS_ALLOW_STATUS_ERRS    0x0010  //status!=0 not considered an error
#define CMD_OPTS_DBG_PRINT_ENTRY      0x1000  //print function entry with parsed params
#define CMD_OPTS_DBG_PRINT_RSP_TIME   0x2000  //print elapsed cmd time
#define CMD_OPTS_DEFAULT              (CMD_OPTS_EXCEPTION_EN | CMD_OPTS_LOG_ERRORS | CMD_OPTS_REQUIRE_STATUS_CODE | CMD_OPTS_DBG_PRINT_RSP_TIME)

//Send a command and return response string
//@return response (NULL if timeout)
//e.g. send(IO_DUT, snformat(x,x,"lcdshow %u %s /"Victor DVT/"", solo=0, color="ib"), timeout=100 );
//@param err_lvl: -1 throws exceptions. 0+ prints errors instead, indicates verbosity
char* cmdSend(cmd_io io, const char* scmd, int timeout_ms = CMD_DEFAULT_TIMEOUT, int opts = CMD_OPTS_DEFAULT, void(*async_handler)(char*) = 0 );
int cmdStatus(); //parsed rsp status of most recent cmdSend(). status=1st arg, INT_MIN if !exist or bad format
uint32_t cmdTimeMs(); //time it took for most recent cmdSend() to finish

//-----------------------------------------------------------------------------
//                  Line Parsing
//-----------------------------------------------------------------------------
//Parsing methods for ascii input strings.
//Note: valid strings must guarantee no \r \n chars and one valid \0 terminator

//@return parsed integer value of s. INT_MIN on parse err.
int cmdParseInt32(char *s);

//@return n-th argument (mutable static copy, \0-terminated). NULL if !exist.
//n=0 is command. strings enclosed by "" are treated as a single arg.
char* cmdGetArg(char *s, int n, char* out_buf=0, int buflen=0); //overload out_buf/buflen to provide user buffer to hold the argument

//@return # of args in the input string, including command arg
int cmdNumArgs(char *s);

//DEBUG: run some parsing tests
void cmdDbgParseTestbench(void);


#endif //CMD_H

