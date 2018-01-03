#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "core/common.h"
#include "core/serial.h"
#include "core/clock.h"
#include "core/lcd.h"
#include "helpware/helper_text.h"
#include "helpware/display.h"
#include "helpware/logging.h"
#include "helpware/pidopen.h"


#define FIXTURE_TTY "/dev/ttyHSL1"
#define FIXTURE_BAUD B1000000


#define LINEBUFSZ 255




int shellcommand(const char* command, int timeout_sec) {
  int retval = -666;
  uint64_t expiration = steady_clock_now()+(timeout_sec*NSEC_PER_SEC);


  fixture_log_writestring("-BEGIN SHELL- ");
  fixture_log_writestring(command);
  fixture_log_writestring("\n");

  int pid;
  int pfd = pidopen("./headprogram", &pid);
  bool timedout = false;

  if (pfd>0) {
    uint64_t scnow = steady_clock_now();
    char buffer[512];
    int n;
    do {
      if (wait_for_data(pfd, 0)) {
        n = read(pfd, buffer, 512);
        if (n>0) {
          printf("%.*s", n, buffer);
          fixture_log_write(buffer,n);

        }
      }
      scnow = steady_clock_now();
      if (scnow > expiration) {
        printf("TIMEOUT after %d sec\n", timeout_sec);
        fixture_log_writestring("TIMEOUT ");
        timedout = true;
        break;
      }
    } while (n>0 );
    retval = pidclose(pid, timedout);
  }


  fixture_log_writestring("--END SHELL-- ");
  fixture_log_writestring(command);
  fixture_log_writestring("\n");

  return retval;

}

int handle_lcdset_command(const char* cmd, int len) {
  return helper_lcdset_command_parse(cmd, len);
}
int handle_lcdshow_command(const char* cmd, int len) {
  return helper_lcdshow_command_parse(cmd, len);
}
int handle_lcdclr_command(const char* cmd, int len) {
  //"clr" is same as "set 0"
  return helper_lcdset_command_parse("0 \n", 3);
}
int handle_logstart_command(const char* cmd, int len) {
  return fixture_log_start(cmd, len);
}
int handle_logstop_command(const char* cmd, int len) {
  return fixture_log_stop(cmd, len);
  return 0;
}



int handle_dutprogram_command(const char* cmd, int len) {
  char* num_end;
  long timeout_sec = strtol(cmd, &num_end, 10);
  printf("timeout = %ld\n", timeout_sec);
  if (num_end == cmd || timeout_sec == 0) {
    timeout_sec = LONG_MAX;
  }
  return shellcommand("./headprogram", timeout_sec);

}

int handle_shell_timeout_test_command(const char* cmd, int len) {
  //return handle_dutprogram_command(cmd, len);
  printf("shell test disabled\n"); fflush(stdout);
  fixture_log_writestring("shell test disabled\n");
  return 0;
}

#define REGISTER_COMMAND(s) {#s, sizeof(#s)-1, handle_##s##_command}


typedef int (*CommandParser)(const char*, int);

typedef struct CommandHandler_t {
  const char* name;
  const int len;
  const CommandParser handler;
} CommandHandler;

static const CommandHandler handlers[] = {
  REGISTER_COMMAND(lcdset),
  REGISTER_COMMAND(lcdshow),
  REGISTER_COMMAND(lcdclr),
  REGISTER_COMMAND(logstart),
  REGISTER_COMMAND(logstop),
  REGISTER_COMMAND(dutprogram),
  REGISTER_COMMAND(shell_timeout_test),
  {"shell-timeout-test", 18, handle_shell_timeout_test_command},
 /* ^^ insert new commands here ^^ */
  {0}
};

const char* fixture_command_parse(const char*  command, int len) {
  static char responseBuffer[LINEBUFSZ];

//  printf("\tparsing  \"%.*s\"\n", len, command);

  const CommandHandler* candidate = &handlers[0];

  while (candidate->name) {
    if (len >= candidate->len &&
        strncmp(command, candidate->name, candidate->len)==0)
    {
      int status = candidate->handler(command+candidate->len, len-candidate->len);
      snprintf(responseBuffer, LINEBUFSZ, "<<%s %d\n", candidate->name, status);
      return responseBuffer;
    }
    candidate++;
  }
  //not recognized, echo back invalid command with error code
  char* endcmd = memchr(command, ' ', len);
  if (endcmd) { len = endcmd - command; }
  int i;
  responseBuffer[0]='<';
  responseBuffer[1]='<';
  for (i=0;i<len;i++)
  {
    responseBuffer[2+i]=*command++;
  }
  snprintf(responseBuffer+2+i, LINEBUFSZ-2-i, " %d\n", -1);
  return responseBuffer;

}

