#ifndef CXXPYTHON_SRE_ENGINE_H
#define CXXPYTHON_SRE_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Native matching state owns no managed references. Marks has 2*(groups+1)
 * entries, including group zero; unmatched captures are -1. */
int cxx_sre_validate(const uint32_t *code, size_t count, size_t groups);
int cxx_sre_run(const uint32_t *code, size_t count, const uint32_t *text, size_t length,
                size_t groups, ptrdiff_t pos, ptrdiff_t endpos, int search, int fullmatch,
                int must_advance, ptrdiff_t *marks, int *lastindex, int (*poll)(void *),
                void *context);
uint32_t cxx_sre_unicode_lower(uint32_t);
uint32_t cxx_sre_unicode_upper(uint32_t);
int cxx_sre_unicode_decimal(uint32_t);
int cxx_sre_unicode_space(uint32_t);
int cxx_sre_unicode_linebreak(uint32_t);
int cxx_sre_unicode_alnum(uint32_t);

#ifdef __cplusplus
}
#endif
#endif
