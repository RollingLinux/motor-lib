#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
 
static char const devPrefix[] =          "ttyACM";
static char const devDir[]    =          "/dev/";

#define SERIAL_ERROR                     -9999
#define SERIAL_OK                        0

#define MOTOR_EXIT_SAFE_START            0x83
#define MOTOR_FORWARD                    0x85
#define MOTOR_REVERSE                    0x86
#define MOTOR_BRAKE                      0x92
#define MOTOR_GET_VARIABLE               0xA1
#define MOTOR_STOP                       0xE0

#define MOTOR_VAR_REQUESTED_SPEED        20
#define MOTOR_VAR_CURRENT_SPEED          21
#define MOTOR_VAR_BRAKE                  22
#define MOTOR_VAR_BATTERY_VOLTAGE        23
#define MOTOR_VAR_CONTROLLER_TEMPERATURE 24
#define MOTOR_VAR_POWERUP_TIME_LO        28
#define MOTOR_VAR_POWERUP_TIME_HI        29

// Code from pololu example
// Reads a variable from the SMC and returns it as number between 0 and 65535.
// Returns SERIAL_ERROR if there was an error.
// The 'variableId' argument must be one of IDs listed in the
// "Controller Variables" section of the user's guide.
// For variables that are actually signed, additional processing is required
// (see smcGetTargetSpeed for an example).
int smcGetVariable(int fd, unsigned char variableId)
{
  unsigned char command[] = {MOTOR_GET_VARIABLE, variableId};
  if(write(fd, &command, sizeof(command)) == -1)
  {
    perror("error writing");
    return SERIAL_ERROR;
  }
 
  unsigned char response[2];
  if(read(fd,response,2) != 2)
  {
    perror("error reading");
    return SERIAL_ERROR;
  }
 
  return response[0] + (response[1] << 8);
}
 
// Returns a number where each bit represents a different error, and the
// bit is 1 if the error is currently active.
// See the user's guide for definitions of the different error bits.
// Returns SERIAL_ERROR if there is an error.
int smcGetErrorStatus(int fd)
{
  return smcGetVariable(fd,0);
}
  
// Sends the Exit Safe Start command, which is required to drive the motor.
// Returns 0 if successful, SERIAL_ERROR if there was an error sending.
int smcExitSafeStart(int fd)
{
  const unsigned char command = MOTOR_EXIT_SAFE_START;
  if (write(fd, &command, 1) == -1)
  {
    perror("error writing");
    return SERIAL_ERROR;
  }
  return SERIAL_OK;
}

typedef struct
{
// for future use. Should contain some specific data for future use.
  int file_descriptor;
} device_config;

device_config* open_device (const char *device_name)
{
  device_config* retval = NULL;
  int fd = open(device_name, O_RDWR | O_NOCTTY);

  if (fd != -1)
  {
    struct termios options;
    tcgetattr(fd, &options);
    options.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    options.c_oflag &= ~(ONLCR | OCRNL);
    tcsetattr(fd, TCSANOW, &options);
    device_config *dc = (device_config*) malloc (sizeof(device_config));
    if (dc != NULL)
    {
      dc->file_descriptor = fd;
      retval = dc;
    }
    else
    {
      close(fd);
    }
  }
 
  return retval;
}

int exit_safe_start (device_config* dc)
{
  unsigned int retval = SERIAL_ERROR;
  if (dc != NULL)
  {
    const unsigned char command = MOTOR_EXIT_SAFE_START;
    if (write(dc->file_descriptor, &command, 1) == -1)
    {
      perror("error writing");
    }
    else
    {
      retval = SERIAL_OK;
    }
  }
  return retval;
}

int get_requested_speed (device_config* dc)
{
  return (dc != NULL) ? smcGetVariable(dc->file_descriptor, MOTOR_VAR_REQUESTED_SPEED) : SERIAL_ERROR;
}

int get_current_speed (device_config* dc)
{
  return (dc != NULL) ? smcGetVariable(dc->file_descriptor, MOTOR_VAR_CURRENT_SPEED) : SERIAL_ERROR;
}

