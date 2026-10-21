#ifndef NJIT_MANAGED_H
#define NJIT_MANAGED_H
#include <stddef.h>
#include <stdint.h>
#ifndef NJIT_CREDENTIALS
#define NJIT_CREDENTIALS "/etc/njitclient/credentials"
#endif
#ifndef NJIT_RUNDIR
#define NJIT_RUNDIR "/var/run/njitclient"
#endif
struct njit_credentials { char username[89]; char password[112]; char account[89]; char domain[89]; };
int njit_read_options(int *, int *);
int njit_can_reconnect(void);
int njit_wait_link(void);
int njit_carrier(void);
int njit_read_credentials(const char *, struct njit_credentials *);
void njit_erase(void *, size_t);
int njit_frame_valid(const uint8_t *, size_t);
int njit_runtime_init(void);
int njit_stopping(void);
void njit_event(const char *);
int njit_retry_pause(void);
void njit_wait_for_manual_retry(void);
void njit_runtime_close(void);
#endif
