#ifndef COMMON_H_
#define COMMON_H_

// Replaces H2O_STRLIT in our custom use cases, in case H2O_STRLIT implementation
// details change.
#define STRLIT(str) (str), (sizeof(str) - 1)

#endif // COMMON_H_