unsigned int get_powerup_time (device_config* dc)
{
  unsigned int retval = 0;

  if (dc != NULL)
  {
    int lo = smcGetVariable(dc->file_descriptor, MOTOR_VAR_POWERUP_TIME_LO);
    int hi = smcGetVariable(dc->file_descriptor, MOTOR_VAR_POWERUP_TIME_HI);
    if (lo != SERIAL_ERROR && hi != SERIAL_ERROR)
    {
      retval = (lo & 0xffff) + ((hi & 0xffff) << 16);
    }
  }

  return retval;
}

int get_current_voltage (device_config* dc)
{
  return (dc != NULL) ? smcGetVariable(dc->file_descriptor, MOTOR_VAR_BATTERY_VOLTAGE) : SERIAL_ERROR;
}

int get_current_temperature (device_config* dc)
{
  return (dc != NULL) ? smcGetVariable(dc->file_descriptor, MOTOR_VAR_CONTROLLER_TEMPERATURE) : SERIAL_ERROR;
}

int set_current_speed(device_config* dc, int speed)
{
  int retval = SERIAL_OK;
  unsigned char command[3];
 
  if (dc != NULL)
  {
    if (speed < 0)
    {
      command[0] = MOTOR_REVERSE;
      speed = -speed;
    }
    else
    {
      command[0] = MOTOR_FORWARD; // Motor Forward
    }
    command[1] = speed & 0x1F;
    command[2] = speed >> 5 & 0x7F;
 
    if (write(dc->file_descriptor, command, sizeof(command)) == -1)
    {
      perror("error writing");
      retval = SERIAL_ERROR;
    }
//    usleep(3000);
  }
  else
  {
    retval = SERIAL_ERROR;
  }
  return retval;
}

void close_device (device_config* dc)
{
  if (dc != NULL)
  {
    close(dc->file_descriptor);
    free(dc);
    dc = NULL;
  }
  return;
}

int main (int argc, char **argv)
{
  int retval = 0;
  struct dirent **namelist;
  int n;
  int left = 0;
  int right = 0;
  int sleep_time = 0;

  if (argc == 4)
  {
    left       = atoi(argv[1]);
    right      = atoi(argv[2]);
    sleep_time = atoi(argv[3]);
    printf ("Left  %d\nRight %d\nSleep %d\n", left, right, sleep_time);
  } 
  n = scandir(devDir, &namelist, NULL, alphasort);
  if (n < 0)
    perror("scandir");
  else
  {
    while (n--)
    {
      if (strncmp(namelist[n]->d_name, devPrefix, strlen(devPrefix)) == 0)
        printf("%s\n", namelist[n]->d_name);
      free(namelist[n]);
    }
    free(namelist);
  }
  device_config* dc0 = open_device ("/dev/ttyACM0");
  device_config* dc1 = open_device ("/dev/ttyACM1");
  printf ("dc0 %p dc1 %p\n", dc0, dc1);

  if (left == 0 && right == 0 && sleep_time == 0)
  {
    printf ("Exit safe start dc0 %d dc1 %d\n", exit_safe_start(dc0), exit_safe_start(dc1));
    printf ("Speed dc0 %d dc1 %d\n", get_requested_speed(dc0), get_requested_speed(dc1));
    printf ("Speed actual dc0 %d dc1 %d\n", get_current_speed(dc0), get_current_speed(dc1));

    int up0 = get_powerup_time(dc0);
    int up1 = get_powerup_time(dc1);
    int up_min_0 = up0 / 60000;
    int up_min_1 = up1 / 60000;
    int up_sec_0 = up0 / 1000 - up_min_0 * 60;
    int up_sec_1 = up1 / 1000 - up_min_1 * 60;

    printf ("Uptime:\n  dc0 %d:%02d %d\n  dc1 %d:%02d %d\n", up_min_0, up_sec_0, up0, up_min_1, up_sec_1, up1);
    printf ("Voltage dc0 %f dc1 %f\n", ((float)get_current_voltage(dc0)) / 1000, ((float)get_current_voltage(dc1)) / 1000);
    printf ("Temperature dc0 %f dc1 %f\n", ((float) get_current_temperature(dc0)) / 10, ((float) get_current_temperature(dc1)) / 10);
  }
  else
  {
    printf ("Set speed left %d\nSet speed right %d\n", set_current_speed(dc0, left), set_current_speed(dc1, right));
    usleep(sleep_time);
  }
  
  close_device(dc0);
  close_device(dc1);

  printf ("Test\n");
  return retval;
}
