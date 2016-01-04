#ifndef KITERROR_H
#define KITERROR_H

#ifdef __cplusplus
extern "C" {
#endif

const char* Kit_GetError();
void Kit_SetError(const char* fmt, ...);
void Kit_ClearError();

#ifdef __cplusplus
}
#endif

#endif // KITERROR_H
