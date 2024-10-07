#include "evaluation.h"

#include <assert.h>

#include "json-c/json.h"

bool is_valid_context(struct json_object *context)
{
  json_object_object_foreach(context, entry_key, entry_val)
  {
    if (json_object_is_type(entry_val, json_type_object) || json_object_is_type(entry_val, json_type_null) || json_object_is_type(entry_val, json_type_array))
      return false;
  }
  return true;
}

bool is_valid_rule(struct json_object *proposed_rule)
{
  json_object_object_foreach(proposed_rule, entry_key, entry_val)
  {
    if (json_object_is_type(entry_val, json_type_object) || json_object_is_type(entry_val, json_type_null))
      return false;
    if (json_object_is_type(entry_val, json_type_array))
    {
      for (int k = 0; k < json_object_array_length(entry_val); k++)
      {
        struct json_object *current_iter = json_object_array_get_idx(entry_val, k);
        if (json_object_is_type(current_iter, json_type_object) ||
            json_object_is_type(current_iter, json_type_null))
          return false;
      }
    }
  }
  return true;
}

bool matches_rule(struct json_object *rule_set, struct json_object *provided_set)
{
  struct json_object *current_val = NULL;

  int n_keys_checked = 0;
  json_object_object_foreach(rule_set, entry_key, entry_val)
  {
    current_val = json_object_object_get(provided_set, entry_key);
    assert(!json_object_is_type(current_val, json_type_object));

    // Special behavior is required for array-based rules.
    if (json_object_is_type(entry_val, json_type_array))
    {
      bool found = false;
      for (int k = 0; k < json_object_array_length(entry_val); k++)
      {
        struct json_object *current_iter = json_object_array_get_idx(entry_val, k);
        if (json_object_is_type(current_iter, json_type_array) || json_object_is_type(current_iter, json_type_object))
          return false;
        found |= json_object_equal(current_iter, current_val);
      }
      if (!found)
        return false;
      continue;
    }

    if (!json_object_equal(entry_val, current_val))
      return false;
    n_keys_checked++;
  }

  return n_keys_checked > 0;
}
