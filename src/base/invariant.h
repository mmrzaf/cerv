#ifndef CERV_INVARIANT_H
#define CERV_INVARIANT_H

void cerv_invariant_fail(void);

#define CERV_INVARIANT(condition) ((condition) ? (void)0 : cerv_invariant_fail())

#endif
