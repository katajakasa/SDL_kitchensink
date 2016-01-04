#ifndef KITPLAYER_H
#define KITPLAYER_H

#include "kitchensink/kitsource.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kit_Player {

} Kit_Player;

Kit_Player* Kit_CreatePlayer(Kit_Source *src);
void Kit_ClosePlayer(Kit_Player *player);



#ifdef __cplusplus
}
#endif

#endif // KITPLAYER_H
