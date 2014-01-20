
#ifndef FIRESTEP_H
#define FIRESTEP_H
#ifdef __cplusplus
extern "C" {
#endif

int firestep_init();
void firestep_destroy();
int firestep_write(const char *buf, size_t bufsize);
const char * firestep_json();

#ifdef __cplusplus
}
#endif
#endif