const char* find_line(const char* buf, int buflen, const char** last)
{
  if (!buflen) {return NULL;}
  assert(buf <= *last && *last <= buf+buflen);
  int remaining = buflen - (*last - buf);
  const char* token = memchr(*last, '\n', remaining);
  if (token) {
    *last = token+1;
    return buf;
  }
  return NULL;
}

int linecount = 0 ;
int fixture_serial(int serialFd) {
  static char linebuf[LINEBUFSZ+1];
  const char* response = NULL;
  static int linelen = 0;
  int nread = serial_read(serialFd, (uint8_t*)linebuf+linelen, LINEBUFSZ-linelen);
  if (nread<=0) { return 0; }
  const char* endl = linebuf+linelen;


  printf("%.*s", nread, linebuf+linelen);
  fflush(stdout);
  fixture_log_write(linebuf+linelen, nread);

  linelen+=nread;
  if (linelen >= LINEBUFSZ)
  {
    printf("TOO MANY CHARACTERS, truncating to %d\n", LINEBUFSZ);
    linelen = LINEBUFSZ;
    linebuf[linelen] = '\n';
  }
  const char* line = find_line(linebuf, linelen, &endl);
  while (line) {
    linecount++;
    if (line[0]=='>' && line[1]=='>') {
      response = fixture_command_parse(line+2, endl-line-2);
      if (response) {
        serial_write(serialFd, (uint8_t*)response, strlen(response));
      }
    }
    linelen-=(endl-line);
    line = find_line(endl, linelen, &endl);
  }
  if (linelen) {
    memmove(linebuf, endl, linelen);
  }
  return 0;
}


static int gSerialFd;


#define LINEBUFSZ 255




void enable_kbhit(bool enable)
{
  static struct termios oldt, newt;
  static bool active;

  if ( enable && !active)
  {
    tcgetattr( STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~( ICANON | ECHO );
    tcsetattr( STDIN_FILENO, TCSANOW, &newt);
    active = true;
  }
  else if (!enable && active) {
    tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
    active = false;
  }
}

int kbhit (void)
{
  struct timeval tv;
  fd_set rdfs;

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  FD_ZERO(&rdfs);
  FD_SET (STDIN_FILENO, &rdfs);

  select(STDIN_FILENO+1, &rdfs, NULL, NULL, &tv);
  return FD_ISSET(STDIN_FILENO, &rdfs);

}

int user_terminal(void) {
  static int linelen = 0;
  static char linebuf[LINEBUFSZ+1];
  if (kbhit()) {
    int nread = read(0, linebuf+linelen, LINEBUFSZ-linelen);
    if (nread<0) { return 1; }
    serial_write(gSerialFd, (uint8_t*)linebuf+linelen, nread);

    char* endl = memchr(linebuf+linelen, '\n', nread);
    if (!endl) {
      linelen+=nread;
      if (linelen >= LINEBUFSZ)
      {
        printf("TOO MANY CHARACTERS, truncating to %d\n", LINEBUFSZ);
        endl = linebuf+LINEBUFSZ-1;
        *endl = '\n';
      }
    }
    if (endl) {
      if (strncmp(linebuf, "quit", 4)==0)  {
        return 1;
      }
      if (linebuf[0]=='>'&& linebuf[1]=='>') {
        const char * response = fixture_command_parse(linebuf+2, endl-linebuf-2);
        if (response) {
          printf("~%s", response);
        }
      }
      linelen = 0;
    }
  }
  return 0;
}


void on_exit(void)
{
  if (gSerialFd) {
    close(gSerialFd);
  }
  fixture_log_terminate();
  enable_kbhit(0);
}


void safe_quit(int n)
{
  error_exit(app_USAGE, "Caught signal %d \n", n);
}



int main(int argc, const char* argv[])
{
  bool exit = false;

  signal(SIGINT, safe_quit);
  signal(SIGKILL, safe_quit);

  lcd_init();
  lcd_set_brightness(20);
  display_init();
  fixture_log_init();

  gSerialFd = serial_init(FIXTURE_TTY, FIXTURE_BAUD);

  serial_write(gSerialFd, (uint8_t*)"\x1b\x1b\n", 4);
  serial_write(gSerialFd, (uint8_t*)"reset\n", 6);

  enable_kbhit(1);
  while (!exit)
  {
    exit = fixture_serial(gSerialFd);
    exit |= user_terminal();
 }

  on_exit();

  return 0;


}
