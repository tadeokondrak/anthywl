foreach file: ['default_config', 'ca.tadeo.anthywl.varlink']
    anthywl_src += custom_target(file,
        input: '../data' / file,
        output: file + '.inc',
        command: [file2string, '@INPUT@', '@OUTPUT@'],
    )
endforeach

anthywl_inc += include_directories('.')
