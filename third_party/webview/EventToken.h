/* Minimal stub for EventToken.h.
 * MinGW-w64 doesn't ship this WinRT header. WebView2.h only needs the
 * EventRegistrationToken struct from it.
 */
#pragma once

#ifndef __EventToken_h__
#define __EventToken_h__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EventRegistrationToken {
    __int64 value;
} EventRegistrationToken;

#ifdef __cplusplus
}
#endif

#endif /* __EventToken_h__ */
