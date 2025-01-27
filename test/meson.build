test(
    'unit tests',
    executable(
        'basic_tests',
        ['test-basic.c', ringbuf],
        dependencies: deps
    ),
    env: [
    'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
    'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
    ],
    protocol: 'tap'
)

test(
    'unit tests',
    executable(
        'concurrency_tests',
        ['test-concurrency.c', ringbuf],
        dependencies: deps
    ),
    env: [
    'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
    'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
    ],
    protocol: 'tap'
)