#include <stdio.h>

#include "unity/unity.h"
#include "json-c/json.h"

#include "evaluation.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_matches_rule_basic(void)
{
  struct json_object *obj = json_object_new_object();
  json_object_object_add(obj, "userId", json_object_new_int(5));
  json_object_object_add(obj, "carMake", json_object_new_string("Honda"));

  struct json_object *rule = json_object_new_object();
  json_object_object_add(rule, "userId", json_object_new_int(5));
  TEST_ASSERT_TRUE(matches_rule(rule, obj));

  json_object_put(obj);
  json_object_put(rule);
}

void test_matches_rule_array(void)
{
  struct json_object *obj = json_object_new_object();
  json_object_object_add(obj, "userId", json_object_new_int(5));
  json_object_object_add(obj, "carMake", json_object_new_string("Honda"));

  struct json_object *rule = json_object_new_object();
  struct json_object *array_rule = json_object_new_array();
  json_object_array_add(array_rule, json_object_new_int(7));
  json_object_array_add(array_rule, json_object_new_string("Mitsubishi"));
  json_object_object_add(rule, "carMake", array_rule);

  json_object_object_add(rule, "userId", json_object_new_int(5));
  fprintf(stderr, "obj: %s\n", json_object_to_json_string(obj));
  fprintf(stderr, "rule: %s\n", json_object_to_json_string(rule));
  TEST_ASSERT_FALSE(matches_rule(rule, obj));

  json_object_array_add(array_rule, json_object_new_string("Honda"));
  fprintf(stderr, "obj: %s\n", json_object_to_json_string(obj));
  fprintf(stderr, "rule: %s\n", json_object_to_json_string(rule));
  TEST_ASSERT_TRUE(matches_rule(rule, obj));

  json_object_put(obj);
  json_object_put(rule);
}

void test_matches_rule_empty(void){
    struct json_object *obj = json_object_new_object();
  json_object_object_add(obj, "userId", json_object_new_int(5));
  json_object_object_add(obj, "carMake", json_object_new_string("Honda"));

  struct json_object *rule = json_object_new_object();
  TEST_ASSERT_FALSE(matches_rule(rule, obj));

  json_object_put(obj);
  json_object_put(rule);
}

void test_is_valid_rule(void)
{
  struct json_object *empty_rule = json_tokener_parse("{}");
  TEST_ASSERT_TRUE(is_valid_rule(empty_rule));
  json_object_put(empty_rule);

  struct json_object *invalid_rule = json_tokener_parse("{ \"id\": { } }");
  TEST_ASSERT_FALSE(is_valid_rule(invalid_rule));
  json_object_put(invalid_rule);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_matches_rule_basic);
  RUN_TEST(test_matches_rule_array);
  RUN_TEST(test_matches_rule_empty);
  RUN_TEST(test_is_valid_rule);
  return UNITY_END();
}
