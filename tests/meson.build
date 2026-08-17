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

# Actually boots kumde (wlroots' headless backend + pixman software
# renderer -- no GPU or display needed, so this runs fine in CI) and
# verifies a kumshot screenshot has real content, not just that the
# process didn't crash. See smoke_headless.sh for why: this is the
# automated form of the manual "run it and look at a screenshot" method
# that caught kumde's worst historical bug (rendered nothing, ever, with
# zero crash and zero compiler warning).
test('smoke_headless',
  find_program('smoke_headless.sh'),
  args : [kumde_exe, kumshot_exe],
  timeout : 30,
)
