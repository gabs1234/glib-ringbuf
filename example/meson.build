stress_test_files = files('stress-test.c')
rw_image_files = files('rw-image.c')

executable (
    'stress-test', 
    ringbuf + stress_test_files,
    dependencies: deps, 
    c_args: c_args,
    include_directories: headers)

executable (
    'rw-image', 
    ringbuf + rw_image_files,
    dependencies: deps, 
    c_args: c_args,
    include_directories: headers)