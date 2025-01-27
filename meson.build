project('ringbuf', 'c',
    version: '1.0',
    default_options:['c_std=c99' ])

c_args = ['-O2', '-D_GNU_SOURCE','-Wall', '-Wextra', '-g']

glib_dep = dependency('glib-2.0', version: '>= 2.38')
deps = [glib_dep]

ringbuf = files('ringbuf.c')
headers = include_directories('.')

subdir('example')
subdir('test')
