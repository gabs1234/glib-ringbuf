project('ringbuf', 'c',
    version: '1.0',
    default_options:['c_std=c99' ])

c_args = ['-O2', '-D_GNU_SOURCE','-Wall', '-Wextra', '-fPIC', '-msse4', '-maes']

headers = include_directories('.')

glib_dep = dependency('glib-2.0', version: '>= 2.38')
deps = [glib_dep]

sources = files('ringbuf-test.c', 'ringbuf.c')

executable (
    'ringbuf', 
    sources, 
    dependencies: deps, 
    c_args: c_args,
    include_directories: headers)