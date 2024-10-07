#ifndef EVALUATION_H_
#define EVALUATION_H_

#include <stdbool.h>

struct json_object;

// Returns whether a given JSON object is the proper format of a rule (i.e.,
// depth == 1 with optional arrays and no `null`s).
bool is_valid_rule(struct json_object *proposed_rule);

// Returns whether a given JSON object is the proper format of a context (i.e.,
// depth == 1 with no arrays, objects, or `null`s).
bool is_valid_context(struct json_object *context);

// Returns whether a given JSON object is a superset of `rule_set`. A JSON object
// with depth > 1 is rejected and returns `false`.
bool matches_rule(struct json_object *rule_set, struct json_object *provided_set);

#endif // EVALUATION_H_
