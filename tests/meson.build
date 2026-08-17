test_include = include_directories('../include')

test_easing = executable('test_easing',
  ['test_easing.c', '../src/easing.c'],
  include_directories : test_include,
  dependencies : [libm],
)
test('easing', test_easing)

test_jsonutil = executable('test_jsonutil',
  ['test_jsonutil.c', '../src/jsonutil.c'],
  include_directories : test_include,
)
test('jsonutil', test_jsonutil)

test_conf = executable('test_conf',
  ['test_conf.c', '../src/conf.c'],
  include_directories : test_include,
)
test('conf', test_conf)
