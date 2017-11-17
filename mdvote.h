#ifndef ___MDVOTE_H___
#define ___MDVOTE_H___

#include <stdint.h>

typedef enum mdvote_type_e {
    MDVOTE_INVAL = 0,
    MDVOTE_ASSEMBLY,
    MDVOTE_MEMBER,
} mdvote_type;

char *mdvote_uuid_unparse_ext(const unsigned char uuid[16], char *out);
int64_t mdvote_get(const unsigned char uuid[16], mdvote_type type);
int mdvote_put(const unsigned char uuid[16], mdvote_type type, int64_t v);
int mdvote_del(const unsigned char uuid[16], mdvote_type type);
void mdvote_init(void);

#endif // ___MDVOTE_H___
